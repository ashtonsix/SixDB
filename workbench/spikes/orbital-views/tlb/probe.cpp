#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <linux/futex.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

using Clock = std::chrono::steady_clock;
struct Control {
    std::atomic<int> ready{0}, stop{0}, tid{0};
};
static void require(bool good, const char* what) {
    if (!good) { std::perror(what); std::exit(2); }
}
static void pin(int cpu) {
    cpu_set_t set; CPU_ZERO(&set); CPU_SET(cpu, &set);
    require(sched_setaffinity(0, sizeof(set), &set) == 0, "affinity");
}
static void touch(char* p, size_t pages, size_t page) {
    for (size_t i = 0; i < pages; ++i) {
        [[maybe_unused]] volatile char byte = p[i * page];
    }
}
static void run_helper(Control* c, int cpu, char* p, size_t pages,
                       size_t page, bool seen, bool sleep) {
    pin(cpu);
    c->tid.store(static_cast<int>(syscall(SYS_gettid)), std::memory_order_relaxed);
    if (seen) touch(p, pages, page);
    c->ready.store(1, std::memory_order_release);
    if (sleep) {
        while (!c->stop.load(std::memory_order_acquire))
            syscall(SYS_futex, &c->stop, FUTEX_WAIT, 0, nullptr, nullptr, 0);
    } else {
        while (!c->stop.load(std::memory_order_acquire))
            std::atomic_signal_fence(std::memory_order_seq_cst);
    }
}
static void ready(Control* c) {
    while (!c->ready.load(std::memory_order_acquire)) std::this_thread::yield();
}
static void stop(Control* c) {
    c->stop.store(1, std::memory_order_release);
    syscall(SYS_futex, &c->stop, FUTEX_WAKE, 1, nullptr, nullptr, 0);
}
static void sleeping(Control* c) {
    std::string path = "/proc/self/task/" + std::to_string(c->tid.load()) + "/stat";
    for (int attempt = 0; attempt != 10000; ++attempt) {
        std::ifstream input(path); std::string line; std::getline(input, line);
        auto end = line.rfind(')');
        if (end != std::string::npos && line[end + 2] == 'S') return;
        std::this_thread::yield();
    }
    require(false, "helper did not block");
}
static std::vector<long long> interrupts() {
    std::ifstream input("/proc/interrupts"); std::string line;
    while (std::getline(input, line)) {
        if (line.find("TLB:") == std::string::npos) continue;
        std::istringstream fields(line.substr(line.find(':') + 1));
        std::vector<long long> counts; long long n;
        while (fields >> n) counts.push_back(n);
        return counts;
    }
    return {};
}
static long long minor_faults() {
    rusage use{}; require(getrusage(RUSAGE_SELF, &use) == 0, "getrusage");
    return use.ru_minflt;
}
struct Case { std::string context, operation; size_t pages; bool populated; };
static void cell(const Case& test, const std::vector<int>& cpus, int iterations) {
    const size_t page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    const size_t bytes = test.pages * page;
    pin(cpus[0]);
    char* p = static_cast<char*>(mmap(nullptr, bytes, PROT_READ | PROT_WRITE,
                                    MAP_SHARED | MAP_ANONYMOUS, -1, 0));
    require(p != MAP_FAILED, "map");
    require(madvise(p, bytes, MADV_NOHUGEPAGE) == 0, "disable huge pages");
    if (test.populated) {
        for (size_t i = 0; i != test.pages; ++i) p[i * page] = 1;
    }
    auto* controls = static_cast<Control*>(mmap(nullptr, page, PROT_READ | PROT_WRITE,
                                          MAP_SHARED | MAP_ANONYMOUS, -1, 0));
    require(controls != MAP_FAILED, "controls");
    for (int i = 0; i != 4; ++i) new (&controls[i]) Control;
    std::vector<std::thread> threads; std::vector<pid_t> children;
    if (test.context == "independent_seen") {
        pid_t child = fork(); require(child >= 0, "fork helper");
        if (!child) {
            run_helper(&controls[0], cpus[1], p, test.pages, page, true, false);
            _exit(0);
        }
        children.push_back(child); ready(&controls[0]);
    } else if (test.context != "local") {
        int n = test.context == "three_remote_unseen" ? 3 : 1;
        bool seen = test.context != "remote_unseen" && test.context != "three_remote_unseen";
        bool sleep = test.context == "sleep_after_seen" || test.context == "switched_after_seen";
        for (int i = 0; i != n; ++i) {
            threads.emplace_back(run_helper, &controls[i], cpus[i + 1], p,
                                 test.pages, page, seen, sleep);
            ready(&controls[i]);
            if (sleep) sleeping(&controls[i]);
        }
        if (test.context == "switched_after_seen") {
            pid_t child = fork(); require(child >= 0, "fork switch helper");
            if (!child) {
                run_helper(&controls[3], cpus[1], p, test.pages, page, false, false);
                _exit(0);
            }
            children.push_back(child); ready(&controls[3]);
        }
    }
    auto operation = [&] {
        auto change = [&](size_t off, size_t length, int protection) {
            require(mprotect(p + off, length, protection) == 0, "protect");
        };
        if (test.operation == "protect_batch") {
            change(0, bytes, PROT_READ); change(0, bytes, PROT_READ | PROT_WRITE);
        } else if (test.operation == "protect_pages") {
            for (size_t i = 0; i != test.pages; ++i) change(i * page, page, PROT_READ);
            for (size_t i = 0; i != test.pages; ++i) change(i * page, page, PROT_READ | PROT_WRITE);
        } else if (test.operation == "protect_noop") {
            change(0, bytes, PROT_READ | PROT_WRITE);
        } else if (test.operation == "replace") {
            require(mmap(p, bytes, PROT_READ | PROT_WRITE,
                         MAP_FIXED | MAP_SHARED | MAP_ANONYMOUS, -1, 0) == p, "replace");
        } else {
            require(munmap(p, bytes) == 0, "unmap");
            require(mmap(p, bytes, PROT_READ | PROT_WRITE,
                         MAP_FIXED | MAP_SHARED | MAP_ANONYMOUS, -1, 0) == p, "remap");
        }
    };
    for (int i = 0; i != 20; ++i) {
        operation();
        if (test.populated) touch(p, test.pages, page);
    }
    auto irq_start = interrupts(); auto faults_start = minor_faults();
    long long operation_ns = 0, touch_ns = 0;
    for (int i = 0; i != iterations; ++i) {
        auto begin = Clock::now(); operation(); auto end = Clock::now();
        if (test.populated) touch(p, test.pages, page);
        auto after = Clock::now();
        operation_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
        touch_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(after - end).count();
    }
    auto faults = minor_faults() - faults_start;
    auto irq_end = interrupts();
    char expected = test.populated && test.operation != "replace" && test.operation != "unmap" ? 1 : 0;
    for (size_t i = 0; i != test.pages; ++i)
        require(p[i * page] == expected, "mapping contents");
    long long total_irqs = 0, remote_irqs = 0;
    if (!irq_start.empty() && irq_start.size() == irq_end.size()) {
        for (size_t i = 0; i != irq_start.size(); ++i) {
            long long delta = irq_end[i] - irq_start[i];
            total_irqs += delta;
            if (static_cast<int>(i) != cpus[0]) remote_irqs += delta;
        }
    } else { total_irqs = -1; remote_irqs = -1; }
    for (int i = 0; i != 4; ++i) stop(&controls[i]);
    for (auto& thread : threads) thread.join();
    for (pid_t child : children) {
        int status; require(waitpid(child, &status, 0) == child && status == 0, "helper exit");
    }
    std::cout << test.context << ',' << test.operation << ',' << test.pages << ','
              << test.populated << ',' << iterations << ',' << cpus[0] << ','
              << page << ',' << static_cast<double>(operation_ns) / iterations << ','
              << static_cast<double>(touch_ns) / iterations << ',' << faults << ','
              << total_irqs << ',' << remote_irqs << '\n' << std::flush;
    require(munmap(p, bytes) == 0, "final unmap");
    require(munmap(controls, page) == 0, "final controls unmap");
}
int main(int argc, char** argv) {
    cpu_set_t allowed; require(sched_getaffinity(0, sizeof(allowed), &allowed) == 0, "allowed CPUs");
    std::vector<int> cpus;
    for (int i = 0; i != CPU_SETSIZE; ++i) if (CPU_ISSET(i, &allowed)) cpus.push_back(i);
    require(cpus.size() >= 4, "probe needs four allowed CPUs");
    std::vector<Case> cases;
    for (std::string context : {"local", "remote_unseen", "remote_seen", "sleep_after_seen",
                               "switched_after_seen", "independent_seen", "three_remote_unseen"})
        for (std::string op : {"protect_noop", "protect_batch", "protect_pages", "replace", "unmap"})
            for (size_t pages : {1u, 64u})
                for (bool populated : {false, true}) cases.push_back({context, op, pages, populated});
    if (argc > 1 && std::string(argv[1]) == "--reverse") std::reverse(cases.begin(), cases.end());
    std::cout << "context,operation,pages,populated,iterations,cpu,page_bytes,operation_ns,touch_ns,minor_faults,tlb_irq_total,tlb_irq_remote\n" << std::flush;
    for (const auto& test : cases) {
        pid_t child = fork(); require(child >= 0, "fork cell");
        if (!child) { cell(test, cpus, 100); _exit(0); }
        int status; require(waitpid(child, &status, 0) == child && status == 0, "cell exit");
    }
}
