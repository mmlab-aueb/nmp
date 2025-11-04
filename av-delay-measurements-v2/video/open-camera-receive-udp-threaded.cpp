// ---------------------------------------------------------------
// g++ -std=c++11 -pthread -o open-camera-receive-udp-threaded-new \
//         open-camera-receive-udp-threaded-new.cpp $(pkg-config --cflags --libs opencv4)
// ---------------------------------------------------------------

#include <opencv2/opencv.hpp>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>

#include <atomic>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <vector>
#include <cstdlib>   // std::strtol

#define DEFAULT_LOCAL_PORT 5000
#define BUF_SIZE   65536      // max UDP payload

/* ────────── shared packet buffer ───────────────────────────── */
struct PacketBuf {
    std::vector<uchar> data;
    bool               ready = false;
    std::mutex         mtx;
    std::condition_variable cv;
};

/* bundle args for receiver thread */
struct RecvArgs {
    PacketBuf* pktbuf;
    int        port;
};

/* global stop flag so both threads can see it */
std::atomic<bool> running{true};

/* small helper to parse/validate port */
static int parse_port(const char* s) {
    char* end = nullptr;
    long v = std::strtol(s, &end, 10);
    if (end == s || *end != '\0' || v < 1 || v > 65535) return -1;
    return static_cast<int>(v);
}

/* ------------------------------------------------------------- */
/*  network-receiver thread – pthread entry point                 */
void* recvThread(void* arg)
{
    auto* rargs  = static_cast<RecvArgs*>(arg);
    PacketBuf* pktbuf = rargs->pktbuf;
    const int local_port = rargs->port;
    delete rargs;  // no longer needed

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("[NET] socket"); running = false; return nullptr; }

    sockaddr_in addr{};  addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(local_port);
    if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("[NET] bind"); running = false; close(sock); return nullptr;
    }
    std::cout << "[NET] listening on port " << local_port << '\n';

    std::vector<uchar> buf(BUF_SIZE);

    while (running.load()) {
        sockaddr_in sender{}; socklen_t slen = sizeof(sender);
        ssize_t n = recvfrom(sock, buf.data(), buf.size(), 0,
                             reinterpret_cast<sockaddr*>(&sender), &slen);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("[NET] recvfrom"); continue;
        }

        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &sender.sin_addr, ip, sizeof(ip));
        std::cout << "[NET] got " << n << " bytes from "
                  << ip << ':' << ntohs(sender.sin_port) << '\n';

        {
            std::lock_guard<std::mutex> lk(pktbuf->mtx);
            pktbuf->data.assign(buf.begin(), buf.begin() + n);
            pktbuf->ready = true;
        }
        pktbuf->cv.notify_one();
    }
    close(sock);
    return nullptr;
}

/* ------------------------------------------------------------- */
/*  display thread – pthread entry point                          */
void* displayThread(void* arg)
{
    auto* pktbuf = static_cast<PacketBuf*>(arg);
    cv::namedWindow("Receiver", cv::WINDOW_AUTOSIZE);

    std::unique_lock<std::mutex> lk(pktbuf->mtx);
    while (running.load()) {
        pktbuf->cv.wait(lk, [&]{ return pktbuf->ready || !running.load(); });
        if (!running.load()) break;

        cv::Mat img = cv::imdecode(pktbuf->data, cv::IMREAD_COLOR);
        pktbuf->ready = false;
        lk.unlock();

        if (img.empty()) {
            std::cerr << "[GUI] imdecode failed – dropped frame\n";
        } else {
            cv::imshow("Receiver", img);
            std::cout << "[GUI] displayed " << img.cols << 'x'
                      << img.rows << " frame\n";
        }
        if (cv::waitKey(1) == 27) {           // ESC quits
            running = false;
            pktbuf->cv.notify_one();           // wake receiver if waiting
        }
        lk.lock();
    }
    return nullptr;
}

/* ------------------------------------------------------------- */
/*  main                                                          */
int main(int argc, char** argv)
{
    int port = DEFAULT_LOCAL_PORT;
    if (argc >= 2) {
        int p = parse_port(argv[1]);
        if (p < 0) {
            std::cerr << "Usage: " << argv[0] << " [LOCAL_PORT]\n"
                      << "Example: " << argv[0] << " 5000\n";
            return 1;
        }
        port = p;
    }
    std::cout << "[MAIN] using local UDP port " << port << '\n';

    PacketBuf pktbuf;

    // allocate args for receiver thread on heap; freed inside the thread
    auto* rargs = new RecvArgs{ &pktbuf, port };

    pthread_t netTid{}, guiTid{};
    if (pthread_create(&netTid, nullptr, recvThread, rargs) != 0)
        { perror("pthread_create (net)"); return 1; }
    if (pthread_create(&guiTid, nullptr, displayThread, &pktbuf) != 0)
        { perror("pthread_create (gui)"); running=false; pthread_join(netTid, nullptr); return 1; }

    pthread_join(netTid, nullptr);
    pthread_join(guiTid, nullptr);
    return 0;
}
