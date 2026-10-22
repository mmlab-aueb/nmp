// udp_p2p_audio_client_threaded.cpp – C++17 rewrite of the original C version
// Build (Linux):  g++ -std=c++17 -pthread udp_p2p_audio_client_threaded.cpp -o udp_p2p_audio_client_threaded_cpp -lasound
// ─────────────────────────────────────────────────────────────────────────────
// * Replaces raw pthreads with std::thread / std::mutex / std::condition_variable
// * Uses std::array for fixed‑size ring‑buffers
// * Keeps the two‑socket UDP hole‑punch logic intact but wrapped in helper funcs
#include <iostream>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <alsa/asoundlib.h>

#define SAMPLE_RATE 44100
#define CHANNELS 1
#define FORMAT SND_PCM_FORMAT_S16_LE
#define FRAME_SIZE 2
#define FRAMES_PER_CHUNK 128
#define CHUNK_SIZE (FRAMES_PER_CHUNK * FRAME_SIZE)
#define PERIOD_SIZE FRAMES_PER_CHUNK
#define PERIODS 2
#define BUFFER_CAPACITY 64

snd_pcm_t *capture_handle;
snd_pcm_t *playback_handle;

char ring_buffer[BUFFER_CAPACITY][CHUNK_SIZE];
int write_index = 0, read_index = 0, buffer_count = 0;
pthread_mutex_t buffer_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t buffer_not_empty = PTHREAD_COND_INITIALIZER;

char ring_buffer1[BUFFER_CAPACITY][CHUNK_SIZE];
int write_index1 = 0, read_index1 = 0, buffer_count1 = 0;
pthread_mutex_t buffer_mutex1 = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t buffer_not_empty1 = PTHREAD_COND_INITIALIZER;

#define SERVER_PORT 10000
#define MAX_MSG_LEN 256

void die(const char *msg) {
    perror(msg);
    exit(1);
}

void *receiver_thread_func(void *arg) {
    int sock = ((int*)arg)[0];
    struct sockaddr_in *peer_port = (struct sockaddr_in *)((int*)arg + 1);
    int packet_count = 0;
    while (1) {
        char temp_buffer[CHUNK_SIZE];
        ssize_t bytes_received = recvfrom(sock, temp_buffer, CHUNK_SIZE, 0, NULL, NULL);
        pthread_mutex_lock(&buffer_mutex1);
        if (buffer_count1 < BUFFER_CAPACITY) {
            memcpy(ring_buffer1[write_index1], temp_buffer, CHUNK_SIZE);
            write_index1 = (write_index1 + 1) % BUFFER_CAPACITY;
            buffer_count1++;
            pthread_cond_signal(&buffer_not_empty1);
            std::cout << "Audio Packet #" << ++packet_count << " | Received: " << bytes_received << " bytes\n";
        }
        pthread_mutex_unlock(&buffer_mutex1);
    }
    return nullptr;
}

void *play_thread_func(void *arg) {
    int packet_count = 0;
    while (1) {
        pthread_mutex_lock(&buffer_mutex1);
        while (buffer_count1 == 0)
            pthread_cond_wait(&buffer_not_empty1, &buffer_mutex1);

        char *data = ring_buffer1[read_index1];
        read_index1 = (read_index1 + 1) % BUFFER_CAPACITY;
        buffer_count1--;
        pthread_mutex_unlock(&buffer_mutex1);

        snd_pcm_sframes_t frames = snd_pcm_writei(playback_handle, data, FRAMES_PER_CHUNK);
        if (frames < 0) snd_pcm_recover(playback_handle, frames, 0);
        std::cout << "Audio Packet #" << ++packet_count << " | Played: " << frames << " frames\n";
    }
    return nullptr;
}

void *capture_thread_func(void *arg) {
    while (1) {
        char temp_buffer[CHUNK_SIZE];
        snd_pcm_sframes_t frames = snd_pcm_readi(capture_handle, temp_buffer, FRAMES_PER_CHUNK);
        snd_pcm_sframes_t delay;
        snd_pcm_delay(capture_handle, &delay);
        std::cout << "ALSA delay: " << delay << " frames (" << (float)delay * 1000 / SAMPLE_RATE << " ms)\n";
        pthread_mutex_lock(&buffer_mutex);
        if (buffer_count < BUFFER_CAPACITY) {
            memcpy(ring_buffer[write_index], temp_buffer, CHUNK_SIZE);
            write_index = (write_index + 1) % BUFFER_CAPACITY;
            buffer_count++;
            pthread_cond_signal(&buffer_not_empty);
        }
        pthread_mutex_unlock(&buffer_mutex);
    }
    return nullptr;
}

