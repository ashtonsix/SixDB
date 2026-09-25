// Small UDP exchanges with kernel software TX/RX stamps and local clock brackets.
#include <arpa/inet.h>
#include <linux/net_tstamp.h>
#include <linux/errqueue.h>
#include <linux/sockios.h>
#include <net/if.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

int64_t now(clockid_t c) {
    timespec t{};
    if (clock_gettime(c, &t)) throw std::runtime_error("clock_gettime");
    return int64_t(t.tv_sec) * 1000000000 + t.tv_nsec;
}
struct Packet {
    uint32_t magic = 0x53495836, src = 0, dst = 0, pass = 0, seq = 0, reply = 0;
    std::array<uint64_t, 5> padding{};
};
static_assert(sizeof(Packet) == 64);
struct Event {
    Packet p;
    bool tx;
    int64_t app, sw, hw, a, wall, b;
};
std::vector<Event> events;

void record(Packet p, bool tx, int64_t app, msghdr& msg) {
    if (msg.msg_flags & (MSG_CTRUNC | MSG_TRUNC)) throw std::runtime_error("truncated timestamp metadata");
    Event e{p, tx, app, 0, 0, 0, 0, 0};
    e.a = now(CLOCK_MONOTONIC_RAW);
    e.wall = now(CLOCK_REALTIME);
    e.b = now(CLOCK_MONOTONIC_RAW);
    bool tx_origin = false;
    for (auto* c = CMSG_FIRSTHDR(&msg); c; c = CMSG_NXTHDR(&msg, c)) {
        if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SO_TIMESTAMPING) {
            timespec stamps[3];
            if (c->cmsg_len < CMSG_LEN(sizeof(stamps))) throw std::runtime_error("short timestamp metadata");
            std::memcpy(stamps, CMSG_DATA(c), sizeof(stamps));
            e.sw = int64_t(stamps[0].tv_sec) * 1000000000 + stamps[0].tv_nsec;
            e.hw = int64_t(stamps[2].tv_sec) * 1000000000 + stamps[2].tv_nsec;
        }
        if (c->cmsg_level == SOL_IP && c->cmsg_type == IP_RECVERR) {
            sock_extended_err error{};
            if (c->cmsg_len < CMSG_LEN(sizeof(error))) throw std::runtime_error("short error metadata");
            std::memcpy(&error,CMSG_DATA(c),sizeof(error));
            tx_origin = error.ee_errno == ENOMSG && error.ee_origin == SO_EE_ORIGIN_TIMESTAMPING &&
                        error.ee_info == SCM_TSTAMP_SND;
        }
    }
    if (tx && !tx_origin) throw std::runtime_error("unexpected TX timestamp origin/type");
    if (!e.sw) throw std::runtime_error("missing software timestamp");
    events.push_back(e);
}

int make_socket(int port) {
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) throw std::runtime_error("socket");
    int flags = SOF_TIMESTAMPING_TX_SOFTWARE | SOF_TIMESTAMPING_RX_SOFTWARE |
                SOF_TIMESTAMPING_SOFTWARE | SOF_TIMESTAMPING_OPT_TSONLY |
                SOF_TIMESTAMPING_RX_HARDWARE | SOF_TIMESTAMPING_RAW_HARDWARE;
    if (auto* device = std::getenv("ONEWAY_DEVICE")) {
        hwtstamp_config config{}; config.rx_filter = HWTSTAMP_FILTER_ALL;
        ifreq request{};
        std::strncpy(request.ifr_name, device, IFNAMSIZ - 1);
        request.ifr_data = reinterpret_cast<char*>(&config);
        // Unsupported on older Nitro. Software stamps remain the common boundary.
        if (ioctl(s, SIOCSHWTSTAMP, &request))
            std::cerr << "hardware RX unavailable: " << std::strerror(errno) << '\n';
    }
    if (setsockopt(s, SOL_SOCKET, SO_TIMESTAMPING, &flags, sizeof(flags)))
        throw std::runtime_error("SO_TIMESTAMPING");
    int size = 1 << 20;
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
    sockaddr_in a{};
    a.sin_family = AF_INET; a.sin_port = htons(port); a.sin_addr.s_addr = INADDR_ANY;
    if (bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a))) throw std::runtime_error("bind");
    return s;
}

void transmit(int s, Packet p, const sockaddr_in& target) {
    auto app = now(CLOCK_MONOTONIC_RAW);
    if (sendto(s, &p, sizeof(p), 0, reinterpret_cast<const sockaddr*>(&target), sizeof(target)) != sizeof(p))
        throw std::runtime_error("sendto");
    // One outstanding TX per socket, so the next TX stamp belongs to this packet.
    alignas(cmsghdr) char control[512];
    msghdr msg{}; msg.msg_control = control; msg.msg_controllen = sizeof(control);
    for (int attempt = 0; ; ++attempt) {
        if (recvmsg(s, &msg, MSG_ERRQUEUE | MSG_DONTWAIT) >= 0) break;
        if (errno != EAGAIN || attempt == 1000) throw std::runtime_error("TX stamp timeout");
        std::this_thread::sleep_for(std::chrono::microseconds(10));
    }
    record(p, true, app, msg);
}

