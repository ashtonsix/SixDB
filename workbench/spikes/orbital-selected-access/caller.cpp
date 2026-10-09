#include "consumer.h"
#include "fixture.h"
#include <array>
#include <cstdio>
#include <optional>

int main() {
  using namespace selected;
  fixture::Memory memory;
  Store store;
  constexpr std::array<std::byte, 8> authored{std::byte{7}, std::byte{1}, std::byte{2}, std::byte{3},
      std::byte{4}, std::byte{5}, std::byte{6}, std::byte{9}};
  auto first = fixture::representation(memory, {1, 7}, 17, 1, authored);
  fixture::publish(store, first, 1);

  // The ordinary uninterrupted caller owns Store; it borrows, binds and reads.
  auto borrowed = store.borrow();
  auto selected = bind(borrowed, 1); assert(selected);
  auto word = consumer::word(*selected, 0, 1); assert(word && *word != 0);
  auto view = ready(*selected, 0, {1, 0, 8, 1, 0}); assert(view);
  assert(view->bytes().front() == std::byte{7});

  // Escape retains once before conversion/publication; the former caller may end.
  auto retained = retain(borrowed, *selected); assert(retained);
  auto escaped = RetainedView::make(std::move(*retained), *view); assert(escaped);
  auto alternative = fixture::representation(memory, {1, 7}, 18, 1, authored);
  assert(store.install_representation(borrowed, alternative));
  auto other = bind(borrowed, 1, alternative); assert(other);
  assert(consumer::word(*other, 0, 1) == word);

  constexpr std::array<std::byte, 8> next{std::byte{42}};
  fixture::publish(store, fixture::representation(memory, {1, 8}, 19, 1, next), 2);
  assert(escaped->view().bytes().front() == std::byte{7});
  assert(!bind(borrowed, 1)); // The cheap owner borrow ended at logical installation.
  auto current = store.read();
  assert(consumer::word(current, 1, 0, 1) != word);
  std::puts("ordinary separate-TU caller: borrowed read, explicit escape, alternative, new publication: passed");
}