void *sender_thread_func(void *arg) {
    int sock = ((int*)arg)[0];
    struct sockaddr_in *peer_port = (struct sockaddr_in *)((int*)arg + 1);
    int packet_count = 0;
    while (1) {
        pthread_mutex_lock(&buffer_mutex);
        while (buffer_count == 0)
            pthread_cond_wait(&buffer_not_empty, &buffer_mutex);

        char *data = ring_buffer[read_index];
        read_index = (read_index + 1) % BUFFER_CAPACITY;
        buffer_count--;
        pthread_mutex_unlock(&buffer_mutex);

        ssize_t bytes_sent = sendto(sock, data, CHUNK_SIZE, 0, (struct sockaddr *)peer_port, sizeof(*peer_port));
        std::cout << "Packet #" << ++packet_count << " | Sent: " << bytes_sent << " bytes";
    }
    return nullptr;
}

// ---------------------------------------------------------------
// NAT hole‑punch helper (unchanged logic, just C++)
void send_punch_packets(int sock1, int sock2,
                        struct sockaddr_in *peer1, struct sockaddr_in *peer2) {
    std::cout << "Inside send_punch_packet" << std::endl;
    struct sockaddr_in loc1{}, loc2{};
    socklen_t l1 = sizeof(loc1), l2 = sizeof(loc2);
    getsockname(sock1, (sockaddr*)&loc1, &l1);
    getsockname(sock2, (sockaddr*)&loc2, &l2);

    std::cout << "[Mapping] Internal port " << ntohs(loc1.sin_port)
              << " mapped to external socket " << sock1 << std::endl;
    std::cout << "[Mapping] Internal port " << ntohs(loc2.sin_port)
              << " mapped to external socket " << sock2 << std::endl;

    for (int i = 0; i < 10; ++i) {
        sendto(sock1, "PUNCH", 5, 0, (sockaddr*)peer1, sizeof(*peer1));
        std::cout << "[Punch:sock1] Sent 5 bytes from " << ntohs(loc1.sin_port)
                  << " to " << inet_ntoa(peer1->sin_addr) << ':' << ntohs(peer1->sin_port) << std::endl;
        sendto(sock2, "PUNCH", 5, 0, (sockaddr*)peer2, sizeof(*peer2));
        std::cout << "[Punch:sock2] Sent 5 bytes from " << ntohs(loc2.sin_port)
                  << " to " << inet_ntoa(peer2->sin_addr) << ':' << ntohs(peer2->sin_port) << std::endl;
        usleep(100000); // 100 ms
    }
}

