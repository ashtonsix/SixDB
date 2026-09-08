#include "model.h"
#include <benchmark/benchmark.h>
#include <chrono>
#include <iostream>
#include <regex>
#include <sched.h>
#include <stdexcept>

using namespace signatures;
using Clock = std::chrono::steady_clock;
int main(int argc, char **argv) {
  try {
    std::string select =
                    "^(absent|near|range)/"
                    "(aos|soa|tag8|tag16|tag32|native|prefix8|prefix16)/q0$",
                text;
    size_t n = 65536;
    unsigned cpu = 0;
    U seed = 41, hash = 13;
    bool accounting = false;
    std::vector<char *> args{argv[0]};
    for (int i = 1; i < argc; ++i) {
      std::string a = argv[i];
      if (a.starts_with("--select="))
        select = a.substr(9);
      else if (a.starts_with("--rows="))
        n = std::stoull(a.substr(7));
      else if (a.starts_with("--cpu="))
        cpu = unsigned(std::stoul(a.substr(6)));
      else if (a.starts_with("--seed="))
        seed = std::stoull(a.substr(7));
      else if (a.starts_with("--hash="))
        hash = std::stoull(a.substr(7));
      else if (a.starts_with("--text="))
        text = a.substr(7);
      else if (a == "--accounting")
        accounting = true;
      else
        args.push_back(argv[i]);
    }
    if (!n)
      throw std::runtime_error("Positive row count required");
    cpu_set_t cpus;
    CPU_ZERO(&cpus);
    CPU_SET(cpu, &cpus);
    if (sched_setaffinity(0, sizeof(cpus), &cpus))
      throw std::runtime_error("CPU affinity failed");
    std::regex re(select);
    unsigned registered = 0;
    if (accounting)
      std::cout
          << "case,method,phase,rows,operations,seed,hash,truth,candidates,"
             "residuals,certain,plane_bytes,summary_bytes,blocks,skipped,text_"
             "bytes,g0,g1,g2,g3,stored_bytes,fixture_bytes,digest,correct\n";
    for (auto c : cases())
      for (auto m : methods(c))
        for (std::string phase : {"q0", "q1", "q2", "q3", "build", "replace"}) {
          bool writes = m == "aos" || m == "tag8" || m == "tag16" ||
                        m == "tag32" || m.starts_with("shared");
          if (phase == "replace" && (c.family != "scalar" || !writes))
            continue;
          if (phase == "build" &&
              (m == "aos" || m == "soa" || m == "native" || m == "direct"))
            continue;
          std::string label = c.name + "/" + m + "/" + phase;
          if (!std::regex_search(label, re))
            continue;
          ++registered;
          if (c.name == "text" && text.empty())
            throw std::runtime_error(
                "Text case requires --text prepared input");
          auto run = [=](benchmark::State *state) {
            auto f = fixture(c, n, seed, text);
            auto s = build(f, m, hash);
            std::vector<U> mask, expected;
            Query q =
                f.queries[phase.starts_with('q') ? unsigned(phase[1] - '0')
                                                 : 0];
            auto want = oracle(f, q, &expected);
            auto got = scan(f, s, q, hash, mask);
            if (got != want || mask != expected)
              throw std::runtime_error("Before-timing oracle: " + label);
            U fixture_bytes = f.rows.capacity() * sizeof(Row);
            for (auto &x : f.columns)
              fixture_bytes += x.capacity() * 4;
            fixture_bytes += f.strings.capacity() * sizeof(std::string);
            for (auto &x : f.strings)
              fixture_bytes += x.capacity() + 1;
            U digest = 0;
            for (auto &r : f.rows)
              for (auto v : r)
                digest = mix(digest ^ v);
            for (auto &x : f.strings)
              for (unsigned char v : x)
                digest = mix(digest ^ v);
            std::vector<std::pair<size_t, unsigned>> updates;
            for (size_t k = 0; k < std::min<size_t>(8192, f.size()); ++k)
              updates.emplace_back(mix(seed + k) % f.size(),
                                   unsigned(mix(seed + 991 + k) % 8));
            auto update = [&] {
              for (unsigned pass = 0; pass < 2; ++pass)
                for (auto [i, field] : updates)
                  replace(f.rows[i], s, i, field,
                          f.rows[i][field] ^ 0x80001001u, hash);
            };
            U operations = phase == "replace" ? 2 * updates.size() : f.size();
            if (!state) {
              Work w;
              if (phase.starts_with('q'))
                scan(f, s, q, hash, mask, &w);
              else if (phase == "replace")
                update();
              std::cout << c.name << ',' << m << ',' << phase << ',' << f.size()
                        << ',' << operations << ',' << seed << ',' << hash
                        << ',' << want.count << ',' << w.candidates << ','
                        << w.residuals << ',' << w.certain << ','
                        << w.plane_bytes << ',' << w.summary_bytes << ','
                        << w.blocks << ',' << w.skipped << ',' << w.text_bytes;
              for (auto g : w.groups)
                std::cout << ',' << g;
              std::cout << ',' << s.allocated() << ',' << fixture_bytes << ','
                        << digest << ",1\n";
              return;
            }
            // Independent validation above touches oracle data; rewarm the
            // measured arm.
            scan(f, s, q, hash, mask);
            for (auto _ : *state) {
              if (phase == "build") {
                auto start = Clock::now();
                auto fresh = build(f, m, hash);
                auto end = Clock::now();
                benchmark::DoNotOptimize(fresh.allocated());
                state->SetIterationTime(
                    std::chrono::duration<double>(end - start).count());
              } else {
                auto start = Clock::now();
                if (phase == "replace")
                  update();
                else
                  got = scan(f, s, q, hash, mask);
                auto end = Clock::now();
                benchmark::DoNotOptimize(got);
                benchmark::ClobberMemory();
                state->SetIterationTime(
                    std::chrono::duration<double>(end - start).count());
                if (phase.starts_with('q') && got != want)
                  throw std::runtime_error("Timed result mismatch");
              }
            }
            if (scan(f, s, q, hash, mask) != want || mask != expected)
              throw std::runtime_error("After-timing oracle mismatch");
            if (phase == "replace") {
              auto fresh = build(f, m, hash);
              if (s.words != fresh.words || s.halves != fresh.halves ||
                  s.bytes != fresh.bytes)
                throw std::runtime_error("Replacement final-state mismatch");
            }
            state->counters["operations"] = double(operations);
            state->counters["rows"] = double(f.size());
          };
          if (accounting)
            run(nullptr);
          else
            benchmark::RegisterBenchmark(
                label.c_str(), [run](benchmark::State &s) { run(&s); })
                ->UseManualTime()
                ->Unit(benchmark::kNanosecond);
        }
    if (!registered)
      throw std::runtime_error("Selection matched no comparisons");
    if (!accounting) {
      int ac = int(args.size());
      args.push_back(nullptr);
      benchmark::Initialize(&ac, args.data());
      if (benchmark::ReportUnrecognizedArguments(ac, args.data()))
        return 1;
      benchmark::RunSpecifiedBenchmarks();
      benchmark::Shutdown();
    }
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
