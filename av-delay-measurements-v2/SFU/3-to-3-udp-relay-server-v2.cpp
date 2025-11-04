// 3-to-3-udp-relay-server.cpp
// Build: g++ -O2 -std=c++17 3-to-3-udp-relay-server.cpp -o 3-to-3-udp-relay-server
//
// Usage example:
//   ./3-to-3-udp-relay-server \
//     --p1 10000 --p2 20000 --p3 30000 \
//     --1to2 10.0.0.2:11000 --1to3 10.0.0.3:12000 \
//     --2to1 10.0.0.1:10100 --2to3 10.0.0.3:12001 \
//     --3to1 10.0.0.1:10101 --3to2 10.0.0.2:11001
//
// Meaning:
//  - Packets on --p1 are forwarded to the endpoints given in --1to2 and --1to3.
//  - Packets on --p2 -> --2to1 and --2to3; on --p3 -> --3to1 and --3to2.
//  - Each peer can listen on two distinct UDP ports (one per remote sender).

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <optional>

static void die(const char* msg) {
    std::cerr << msg << ": " << std::strerror(errno) << "\n";
    std::exit(1);
}

static bool parse_int(const std::string& s, int& out) {
    try { size_t i=0; int v = std::stoi(s, &i); if (i!=s.size()) return false; out=v; return true; }
    catch(...) { return false; }
}

static bool parse_ip_port(const std::string& spec, sockaddr_in& out) {
    auto pos = spec.find(':');
    if (pos == std::string::npos) return false;
    std::string ip = spec.substr(0, pos);
    std::string ps = spec.substr(pos+1);
    int port = 0;
    if (!parse_int(ps, port)) return false;

    std::memset(&out, 0, sizeof(out));
    out.sin_family = AF_INET;
    out.sin_port = htons(port);
    return inet_pton(AF_INET, ip.c_str(), &out.sin_addr) == 1;
}

static void print_usage(const char* argv0) {
    std::cerr <<
      "Usage:\n"
      "  " << argv0 << " --p1 <port> --p2 <port> --p3 <port>\n"
      "                 --1to2 <ip:port> --1to3 <ip:port>\n"
      "                 --2to1 <ip:port> --2to3 <ip:port>\n"
      "                 --3to1 <ip:port> --3to2 <ip:port>\n\n"
      "Defaults: --p1 10000  --p2 20000  --p3 30000\n";
}

