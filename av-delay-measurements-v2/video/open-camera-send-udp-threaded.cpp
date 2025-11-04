// g++ -std=c++11 -pthread -o open-camera-send-udp-threaded open-camera-send-udp-threaded.cpp \
//     $(pkg-config --cflags --libs opencv4)

#include <opencv2/opencv.hpp>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <string>
#include <cstdint>
#include <cstdlib>

#define PREVIEW_TITLE "Sender – Local Preview"
#define RESIZE_W 420
#define RESIZE_H 280

struct FrameBuf {
    std::vector<uchar> data;          // JPEG bytes
    std::mutex mtx;
    std::condition_variable cv;
    bool ready = false;
} framebuf;

/* ---------- capture / preview (producer) ---------- */
void captureLoop(std::atomic<bool>& running)
{
    cv::VideoCapture cap(0);
    if (!cap.isOpened()) { std::cerr << "[CAP] Camera open failed!\n"; running=false; return; }

    cv::Mat frame, resized;
    std::vector<uchar> jpeg;
    cv::namedWindow(PREVIEW_TITLE, cv::WINDOW_AUTOSIZE);

    while (running.load()) {
        cap >> frame;
        if (frame.empty()) continue;

        cv::imshow(PREVIEW_TITLE, frame);
        // ESC quits
        int key = cv::waitKey(1);
        if (key == 27) { running=false; break; }

        cv::resize(frame, resized, {RESIZE_W, RESIZE_H});
        cv::imencode(".jpg", resized, jpeg);

        if (jpeg.size() > 65000) {
            std::cerr << "[CAP] frame " << jpeg.size()
                      << " bytes -- skipped (would exceed single UDP pkt)\n";
            continue;
        }

        {   std::lock_guard<std::mutex> lk(framebuf.mtx);
            framebuf.data.swap(jpeg);          // move newest JPEG in
            framebuf.ready = true;
            std::cout << "[CAP] queued " << framebuf.data.size() << " bytes\n";
        }
        framebuf.cv.notify_one();
    }

    cv::destroyWindow(PREVIEW_TITLE);
}

/* ---------- UDP sender---------- */
void senderLoop(std::atomic<bool>& running, const std::string& destIp, uint16_t destPort)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("[SND] socket"); running=false; return; }

    sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_port = htons(destPort);
    if (inet_pton(AF_INET, destIp.c_str(), &dst.sin_addr) != 1) {
        std::cerr << "[SND] Invalid IPv4 address: " << destIp << "\n";
        close(sock);
        running = false;
        return;
    }

    std::unique_lock<std::mutex> lk(framebuf.mtx);
    while (running.load()) {
        // Wake either when a frame is ready OR when we're shutting down
        framebuf.cv.wait(lk, [&]{ return framebuf.ready || !running.load(); });
        if (!running.load() && !framebuf.ready) break;

        // Move the buffer out while holding the lock, then send without the lock
        std::vector<uchar> buf;
        buf.swap(framebuf.data);
        framebuf.ready = false;
        lk.unlock();

        ssize_t sent = sendto(sock, buf.data(), buf.size(), 0,
                              (sockaddr*)&dst, sizeof(dst));
        if (sent < 0) {
            perror("[SND] sendto");
        } else {
            std::cout << "[SND] sent " << sent << " bytes to "
                      << destIp << ":" << destPort << "\n";
        }

        lk.lock();
    }
    close(sock);
}

/* ---------- usage helper ---------- */
static void printUsage(const char* prog)
{
    std::cerr << "Usage: " << prog << " <dest_ipv4> <dest_port>\n"
              << "Example:\n  " << prog << " 127.0.0.1 8888\n";
}

/* ---------- main ---------- */
int main(int argc, char* argv[])
{
    if (argc != 3) {
        printUsage(argv[0]);
        return 1;
    }

    std::string destIp = argv[1];

    // Parse and validate port
    char* endp = nullptr;
    long portLong = std::strtol(argv[2], &endp, 10);
    if (endp == argv[2] || *endp != '\0' || portLong < 1 || portLong > 65535) {
        std::cerr << "[MAIN] Invalid port: " << argv[2] << "\n";
        printUsage(argv[0]);
        return 1;
    }
    uint16_t destPort = static_cast<uint16_t>(portLong);

    // Quick IP sanity check (won't bind; just parse)
    in_addr tmp{};
    if (inet_pton(AF_INET, destIp.c_str(), &tmp) != 1) {
        std::cerr << "[MAIN] Invalid IPv4 address: " << destIp << "\n";
        printUsage(argv[0]);
        return 1;
    }

    std::cout << "[MAIN] Sending to " << destIp << ":" << destPort << "\n";

    std::atomic<bool> running(true);

    std::thread capThread(captureLoop, std::ref(running));
    std::thread sndThread(senderLoop, std::ref(running), destIp, destPort);

    capThread.join();

    // ensure sender exits if capture loop quit
    running = false;
    framebuf.cv.notify_one();  // wake sender if it’s waiting

    sndThread.join();
    return 0;
}
