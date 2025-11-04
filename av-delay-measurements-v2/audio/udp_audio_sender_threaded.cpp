// g++ -std=c++17 udp_audio_sender_threaded.cpp -o udp_audio_sender_threaded -lasound -lpthread
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>
#include <alsa/asoundlib.h>

// ====================== constants ===================================
#define SAMPLE_RATE       44100
#define CHANNELS          1
#define FORMAT            SND_PCM_FORMAT_S16_LE
#define FRAME_SIZE        2
#define FRAMES_PER_CHUNK  128
#define CHUNK_SIZE        (FRAMES_PER_CHUNK * FRAME_SIZE)
#define PERIOD_SIZE       FRAMES_PER_CHUNK
#define PERIODS           2
#define BUFFER_CAPACITY   64

/* ────────── ring-buffer shared by the two threads ─────────────────── */
char ring_buffer[BUFFER_CAPACITY][CHUNK_SIZE];
int  write_index   = 0;
int  read_index    = 0;
int  buffer_count  = 0;


pthread_mutex_t buffer_mutex     = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  buffer_not_empty = PTHREAD_COND_INITIALIZER;

snd_pcm_t*      capture_handle = nullptr;
int             sock           = -1;
struct sockaddr_in dest_addr{};

// ===================== ALSA setup ======================
void alsa_capture_init()
{
    snd_pcm_hw_params_t* hw_params = nullptr;
    if (snd_pcm_open(&capture_handle, "default", SND_PCM_STREAM_CAPTURE, 0) < 0)
        //throw std::string("Failed to open ALSA capture device");
        std::cout<<"Failed to open ALSA capture device"<<std::endl;
    snd_pcm_hw_params_malloc(&hw_params);
    snd_pcm_hw_params_any(capture_handle, hw_params);
    snd_pcm_hw_params_set_access (capture_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format (capture_handle, hw_params, FORMAT);
    snd_pcm_hw_params_set_channels(capture_handle, hw_params, CHANNELS);
    snd_pcm_hw_params_set_rate   (capture_handle, hw_params, SAMPLE_RATE, 0);

    snd_pcm_uframes_t period_size = PERIOD_SIZE;
    snd_pcm_uframes_t buffer_size = PERIOD_SIZE * PERIODS;
    snd_pcm_hw_params_set_period_size (capture_handle, hw_params, period_size, 0);
    snd_pcm_hw_params_set_buffer_size (capture_handle, hw_params, buffer_size);

    snd_pcm_hw_params(capture_handle, hw_params);
    snd_pcm_hw_params_free(hw_params);
    snd_pcm_prepare(capture_handle);

    unsigned actual_channels{};
    snd_pcm_hw_params_get_channels(hw_params, &actual_channels);
    printf("Using channels: %u\n", actual_channels);
}

// ===================== thread funcs =====================
void* capture_thread_func(void*)
{
    char temp_buffer[CHUNK_SIZE];

    while (1) {
        snd_pcm_readi(capture_handle, temp_buffer, FRAMES_PER_CHUNK);
        snd_pcm_sframes_t delay;
        snd_pcm_delay(capture_handle, &delay);
        printf("ALSA delay: %ld frames (%.2f ms)\n", delay, (float)delay * 1000 / SAMPLE_RATE);
        pthread_mutex_lock(&buffer_mutex);
        if (buffer_count < BUFFER_CAPACITY) {
            memcpy(ring_buffer[write_index], temp_buffer, CHUNK_SIZE);
            write_index = (write_index + 1) % BUFFER_CAPACITY;
            ++buffer_count;
            pthread_cond_signal(&buffer_not_empty);
        }
        pthread_mutex_unlock(&buffer_mutex);
    }
    return nullptr;
}

void* sender_thread_func(void*)
{
    int packet_count = 0;

    while (1) {
        pthread_mutex_lock(&buffer_mutex);
        while (buffer_count == 0)
            pthread_cond_wait(&buffer_not_empty, &buffer_mutex);

        char* data = ring_buffer[read_index];
        read_index = (read_index + 1) % BUFFER_CAPACITY;
        --buffer_count;
        pthread_mutex_unlock(&buffer_mutex);

        ssize_t bytes_sent = sendto(sock, data, CHUNK_SIZE, 0,
                                    (struct sockaddr*)&dest_addr,
                                    sizeof(dest_addr));
        printf("Packet #%d | Sent: %zd bytes\n", ++packet_count, bytes_sent);
    }
    return nullptr;
}

// ======================─ main ======================───
int main(int argc, char* argv[])
{
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <receiver_ip> <receiver_port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char* ip_arg = argv[1];
    int         port   = atoi(argv[2]);

    // UDP socket
    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port   = htons(port);
    inet_pton(AF_INET, ip_arg, &dest_addr.sin_addr);

    alsa_capture_init();

    // start threads
    pthread_t cap_thread, send_thread;
    pthread_create(&cap_thread,  nullptr, capture_thread_func, nullptr);
    pthread_create(&send_thread, nullptr, sender_thread_func,   nullptr);

    pthread_join(cap_thread,  nullptr);
    pthread_join(send_thread, nullptr);

    snd_pcm_close(capture_handle);
    close(sock);
    return 0;
}
