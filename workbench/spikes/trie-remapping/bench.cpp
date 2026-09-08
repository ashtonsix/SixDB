#include "model.h"
#include <algorithm>
#include <benchmark/benchmark.h>
#include <chrono>
#include <iostream>
#include <regex>
#include <sched.h>
#include <stdexcept>
#include <unordered_map>
using namespace remap;
using Clock = std::chrono::steady_clock;

U run_points(Index &index, const Trace &t) {
  U sum = 0;
  for (U k : t.queries)
    sum += index.point(k);
  return sum;
}
U run_scan(Index &index, const Trace &t, unsigned n) {
  U sum = 0;
  for (unsigned q = 0; q < 256; ++q)
    sum += index.scan(t.queries[q], n);
  return sum;
}
U run_secondary(Index &index, const Trace &t, const Profile &p) {
  U sum = 0;
  for (unsigned q = 0; q < 2048; ++q) {
    unsigned r = (q * 131 + 17) % unsigned(t.initial.rows.size());
    sum += index.secondary(t.initial.rows[r].id, q % p.references);
  }
  return sum;
}
U secondary_oracle(const Trace &t, const Profile &p) {
  U sum = 0;
  std::array<U, columns_max> v{};
  for (unsigned q = 0; q < 2048; ++q) {
    unsigned r = (q * 131 + 17) % unsigned(t.initial.rows.size());
    for (unsigned c = 0; c < p.columns; ++c)
      v[c] = t.initial.columns[c][r];
    sum += row_digest(t.initial.rows[r], v.data(), p.columns);
  }
  return sum;
}
U oracle(const Trace &t, const Profile &p, unsigned scans = 0) {
  U sum = 0;
  std::array<U, columns_max> values{};
  unsigned qn = scans ? 256 : unsigned(t.queries.size());
  for (unsigned q = 0; q < qn; ++q) {
    U k = t.queries[q];
    auto it = std::lower_bound(t.initial.rows.begin(), t.initial.rows.end(), k,
                               [](Entry e, U x) { return e.key < x; });
    for (unsigned n = 0; n < (scans ? scans : 1) && it != t.initial.rows.end();
         ++n, ++it) {
      if (!scans && it->key != k)
        break;
      unsigned i = unsigned(it - t.initial.rows.begin());
      for (unsigned c = 0; c < p.columns; ++c)
        values[c] = t.initial.columns[c][i];
      sum += scans ? (p.columns ? values[0] : it->key)
                   : row_digest(*it, values.data(), p.columns);
    }
  }
  return sum;
}
int main(int argc, char **argv) {
  bool accounting = false;
  std::string select = ".*";
  unsigned cpu = 0, natural_slack = 25;
  std::vector<char *> args{argv[0]};
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--accounting")
      accounting = true;
    else if (a.starts_with("--select="))
      select = a.substr(9);
    else if (a.starts_with("--natural-slack="))
      natural_slack = unsigned(std::stoul(a.substr(16)));
    else if (a.starts_with("--cpu="))
      cpu = unsigned(std::stoul(a.substr(6)));
    else
      args.push_back(argv[i]);
  }
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(cpu, &cpuset);
  if (sched_setaffinity(0, sizeof(cpuset), &cpuset))
    throw std::runtime_error("affinity failed");
  std::regex filter(select);
  unsigned registered = 0;
  std::unordered_map<std::string, std::shared_ptr<Trace>> traces, after;
  if (accounting)
    std::cout
        << "case,profile,method,phase,population,operations,universe,trace_"
           "digest,columns,secondary_references,stable_ids,payload_bytes,key_"
           "bytes,routing_bytes,reference_bytes,summary_entries,pages,nodes,"
           "payload_moved,key_moved,relabelled,references_repaired,summary_"
           "adjustments,splits,page_relabels,largest_move,redistributions,"
           "correct\n";
  for (auto c : cases())
    for (auto p : profiles())
      for (auto m : methods())
        for (std::string phase : {"points", "secondary", "scan16", "scan256",
                                  "scan256_after", "churn", "rebuild"}) {
          p.natural_slack = natural_slack;
          if (phase == "secondary" && !p.references)
            continue;
          std::string label = c.name + "/" + p.name + "/" + m + "/" + phase;
          if (!std::regex_search(label, filter))
            continue;
          ++registered;
          auto &shared = traces[c.name];
          if (!shared)
            shared = std::make_shared<Trace>(trace(c));
          auto t = shared;
          auto &later = after[c.name];
          if (phase == "scan256_after" && !later)
            later = std::make_shared<Trace>(after_trace(*t));
          auto post = later;
          U expected = phase == "scan256_after" ? oracle(*post, p, 256)
                       : phase == "secondary"   ? secondary_oracle(*t, p)
                       : phase == "points"      ? oracle(*t, p)
                       : phase == "scan16"      ? oracle(*t, p, 16)
                       : phase == "scan256"     ? oracle(*t, p, 256)
                                                : 0;
          unsigned operations = (phase == "points" || phase == "secondary")
                                    ? unsigned(t->queries.size())
                                : (phase == "scan16" || phase == "scan256" ||
                                   phase == "scan256_after")
                                    ? 256
                                : phase == "rebuild" ? c.population
                                                     : c.operations;
          if (phase == "churn") {
            auto baseline =
                make_index("natural", p, t->initial, t->identities, false);
            expected = cycle(*baseline, *t, p, true);
            baseline->validate();
          }
          if (accounting) {
            auto index = make_index(phase == "rebuild" ? "natural" : m, p,
                                    t->initial, t->identities, true);
            index->validate();
            if (phase == "scan256_after") {
              cycle(*index, *t, p, true);
              index->validate();
              index->reset_counters();
            }
            U actual = 0;
            if (phase == "scan256_after")
              actual = run_scan(*index, *post, 256);
            else if (phase == "secondary")
              actual = run_secondary(*index, *t, p);
            else if (phase == "points")
              actual = run_points(*index, *t);
            else if (phase == "scan16")
              actual = run_scan(*index, *t, 16);
            else if (phase == "scan256")
              actual = run_scan(*index, *t, 256);
            else if (phase == "churn")
              actual = cycle(*index, *t, p, true);
            else {
              auto image = index->image();
              index = make_index(m, p, image, t->identities, true);
            }
            if (actual != expected)
              throw std::runtime_error("oracle mismatch: " + label);
            index->validate();
            auto f = index->footprint();
            auto a = index->counters();
            std::cout << c.name << ',' << p.name << ',' << m << ',' << phase
                      << ',' << c.population << ',' << operations << ','
                      << c.universe << ',' << t->digest << ',' << p.columns
                      << ',' << p.references << ',' << p.stable_ids << ','
                      << f.payload << ',' << f.keys << ',' << f.routing << ','
                      << f.references << ',' << f.summary_entries << ','
                      << f.pages << ',' << f.nodes << ',' << a.payload_moved
                      << ',' << a.key_moved << ',' << a.relabelled << ','
                      << a.references_repaired << ',' << a.summary_adjustments
                      << ',' << a.splits << ',' << a.page_relabels << ','
                      << a.largest_move << ',' << a.redistributions << ",1\n";
          } else
            benchmark::RegisterBenchmark(
                label.c_str(),
                [=](benchmark::State &state) {
                  auto steady =
                      phase == "churn"
                          ? nullptr
                          : make_index(phase == "rebuild" ? "natural" : m, p,
                                       t->initial, t->identities, false);
                  if (phase == "scan256_after")
                    cycle(*steady, *t, p, true);
                  for (auto _ : state) {
                    (void)_;
                    auto mutable_index =
                        phase == "churn"
                            ? make_index(m, p, t->initial, t->identities, false)
                            : nullptr;
                    std::unique_ptr<Index> rebuilt;
                    Image image;
                    U actual = 0;
                    auto start = Clock::now();
                    if (phase == "scan256_after")
                      actual = run_scan(*steady, *post, 256);
                    else if (phase == "secondary")
                      actual = run_secondary(*steady, *t, p);
                    else if (phase == "points")
                      actual = run_points(*steady, *t);
                    else if (phase == "scan16")
                      actual = run_scan(*steady, *t, 16);
                    else if (phase == "scan256")
                      actual = run_scan(*steady, *t, 256);
                    else if (phase == "churn")
                      actual = cycle(*mutable_index, *t, p);
                    else {
                      image = steady->image();
                      rebuilt = make_index(m, p, image, t->identities, false);
                      benchmark::DoNotOptimize(rebuilt.get());
                    }
                    auto stop = Clock::now();
                    benchmark::DoNotOptimize(actual);
                    if (actual != expected)
                      throw std::runtime_error("timed oracle: " + label);
                    state.SetIterationTime(
                        std::chrono::duration<double>(stop - start).count());
                  }
                  state.counters["operations"] = operations;
                })
                ->UseManualTime()
                ->Unit(benchmark::kNanosecond);
        }
  if (!registered)
    throw std::runtime_error("empty selection");
  if (accounting)
    return 0;
  int count = int(args.size());
  benchmark::Initialize(&count, args.data());
  if (benchmark::ReportUnrecognizedArguments(count, args.data()))
    return 1;
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
}
