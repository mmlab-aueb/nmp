// === server_for_p2p_connection.c ===
// Compile: gcc server_for_p2p_connection.c -o server_for_p2p_connection

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 10000
#define MAX_CLIENTS 2

struct client_info {
    struct sockaddr_in addr1;
    struct sockaddr_in addr2;
    int received; // bitmask: 1 = addr1 received, 2 = addr2 received, 3 = both
    char role[16];
};

int main() {
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);
    struct client_info clients[MAX_CLIENTS] = {0};

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    printf("Server listening on UDP port %d\n", PORT);

    while (1) {
        char buffer[64];
        int recv_bytes = recvfrom(sockfd, buffer, sizeof(buffer) - 1, 0,
                                  (struct sockaddr *)&client_addr, &addr_len);
        buffer[recv_bytes] = '\0';

        if (recv_bytes > 0) {
            char role[16];
            int port_index = -1;

            if (sscanf(buffer, "%15s %d", role, &port_index) == 2 && port_index >= 0 && port_index <= 1) {
                printf("Received registration: role=%s port_index=%d from %s:%d\n", role, port_index,
                       inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

                // Find or assign client slot
                int idx = -1;
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (strcmp(clients[i].role, role) == 0 || clients[i].role[0] == '\0') {
                        idx = i;
                        break;
                    }
                }
                if (idx >= 0) {
                    if (clients[idx].role[0] == '\0') {
                        strncpy(clients[idx].role, role, sizeof(clients[idx].role)-1);
                    }
                    if (port_index == 0) {
                        clients[idx].addr1 = client_addr;
                        clients[idx].received |= 1;
                    } else {
                        clients[idx].addr2 = client_addr;
                        clients[idx].received |= 2;
                    }
                }

                // If both clients have registered both ports
                if (clients[0].received == 3 && clients[1].received == 3) {
                    for (int i = 0; i < MAX_CLIENTS; ++i) {
                        struct client_info *peer = &clients[1 - i];

                        char msg1[64], msg2[64];
                        const char *ip = inet_ntoa(peer->addr1.sin_addr);
                        int port1 = ntohs(peer->addr1.sin_port);
                        int port2 = ntohs(peer->addr2.sin_port);
                        snprintf(msg1, sizeof(msg1), "%s %d", ip, port1);
                        snprintf(msg2, sizeof(msg2), "%s %d", ip, port2);

                        sendto(sockfd, msg1, strlen(msg1), 0,
                               (struct sockaddr *)&clients[i].addr1, sizeof(clients[i].addr1));
                        sendto(sockfd, msg2, strlen(msg2), 0,
                               (struct sockaddr *)&clients[i].addr2, sizeof(clients[i].addr2));

                        printf("Sent to client %d ports: %s, %s\n", i + 1, msg1, msg2);
                    }
                    memset(clients, 0, sizeof(clients)); // reset for next pair
                }
            }
        }
    }

    close(sockfd);
    return 0;
}
