// Code in this file is licensed under the GNU General Public License v3.0:
// 
//   This program is free software: you can redistribute it and/or modify
//   it under the terms of the GNU General Public License as published by
//   the Free Software Foundation, either version 3 of the License, or
//   (at your option) any later version.
// 
//   This program is distributed in the hope that it will be useful,
//   but WITHOUT ANY WARRANTY; without even the implied warranty of
//   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
//   GNU General Public License for more details.
// 
//   You should have received a copy of the GNU General Public License
//   along with this program. If not, see <https://www.gnu.org/licenses/>.
//
// For full license terms, see the LICENSE (GPLv3) and LICENSE-APACHE files
// included with this source distribution.
// -----------------------------------------------------------------------------
// Code by mmlab-AUEB, 2025.
//

#include <iostream>
#include <string>
#include <sstream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>

#define MCU_PORT 8888   
#define MCU_IP   "195.251.234.16" 
#define BUFFER_SIZE 1024

struct CommandPacket {
    uint8_t code;
    unsigned value;
    
    int serialize(char* buff){
		buff[0] = code;
		memcpy (buff+sizeof(code), &value, sizeof(value));
		return sizeof(code)+sizeof(value);
		}
};

// Function to map user input to code & value
bool parseCommand(const std::string &input, CommandPacket &packet) {
    std::istringstream iss(input);
    std::string cmd;
    iss >> cmd;

    if (cmd == "reset") {
        packet.code = 10; packet.value = 0;
    } else if (cmd == "mcu") {
        packet.code = 11; packet.value = 0;
    } else if (cmd == "sfu") {
        packet.code = 12; packet.value = 0;
    } else if (cmd == "threads") {
        int x; if (!(iss >> x)) return false;
        packet.code = 13; packet.value = x;
    } else if (cmd == "jpg") {
        int x; if (!(iss >> x) || x <= 0 || x >= 100) return false;
        packet.code = 14; packet.value = x;
    } else if (cmd == "state") {
        packet.code = 15; packet.value = 0;
    } else if (cmd == "streams") {
        int x; if (!(iss >> x) || x <= 0) return false;
        packet.code = 16; packet.value = x;
    } else if (cmd == "quit" || cmd == "exit") {
        return false; // special flag to exit
    } else {
        std::cerr << "Unknown command.\n";
        return false;
    }
    return true;
}

int main() {
    int sockfd;
    struct sockaddr_in mcu_addr{};
    char sendBuffer[BUFFER_SIZE];
    char buffer[BUFFER_SIZE];

    // Create UDP socket
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation failed");
        return 1;
    }

    // MCU address setup
    mcu_addr.sin_family = AF_INET;
    mcu_addr.sin_port = htons(MCU_PORT);
    if (inet_pton(AF_INET, MCU_IP, &mcu_addr.sin_addr) <= 0) {
        perror("Invalid MCU IP address");
        close(sockfd);
        return 1;
    }

    std::cout << "MCU Controller started. Type commands (reset, mcu, sfu, threads X, jpg X, state, streams X). Type 'quit' to exit.\n";

    while (true) {
        std::cout << "> ";
        std::string userInput;
        std::getline(std::cin, userInput);
        if (userInput.empty()) {
			continue;
		}

        CommandPacket packet{};
        if (!parseCommand(userInput, packet)) {
            if (userInput == "quit" || userInput == "exit") break;
            continue;
        }

        // Send packet to MCU
        unsigned packetlen  = packet.serialize(sendBuffer);
        ssize_t sent = sendto(sockfd, sendBuffer, packetlen, 0, (struct sockaddr*) &mcu_addr, sizeof(mcu_addr));
        if (sent < 0) { 
			perror("sendto failed"); continue;
		}

        // Receive response with timeout
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(sockfd, &fds);
        struct timeval timeout{2, 0};  // 2 sec timeout

        int rv = select(sockfd + 1, &fds, nullptr, nullptr, &timeout);
        if (rv == -1) {
            perror("select");
            continue;
        } else if (rv == 0) {
            std::cout << "No response from MCU (timeout).\n";
            continue;
        }

        socklen_t len = sizeof(mcu_addr);
        ssize_t n = recvfrom(sockfd, buffer, BUFFER_SIZE - 1, 0, (struct sockaddr*)&mcu_addr, &len);
        if (n < 0) {
            perror("recvfrom failed");
            continue;
        }

        buffer[n] = '\0';
        std::cout << "MCU Response: " << buffer << "\n";
    }

    close(sockfd);
    return 0;
}