bool receive(int s, Packet& p, sockaddr_in& peer, int timeout_ms) {
    pollfd fd{s, POLLIN, 0};
    if (poll(&fd, 1, timeout_ms) <= 0) return false;
    alignas(cmsghdr) char control[512];
    iovec io{&p, sizeof(p)};
    msghdr msg{};
    msg.msg_name = &peer; msg.msg_namelen = sizeof(peer);
    msg.msg_iov = &io; msg.msg_iovlen = 1;
    msg.msg_control = control; msg.msg_controllen = sizeof(control);
    auto count = recvmsg(s, &msg, 0);
    auto app = now(CLOCK_MONOTONIC_RAW);
    if (count != sizeof(p) || p.magic != 0x53495836 || (msg.msg_flags & (MSG_CTRUNC | MSG_TRUNC)))
        throw std::runtime_error("invalid packet/control");
    record(p, false, app, msg);
    return true;
}

void save(const std::string& path) {
    std::ofstream f(path);
    f << "src,dst,pass,seq,reply,tx,app_raw,sw_real,hw,a,wall,b\n";
    for (auto& e : events)
        f << e.p.src << ',' << e.p.dst << ',' << e.p.pass << ',' << e.p.seq << ','
          << e.p.reply << ',' << e.tx << ',' << e.app << ',' << e.sw << ',' << e.hw << ','
          << e.a << ',' << e.wall << ',' << e.b << '\n';
    if (!f) throw std::runtime_error("write events");
}

int main(int argc, char** argv) try {
    if (argc != 9) throw std::runtime_error("probe IP SRC DST PASS COUNT PERIOD_US OUTPUT ROLE(client/server)");
    std::string role = argv[8];
    int count = std::stoi(argv[5]);
    if (count < 1 || count > 2000) throw std::runtime_error("count cap 2000");
    int period = std::stoi(argv[6]);
    if (period < 2000) throw std::runtime_error("minimum period 2000us");
    Packet p; p.src = std::stoul(argv[2]); p.dst = std::stoul(argv[3]); p.pass = std::stoul(argv[4]);
    int local_port = role == "server" ? 43401 : 0;
    int peer_port = 43401;
    if (auto* value = std::getenv("ONEWAY_LOCAL_PORT")) local_port = std::stoi(value);
    if (auto* value = std::getenv("ONEWAY_PEER_PORT")) peer_port = std::stoi(value);
    if (local_port < 0 || local_port > 65535 || peer_port < 1 || peer_port > 65535)
        throw std::runtime_error("invalid port");
    int s = make_socket(local_port);
    events.reserve(count * 2 + 64);
    sockaddr_in target{}; target.sin_family = AF_INET; target.sin_port = htons(peer_port);
    if (inet_pton(AF_INET, argv[1], &target.sin_addr) != 1) throw std::runtime_error("address");
    int received = 0;
    if (role == "client") {
        auto next = std::chrono::steady_clock::now();
        for (int i = 0; i < count; ++i) {
            std::this_thread::sleep_until(next);
            p.seq = i;
            transmit(s, p, target);
            Packet reply; sockaddr_in peer{};
            auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
            while (std::chrono::steady_clock::now() < end && receive(s, reply, peer, 100)) {
                if (reply.src == p.src && reply.dst == p.dst && reply.pass == p.pass &&
                    reply.seq == p.seq && reply.reply == 1 && peer.sin_addr.s_addr == target.sin_addr.s_addr &&
                    peer.sin_port == target.sin_port) { ++received; break; }
                throw std::runtime_error("unexpected reply identity");
            }
            // Do not catch up in a burst after a pause/lost packet.
            next = std::max(next + std::chrono::microseconds(period), std::chrono::steady_clock::now());
        }
    } else if (role == "server") {
        auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5) + std::chrono::microseconds(int64_t(count) * period);
        while (received < count && std::chrono::steady_clock::now() < end) {
            Packet request; sockaddr_in peer{};
            if (!receive(s, request, peer, 100)) continue;
            if (request.src != p.src || request.dst != p.dst || request.pass != p.pass || request.reply || request.seq >= unsigned(count) ||
                peer.sin_addr.s_addr != target.sin_addr.s_addr)
                throw std::runtime_error("unexpected request identity");
            request.reply = 1;
            transmit(s, request, peer);
            ++received;
        }
    } else throw std::runtime_error("role");
    save(argv[7]);
    sockaddr_in local{}; socklen_t address_size=sizeof(local);
    if (getsockname(s,reinterpret_cast<sockaddr*>(&local),&address_size)) throw std::runtime_error("getsockname");
    std::cout << "{\"requested\":" << count << ",\"received\":" << received << ",\"events\":" << events.size()
              << ",\"local_port\":" << ntohs(local.sin_port) << "}\n";
    close(s);
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
