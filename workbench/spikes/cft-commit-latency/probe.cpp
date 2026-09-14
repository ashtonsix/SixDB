#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using clock_type = std::chrono::steady_clock;
void require(bool ok, const char* what) { if (!ok) throw std::runtime_error(std::string(what) + ": " + strerror(errno)); }
void exact(int fd, char* data, size_t size, bool send_data) {
    while (size) {
        auto n = send_data ? send(fd, data, size, MSG_NOSIGNAL) : recv(fd, data, size, 0);
        require(n > 0, send_data ? "send" : "recv"); data += n; size -= n;
    }
}
void configure(int fd) {
    int one = 1; require(setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)) == 0, "nodelay");
    timeval timeout{5, 0};
    require(setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0, "recv timeout");
    require(setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0, "send timeout");
}
tcp_info info(int fd) { tcp_info t{}; socklen_t n = sizeof(t); require(getsockopt(fd, IPPROTO_TCP, TCP_INFO, &t, &n) == 0, "tcp_info"); return t; }
int main(int argc, char** argv) try {
    if (argc == 2 && std::string(argv[1]) == "server") {
        int fd = socket(AF_INET, SOCK_STREAM, 0); require(fd >= 0, "socket");
        int one = 1; setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(43001); addr.sin_addr.s_addr = INADDR_ANY;
        require(bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0, "bind"); require(listen(fd, 32) == 0, "listen");
        for (;;) { int peer = accept(fd, nullptr, nullptr); require(peer >= 0, "accept"); std::thread([peer] {
            try { configure(peer); uint32_t wire_size; exact(peer, reinterpret_cast<char*>(&wire_size), 4, false);
                size_t size = ntohl(wire_size); require(size > 0 && size <= 1048576, "message size");
                std::vector<char> data(size); for (;;) { exact(peer, data.data(), size, false); exact(peer, data.data(), size, true); }
            } catch (...) {} close(peer);
        }).detach(); }
    }
    if (argc != 6) throw std::runtime_error("usage: probe server | probe IP BYTES SAMPLES OUTPUT");
    std::string ip = argv[1]; size_t size = std::stoul(argv[2]), count = std::stoul(argv[3]);
    // Last argument is warmup count, excluded from the retained measurements.
    size_t warmup = std::stoul(argv[5]);
    int fd = socket(AF_INET, SOCK_STREAM, 0); require(fd >= 0, "socket"); configure(fd);
    sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(43001); require(inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) == 1, "address");
    auto begin = clock_type::now(); require(connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0, "connect");
    double connect_us = std::chrono::duration<double, std::micro>(clock_type::now() - begin).count();
    uint32_t wire_size = htonl(size); exact(fd, reinterpret_cast<char*>(&wire_size), 4, true);
    std::vector<char> sent(size), received(size); for (size_t i = 0; i < size; ++i) sent[i] = static_cast<char>((i * 71 + 19) % 251);
    std::vector<uint64_t> samples; samples.reserve(count);
    tcp_info before{};
    for (size_t i = 0; i < warmup + count; ++i) {
        if (i == warmup) before = info(fd);
        auto start = clock_type::now(); exact(fd, sent.data(), size, true); exact(fd, received.data(), size, false);
        auto end = clock_type::now(); require(sent == received, "echo integrity");
        if (i >= warmup) samples.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
    }
    auto after = info(fd); close(fd);
    std::ofstream out(argv[4]); require(bool(out), "open samples"); out << "rtt_ns\n"; for (auto n : samples) out << n << '\n';
    std::cout << "{\"connect_us\":" << connect_us << ",\"samples\":" << count << ",\"warmup\":" << warmup
              << ",\"snd_mss\":" << after.tcpi_snd_mss << ",\"rcv_mss\":" << after.tcpi_rcv_mss
              << ",\"pmtu\":" << after.tcpi_pmtu << ",\"rtt_us\":" << after.tcpi_rtt
              << ",\"rttvar_us\":" << after.tcpi_rttvar << ",\"total_retrans\":" << after.tcpi_total_retrans
              << ",\"measured_retrans\":" << after.tcpi_total_retrans - before.tcpi_total_retrans << "}\n";
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
