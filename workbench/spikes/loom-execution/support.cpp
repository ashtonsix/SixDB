#include "support.h"

#include <charconv>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <sched.h>
#include <stdexcept>
#include <time.h>

namespace loom_spike {
namespace {
std::uint64_t parse_number(const std::string& value) {
    std::uint64_t result = 0;
    auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (error != std::errc{} || end != value.data() + value.size())
        throw std::invalid_argument("invalid unsigned integer: " + value);
    return result;
}
std::string quoted(const std::string& value) {
    std::string result = "\"";
    const char* hex = "0123456789abcdef";
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') { result += '\\'; result += c; }
        else if (c < 32) {
            result += "\\u00"; result += hex[c >> 4]; result += hex[c & 15];
        } else result += c;
    }
    return result + "\"";
}
std::uint64_t clock_ns(clockid_t clock) {
    timespec value{};
    if (clock_gettime(clock, &value)) throw std::runtime_error(std::strerror(errno));
    return std::uint64_t(value.tv_sec) * 1000000000ULL + std::uint64_t(value.tv_nsec);
}
} // namespace

Options::Options(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string argument = argv[i];
        if (!argument.starts_with("--"))
            throw std::invalid_argument("expected --option: " + argument);
        argument.erase(0, 2);
        auto equal = argument.find('=');
        if (equal != std::string::npos)
            values_[argument.substr(0, equal)] = argument.substr(equal + 1);
        else if (i + 1 < argc && !std::string(argv[i + 1]).starts_with("--"))
            values_[argument] = argv[++i];
        else values_[argument] = "1";
    }
}
std::string Options::get(const std::string& name, const std::string& fallback) const {
    auto found = values_.find(name);
    return found == values_.end() ? fallback : found->second;
}
std::uint64_t Options::number(const std::string& name, std::uint64_t fallback) const {
    return has(name) ? parse_number(get(name)) : fallback;
}
bool Options::has(const std::string& name) const { return values_.contains(name); }
std::vector<int> Options::cpus() const {
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    if (sched_getaffinity(0, sizeof(allowed), &allowed))
        throw std::runtime_error(std::strerror(errno));
    std::vector<int> result;
    if (!has("cpus")) {
        for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu)
            if (CPU_ISSET(cpu, &allowed)) result.push_back(cpu);
    } else {
        auto text = get("cpus");
        std::size_t start = 0;
        while (start < text.size()) {
            auto end = text.find(',', start);
            auto cpu = parse_number(text.substr(start, end - start));
            if (cpu >= CPU_SETSIZE || !CPU_ISSET(static_cast<int>(cpu), &allowed))
                throw std::invalid_argument("requested CPU is outside allowed affinity");
            result.push_back(static_cast<int>(cpu));
            if (end == std::string::npos) break;
            start = end + 1;
        }
    }
    if (result.empty()) throw std::invalid_argument("empty CPU list");
    return result;
}
std::uint64_t now_ns() { return clock_ns(CLOCK_MONOTONIC_RAW); }
std::uint64_t thread_cpu_ns() { return clock_ns(CLOCK_THREAD_CPUTIME_ID); }
void pin(int cpu) {
    if (cpu < 0 || cpu >= CPU_SETSIZE) throw std::invalid_argument("invalid CPU");
    cpu_set_t set;
    CPU_ZERO(&set); CPU_SET(cpu, &set);
    if (sched_setaffinity(0, sizeof(set), &set)) throw std::runtime_error(std::strerror(errno));
}
void emit(const Record& record) {
    std::cout << "{\"family\":" << quoted(record.family)
              << ",\"variant\":" << quoted(record.variant)
              << ",\"unit\":" << quoted(record.unit)
              << ",\"repeat\":" << record.repeat
              << ",\"elapsed_ns\":" << record.elapsed_ns
              << ",\"items\":" << record.items
              << ",\"verified\":" << (record.verified ? "true" : "false")
              << ",\"parameters\":{";
    bool first = true;
    for (const auto& [name, value] : record.parameters) {
        if (!first) std::cout << ',';
        first = false; std::cout << quoted(name) << ':' << quoted(value);
    }
    std::cout << "},\"counters\":{"; first = true;
    for (const auto& [name, value] : record.counters) {
        if (!first) std::cout << ',';
        first = false; std::cout << quoted(name) << ':' << value;
    }
    std::cout << "}}\n";
}
} // namespace loom_spike
