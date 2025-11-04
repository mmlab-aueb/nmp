// g++ -std=c++17 udp_audio_receiver_threaded.cpp -o udp_audio_receiver_threaded_cpp  -lasound -lpthread
/*           */ 
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

/* ────────── macros (same names/values as the C original) ─────────── */
#define LISTEN_PORT      5000
#define SAMPLE_RATE      44100
#define CHANNELS         1
#define FORMAT           SND_PCM_FORMAT_S16_LE
#define FRAME_SIZE       2
#define FRAMES_PER_CHUNK 128
#define CHUNK_SIZE       (FRAMES_PER_CHUNK * FRAME_SIZE)
#define PERIOD_SIZE      FRAMES_PER_CHUNK
#define PERIODS          2
#define BUFFER_CAPACITY  64
/* ------------------------------------------------------------------- */

/* ────────── ring-buffer shared by the two threads ─────────────────── */
char ring_buffer[BUFFER_CAPACITY][CHUNK_SIZE];
int  write_index   = 0;
int  read_index    = 0;
int  buffer_count  = 0;

pthread_mutex_t buffer_mutex     = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  buffer_not_empty = PTHREAD_COND_INITIALIZER;

snd_pcm_t* playback_handle = nullptr;

/* ────────── UDP receiver thread ───────────────────────────────────── */
void* receiver_thread_func(void*)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("socket"); std::exit(EXIT_FAILURE); }

    sockaddr_in local{};
    local.sin_family      = AF_INET;
    local.sin_port        = htons(LISTEN_PORT);
    local.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, reinterpret_cast<sockaddr*>(&local), sizeof(local)) < 0)
    { perror("bind"); std::exit(EXIT_FAILURE); }

    char tmp[CHUNK_SIZE];

    while (true) {
        ssize_t n = recvfrom(sock, tmp, CHUNK_SIZE, 0, nullptr, nullptr);
        if (n < 0) { perror("recvfrom"); continue; }
        printf("Received %zd bytes\n", n);

        pthread_mutex_lock(&buffer_mutex);
        if (buffer_count < BUFFER_CAPACITY) {
            std::memcpy(ring_buffer[write_index], tmp, CHUNK_SIZE);
            write_index = (write_index + 1) % BUFFER_CAPACITY;
            ++buffer_count;
            pthread_cond_signal(&buffer_not_empty);
        }
        pthread_mutex_unlock(&buffer_mutex);
    }
    close(sock);
    return nullptr;
}

/* ────────── ALSA playback thread ──────────────────────────────────── */
void* playback_thread_func(void*)
{
    while (true) {
        pthread_mutex_lock(&buffer_mutex);
        while (buffer_count == 0)
            pthread_cond_wait(&buffer_not_empty, &buffer_mutex);

        const char* data = ring_buffer[read_index];
        read_index  = (read_index + 1) % BUFFER_CAPACITY;
        --buffer_count;
        pthread_mutex_unlock(&buffer_mutex);

        snd_pcm_sframes_t frames =
            snd_pcm_writei(playback_handle, data, FRAMES_PER_CHUNK);
        if (frames < 0)
            snd_pcm_recover(playback_handle, frames, 0);

        snd_pcm_sframes_t delay{};
        snd_pcm_delay(playback_handle, &delay);
        printf("ALSA delay: %ld frames (%.2f ms)\n",
               delay, (float)delay * 1000 / SAMPLE_RATE);
    }
    return nullptr;
}

/* ────────── main ──────────────────────────────────────────────────── */
int main()
{
    /* Configure ALSA playback */
    snd_pcm_hw_params_t* hw = nullptr;
    if (snd_pcm_open(&playback_handle, "default",
                     SND_PCM_STREAM_PLAYBACK, 0) < 0)
    { perror("snd_pcm_open"); return EXIT_FAILURE; }

    snd_pcm_hw_params_malloc(&hw);
    snd_pcm_hw_params_any(playback_handle, hw);
    snd_pcm_hw_params_set_access (playback_handle, hw, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format (playback_handle, hw, FORMAT);
    snd_pcm_hw_params_set_channels(playback_handle, hw, CHANNELS);
    snd_pcm_hw_params_set_rate   (playback_handle, hw, SAMPLE_RATE, 0);

    snd_pcm_uframes_t period = PERIOD_SIZE;
    snd_pcm_uframes_t buf_sz = PERIOD_SIZE * PERIODS;
    snd_pcm_hw_params_set_period_size(playback_handle, hw, period, 0);
    snd_pcm_hw_params_set_buffer_size(playback_handle, hw, buf_sz);

    snd_pcm_hw_params(playback_handle, hw);
    snd_pcm_hw_params_free(hw);
    snd_pcm_prepare(playback_handle);

    /* Launch threads */
    pthread_t recv_thread{}, play_thread{};
    pthread_create(&recv_thread, nullptr, receiver_thread_func, nullptr);
    pthread_create(&play_thread, nullptr, playback_thread_func, nullptr);

    pthread_join(recv_thread, nullptr);
    pthread_join(play_thread,  nullptr);

    snd_pcm_close(playback_handle);
    return 0;
}
