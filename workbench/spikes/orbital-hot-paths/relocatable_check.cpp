// Single-threaded protocol checks for relocatable handles: nested and
// interleaved Sections keep the outermost epoch, and retirement waits for it.
#include "relocatable.h"
#include <orbital/worker.h>
#include <cstdio>
#include <cstdlib>
#include <thread>

namespace {
namespace a = orbital::allocation;
namespace w = orbital::worker;
void check(bool ok, const char* why) { if (!ok) { std::fprintf(stderr, "relocatable: %s\n", why); std::abort(); } }
}

int main() {
  auto arena = *a::open_arena(64 << 20);
  w::Lane lane; check(bool(w::adopt(lane)), "adopt");
  auto* pool = *a::add_pool(*arena, lane);
  {
    std::array<relocatable::Reader, 1> readers;
    relocatable::Table table(*pool, 4, readers);
    const auto h = *table.create(64);
    {
      relocatable::Table::Section outer(table, readers[0]);
      const auto view = table.resolve(h);
      { relocatable::Table::Section inner(table, readers[0]); (void)table.resolve(h); }
      check(readers[0].epoch.load() != relocatable::idle, "inner Section ended the outer protection");
      check(bool(table.replace(h, 200, [](std::byte*, std::size_t) {})), "replace");
      check(table.collect() == 0 && table.retired() == 1, "collect freed a payload the outer Section can hold");
      (void)view;
    }
    check(table.collect() == 1 && table.retired() == 0, "retirement did not follow the outer Section");
    // Interleaved (non-LIFO) Sections on one Reader, as two Tasks on one worker.
    auto* first = new relocatable::Table::Section(table, readers[0]);
    auto* second = new relocatable::Table::Section(table, readers[0]);
    check(bool(table.replace(h, 64, [](std::byte*, std::size_t) {})), "replace");
    delete first;
    check(table.collect() == 0, "ending the first of two interleaved Sections exposed the payload");
    delete second;
    check(table.collect() == 1, "last Section did not release protection");
    check(bool(table.destroy(h)), "destroy");
    table.collect();
  }
  {
    // Two Tables on one thread: each has its own Reader, so nesting one
    // Table's Section inside another's cannot withdraw either protection.
    std::array<relocatable::Reader, 1> first_reader, second_reader;
    relocatable::Table first(*pool, 2, first_reader), second(*pool, 2, second_reader);
    check(first_reader[0].table == &first && second_reader[0].table == &second, "Readers bound to their Tables");
    const auto x = *first.create(64), y = *second.create(64);
    {
      relocatable::Table::Section outer(first, first_reader[0]);
      (void)first.resolve(x);
      { relocatable::Table::Section inner(second, second_reader[0]); (void)second.resolve(y); }
      check(bool(first.replace(x, 200, [](std::byte*, std::size_t) {})), "replace in first");
      check(first.collect() == 0, "another Table's Section withdrew this Table's protection");
    }
    check(first.collect() == 1, "first Table retirement did not follow its Section");
    check(bool(first.destroy(x)) && bool(second.destroy(y)), "destroy");
    first.collect(); second.collect();
  }
  for (;;) {
    auto progress = a::close(*pool);
    if (progress.kind() == orbital::Progress::Kind::done) break;
    w::poll(lane, 64); std::this_thread::yield();
  }
  (void)a::remove_pool(*pool); w::relinquish(lane);
  std::puts("relocatable: nested, interleaved and cross-Table Sections hold retirement until the last ends");
}