int main(int argc, char* argv[]) {
    int inport[3] = {10000, 20000, 30000};

    // Matrix of optional destinations: dest[i][j] = endpoint for sender i -> receiver j (i!=j)
    std::optional<sockaddr_in> dest[3][3];

    // Parse args
    for (int i=1; i<argc; ++i) {
        std::string a = argv[i];
        auto need_value = [&](const char* name) {
            if (i+1 >= argc) { std::cerr << "Missing value after " << name << "\n"; print_usage(argv[0]); std::exit(1); }
        };

        if (a == "--p1") { need_value("--p1"); if (!parse_int(argv[++i], inport[0])) { std::cerr<<"Bad --p1\n"; return 1; } }
        else if (a == "--p2") { need_value("--p2"); if (!parse_int(argv[++i], inport[1])) { std::cerr<<"Bad --p2\n"; return 1; } }
        else if (a == "--p3") { need_value("--p3"); if (!parse_int(argv[++i], inport[2])) { std::cerr<<"Bad --p3\n"; return 1; } }

        else if (a == "--1to2") { need_value("--1to2"); sockaddr_in sa{}; if (!parse_ip_port(argv[++i], sa)) { std::cerr<<"Bad --1to2\n"; return 1; } dest[0][1] = sa; }
        else if (a == "--1to3") { need_value("--1to3"); sockaddr_in sa{}; if (!parse_ip_port(argv[++i], sa)) { std::cerr<<"Bad --1to3\n"; return 1; } dest[0][2] = sa; }
        else if (a == "--2to1") { need_value("--2to1"); sockaddr_in sa{}; if (!parse_ip_port(argv[++i], sa)) { std::cerr<<"Bad --2to1\n"; return 1; } dest[1][0] = sa; }
        else if (a == "--2to3") { need_value("--2to3"); sockaddr_in sa{}; if (!parse_ip_port(argv[++i], sa)) { std::cerr<<"Bad --2to3\n"; return 1; } dest[1][2] = sa; }
        else if (a == "--3to1") { need_value("--3to1"); sockaddr_in sa{}; if (!parse_ip_port(argv[++i], sa)) { std::cerr<<"Bad --3to1\n"; return 1; } dest[2][0] = sa; }
        else if (a == "--3to2") { need_value("--3to2"); sockaddr_in sa{}; if (!parse_ip_port(argv[++i], sa)) { std::cerr<<"Bad --3to2\n"; return 1; } dest[2][1] = sa; }
        else {
            std::cerr << "Unknown arg: " << a << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    // Validate: all six off-diagonal routes must be present
    bool ok = true;
    for (int i=0;i<3;++i) for (int j=0;j<3;++j) if (i!=j) ok &= dest[i][j].has_value();
    if (!ok) {
        std::cerr << "Error: provide all six mappings (--1to2, --1to3, --2to1, --2to3, --3to1, --3to2).\n";
        print_usage(argv[0]);
        return 1;
    }

    // Create 3 sockets (one per input)
    int sock[3];
    pollfd pfd[3];

    for (int i=0; i<3; ++i) {
        sock[i] = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (sock[i] < 0) die("socket");

        int yes = 1;
        if (setsockopt(sock[i], SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0)
            die("setsockopt(SO_REUSEADDR)");

        int buf = 1 << 20; // 1 MiB (best-effort)
        setsockopt(sock[i], SOL_SOCKET, SO_RCVBUF, &buf, sizeof(buf));
        setsockopt(sock[i], SOL_SOCKET, SO_SNDBUF, &buf, sizeof(buf));

        sockaddr_in bind_addr{};
        bind_addr.sin_family = AF_INET;
        bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        bind_addr.sin_port = htons(inport[i]);
        if (bind(sock[i], reinterpret_cast<sockaddr*>(&bind_addr), sizeof(bind_addr)) < 0)
            die("bind");

        pfd[i].fd = sock[i];
        pfd[i].events = POLLIN;
        pfd[i].revents = 0;
    }

    auto ep_str = [](const sockaddr_in& sa) {
        char ip[INET_ADDRSTRLEN]; inet_ntop(AF_INET, &sa.sin_addr, ip, sizeof(ip));
        return std::string(ip) + ":" + std::to_string(ntohs(sa.sin_port));
    };

    std::cout << "3-to-3 UDP relay (matrix) ready\n";
    std::cout << "  Inputs: p1=" << inport[0] << "  p2=" << inport[1] << "  p3=" << inport[2] << "\n";
    std::cout << "  1->2 " << ep_str(*dest[0][1]) << "   1->3 " << ep_str(*dest[0][2]) << "\n";
    std::cout << "  2->1 " << ep_str(*dest[1][0]) << "   2->3 " << ep_str(*dest[1][2]) << "\n";
    std::cout << "  3->1 " << ep_str(*dest[2][0]) << "   3->2 " << ep_str(*dest[2][1]) << "\n";

    constexpr size_t MAX_UDP = 65536;
    std::vector<unsigned char> buf(MAX_UDP);

    while (true) {
        int r = ::poll(pfd, 3, -1);
        if (r < 0) {
            if (errno == EINTR) continue;
            die("poll");
        }

        for (int i=0; i<3; ++i) {
            if (!(pfd[i].revents & POLLIN)) continue;
            pfd[i].revents = 0;  // clear for next poll cycle

            sockaddr_in src{};
            socklen_t slen = sizeof(src);
            ssize_t n = ::recvfrom(sock[i], buf.data(), buf.size(), 0,
                                   reinterpret_cast<sockaddr*>(&src), &slen);
            if (n < 0) {
                if (errno == EINTR) continue;
                std::cerr << "recvfrom on input " << (i+1) << " failed: "
                          << std::strerror(errno) << "\n";
                continue;
            }

            // Forward to the two configured outputs for this input row
            for (int j=0; j<3; ++j) {
                if (j == i) continue; // no self-route
                const sockaddr_in& d = *dest[i][j];
                if (::sendto(sock[i], buf.data(), (size_t)n, 0,
                             reinterpret_cast<const sockaddr*>(&d), sizeof(d)) != n) {
                    std::cerr << "sendto " << (i+1) << "->" << (j+1)
                              << " (" << ep_str(d) << ") failed: "
                              << std::strerror(errno) << "\n";
                }
            }
        }
    }

    // (Unreachable)
    for (int i=0; i<3; ++i) close(sock[i]);
    return 0;
}
