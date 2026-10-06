#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace loom_spike {

class Options {
  public:
    Options(int argc, char** argv);
    std::string get(const std::string& name, const std::string& fallback = "") const;
    std::uint64_t number(const std::string& name, std::uint64_t fallback) const;
    bool has(const std::string& name) const;
    std::vector<int> cpus() const;

  private:
    std::map<std::string, std::string> values_;
};

std::uint64_t now_ns();
std::uint64_t thread_cpu_ns();
void pin(int cpu);
inline void relax() {
#if defined(__x86_64__)
    __asm__ volatile("pause" ::: "memory");
#elif defined(__aarch64__)
    __asm__ volatile("yield" ::: "memory");
#endif
}

struct Record {
    std::string family, variant, unit;
    std::uint64_t repeat = 0, elapsed_ns = 0, items = 0;
    bool verified = false;
    std::map<std::string, std::string> parameters;
    std::map<std::string, std::uint64_t> counters;
};
void emit(const Record& record);

} // namespace loom_spike