int main(int argc, char *argv[]) {
    if (argc != 5) {
        std::cerr << "Usage: " << argv[0] << " <server_ip> <my_role: client1|client2> <local_port1> <local_port2>\n";
        exit(1);
    }

    const char *server_ip = argv[1];
    const char *role = argv[2];
    int local_port1 = atoi(argv[3]);
    int local_port2 = atoi(argv[4]);

    int s1 = socket(AF_INET, SOCK_DGRAM, 0);
    int s2 = socket(AF_INET, SOCK_DGRAM, 0);
    if (s1 < 0 || s2 < 0) die("socket");

    struct sockaddr_in a1{};
    a1.sin_family = AF_INET;
    a1.sin_port   = htons(local_port1);
    a1.sin_addr.s_addr = INADDR_ANY;
    struct sockaddr_in a2{};
    a2.sin_family = AF_INET;
    a2.sin_port   = htons(local_port2);
    a2.sin_addr.s_addr = INADDR_ANY;
    bind(s1, (struct sockaddr*)&a1, sizeof(a1));
    bind(s2, (struct sockaddr*)&a2, sizeof(a2));

    struct sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(SERVER_PORT);
    inet_pton(AF_INET, server_ip, &server_addr.sin_addr);
    
    char msg[64];
    snprintf(msg, sizeof(msg), "%s 0", role);
    sendto(s1, msg, strlen(msg), 0, (struct sockaddr*)&server_addr, sizeof(server_addr));
    std::cout << "Registered " << role << " from port " << local_port1 << std::endl;
    sleep(1);
    snprintf(msg, sizeof(msg), "%s 1", role);
    sendto(s2, msg, strlen(msg), 0, (struct sockaddr*)&server_addr, sizeof(server_addr));
    std::cout << "Registered " << role << " from port " << local_port2 << std::endl;
    struct sockaddr_in p1, p2;
    char buf[MAX_MSG_LEN];

    //recvfrom(s1, buf, sizeof(buf)-1, 0, NULL, NULL);
    //sscanf(buf, "%s %hu", msg, (unsigned short*)&p1.sin_port);
    //inet_pton(AF_INET, msg, &p1.sin_addr);
    //p1.sin_family = AF_INET;

    // --- peer 1 ---
    recvfrom(s1, buf, sizeof(buf) - 1, 0, NULL, NULL);
    std::cout << "11111111111111111 "<< std::endl;
    
    buf[strcspn(buf, "\n")] = 0;                 // strip newline
    char ip_str1[INET_ADDRSTRLEN];
    unsigned short port1;
    sscanf(buf, "%s %hu", ip_str1, &port1);
    p1.sin_family = AF_INET;
    p1.sin_port   = htons(port1);               // <-- htons here!
    inet_pton(AF_INET, ip_str1, &p1.sin_addr);
    std::cout << "Peer1 = " << inet_ntoa(p1.sin_addr) << ":" << ntohs(p1.sin_port) << std::endl;


    //recvfrom(s2, buf, sizeof(buf)-1, 0, NULL, NULL);
    //sscanf(buf, "%s %hu", msg, (unsigned short*)&p2.sin_port);
    //inet_pton(AF_INET, msg, &p2.sin_addr);
    //p2.sin_family = AF_INET;

    // --- peer 2 ---
    recvfrom(s2, buf, sizeof(buf) - 1, 0, NULL, NULL);
    buf[strcspn(buf, "\n")] = 0;
    char ip_str2[INET_ADDRSTRLEN];
    unsigned short port2;
    sscanf(buf, "%s %hu", ip_str2, &port2);
    p2.sin_family = AF_INET;
    p2.sin_port   = htons(port2);
    inet_pton(AF_INET, ip_str2, &p2.sin_addr);

    // punch some initial traffic to create NAT bindings
    send_punch_packets(s1, s2, &p1, &p2);


    snd_pcm_hw_params_t *hw_params;

    snd_pcm_open(&capture_handle, "default", SND_PCM_STREAM_CAPTURE, 0);
    snd_pcm_hw_params_malloc(&hw_params);
    snd_pcm_hw_params_any(capture_handle, hw_params);
    snd_pcm_hw_params_set_access(capture_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(capture_handle, hw_params, FORMAT);
    snd_pcm_hw_params_set_channels(capture_handle, hw_params, CHANNELS);
    snd_pcm_hw_params_set_rate(capture_handle, hw_params, SAMPLE_RATE, 0);
    snd_pcm_hw_params_set_period_size(capture_handle, hw_params, PERIOD_SIZE, 0);
    snd_pcm_hw_params_set_buffer_size(capture_handle, hw_params, PERIOD_SIZE * PERIODS);
    snd_pcm_hw_params(capture_handle, hw_params);
    snd_pcm_hw_params_free(hw_params);
    snd_pcm_prepare(capture_handle);

    snd_pcm_open(&playback_handle, "default", SND_PCM_STREAM_PLAYBACK, 0);
    snd_pcm_hw_params_malloc(&hw_params);
    snd_pcm_hw_params_any(playback_handle, hw_params);
    snd_pcm_hw_params_set_access(playback_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(playback_handle, hw_params, FORMAT);
    snd_pcm_hw_params_set_channels(playback_handle, hw_params, CHANNELS);
    snd_pcm_hw_params_set_rate(playback_handle, hw_params, SAMPLE_RATE, 0);
    snd_pcm_hw_params_set_period_size(playback_handle, hw_params, PERIOD_SIZE, 0);
    snd_pcm_hw_params_set_buffer_size(playback_handle, hw_params, PERIOD_SIZE * PERIODS);
    snd_pcm_hw_params(playback_handle, hw_params);
    snd_pcm_hw_params_free(hw_params);
    snd_pcm_prepare(playback_handle);

    pthread_t capT, sendT, recvT, playT;
    void *arg1 = malloc(sizeof(int) + sizeof(sockaddr_in));
    void *arg2 = malloc(sizeof(int) + sizeof(sockaddr_in));
    ((int*)arg1)[0] = s1;
    ((int*)arg2)[0] = s2;
    memcpy((int*)arg1 + 1, &p1, sizeof(sockaddr_in));
    memcpy((int*)arg2 + 1, &p2, sizeof(sockaddr_in));

    pthread_create(&capT, NULL, capture_thread_func, NULL);
    pthread_create(&sendT, NULL, sender_thread_func, arg1);
    pthread_create(&recvT, NULL, receiver_thread_func, arg2);
    pthread_create(&playT, NULL, play_thread_func, NULL);

    pthread_join(capT, NULL);
    pthread_join(sendT, NULL);
    pthread_join(recvT, NULL);
    pthread_join(playT, NULL);

    close(s1); close(s2);
    return 0;
}
