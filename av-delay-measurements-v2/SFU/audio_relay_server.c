// udp_audio_relay_server.c
// gcc udp_audio_relay_server.c -o udp_audio_relay_server

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <errno.h>

#define PORT_PUNCH 10000   // where client sends punch packet
#define PORT_AUDIO 20000   // where sender sends audio
#define BUFFER_SIZE 1500

int main() {
    int punch_sock = socket(AF_INET, SOCK_DGRAM, 0);
    int audio_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (punch_sock < 0 || audio_sock < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in punch_addr = {0};
    punch_addr.sin_family = AF_INET;
    punch_addr.sin_port = htons(PORT_PUNCH);
    punch_addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(punch_sock, (struct sockaddr *)&punch_addr, sizeof(punch_addr)) < 0) {
        perror("bind punch_sock");
        return 1;
    }

    struct sockaddr_in audio_addr = {0};
    audio_addr.sin_family = AF_INET;
    audio_addr.sin_port = htons(PORT_AUDIO);
    audio_addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(audio_sock, (struct sockaddr *)&audio_addr, sizeof(audio_addr)) < 0) {
        perror("bind audio_sock");
        return 1;
    }

    printf("Relay Server Listening...\n");

    // Step 1: receive initial punch packet and store client IP:20000
    char buffer[BUFFER_SIZE];
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    //printf("Waiting for punch packet on port %d...\n", PORT_PUNCH);
    //ssize_t recv_len = recvfrom(punch_sock, buffer, sizeof(buffer), 0,
    //                            (struct sockaddr *)&client_addr, &client_len);
    //if (recv_len < 0) {
    //    perror("recvfrom punch");
    //    return 1;
    //}

    // modify port to 20000 (where client expects audio)
    client_addr.sin_port = htons(20000);
    //printf("Got punch from %s:%d, will forward audio to %s:20000\n",
    //       inet_ntoa(client_addr.sin_addr), ntohs(((struct sockaddr_in *)&client_addr)->sin_port),
    //       inet_ntoa(client_addr.sin_addr));
    for(int i = 0 ; i < 10 ; i++ ){
        printf("Waiting for punch packet on port %d...\n", PORT_PUNCH);
        ssize_t recv_len = recvfrom(punch_sock, buffer, sizeof(buffer), 0,
                                    (struct sockaddr *)&client_addr, &client_len);
        if (recv_len < 0) {
            perror("recvfrom punch");
            return 1;
        }
    }
    // Step 2: relay audio packets
    int counter = 0;
    while (1) {
        counter++;
        struct sockaddr_in sender_addr;
        socklen_t sender_len = sizeof(sender_addr);
        ssize_t audio_len = recvfrom(audio_sock, buffer, sizeof(buffer), 0,
                                     (struct sockaddr *)&sender_addr, &sender_len);
        if (audio_len < 0) {
            perror("recvfrom audio");
            continue;
        }

        // Relay
        sendto(punch_sock, buffer, audio_len, 0,
               (struct sockaddr *)&client_addr, sizeof(client_addr));
        printf("#%d Relayed %zd bytes from %s:%d to %s:20000\n",counter,
               audio_len,
               inet_ntoa(sender_addr.sin_addr), ntohs(sender_addr.sin_port),
               inet_ntoa(client_addr.sin_addr));
    }

    close(punch_sock);
    close(audio_sock);
    return 0;
}
