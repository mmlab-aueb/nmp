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


/**
 * compile and run: reset && APP=UDPhopePunch; gcc -c ${APP}.cpp -o ${APP} -lstdc++ && ./${APP}
*/

#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h> // inet_addr()

#include <iostream>
#include <fstream>
#include <netinet/tcp.h>
#include <cstring>
#include <unistd.h>

int main(void) {
/**
	 * Start UDP server
	 */
	int serverSocket = socket(AF_INET, SOCK_DGRAM, 0);
		
	sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(8888);
    serverAddress.sin_addr.s_addr = INADDR_ANY;

	bind(serverSocket, (struct sockaddr*)&serverAddress, sizeof(serverAddress));
    // listening to the assigned socket
	printf("Waiting for requests.\n");

	char* buffer = new char [9999999]; //store the chunks of the frame
	char receivedPacket[1500] = {0};
	unsigned CHUNK_SIZE = 1400;
	
	struct sockaddr_in cliaddr; 
	socklen_t len = sizeof(cliaddr);
	
	unsigned MAX_CONSUMER_NUM = 10;
	struct sockaddr_in* consumers = (sockaddr_in*) malloc(sizeof(cliaddr) * MAX_CONSUMER_NUM);
	unsigned consumer_num =0;

	while (true){
		int recChunkLen = recvfrom(serverSocket, receivedPacket, 1500 , MSG_WAITALL, ( struct sockaddr *) &cliaddr, &len);
		/// simple UDP hole punching protocol
		/// <REQUEST_TYPE (1byte)>  : 0 for consumer reqistration, 1 for producer lookup (for consumer) 
		/// 2 is empty response to consumer lookup, 3 is full response to consumer lookup
		/// 4 is ACK to registration request
		if (receivedPacket[0] == 0 ){
			//new consumer registration
			char ip[INET_ADDRSTRLEN];
			inet_ntop (AF_INET, &(cliaddr.sin_addr), ip, sizeof (ip));
			uint16_t port = htons (cliaddr.sin_port);
			printf ("Got new consumer registration. host %s:%d\n", ip, port);
			std::memcpy(consumers+sizeof(cliaddr)*consumer_num, &cliaddr, sizeof (cliaddr));
			consumer_num++;
			
		}else if (receivedPacket[0] == 1 ){
			//producer lookup for consumer
			printf("Got new consumer lookup\n");	
			//if no consumers have registered yet!
			if (consumer_num==0){
				printf("No consumers to send. Empty response.\n");
				//send empty response, singalled by char 2.
				char type = 2;
				if (sendto(serverSocket, &type, sizeof(char), MSG_CONFIRM, (const struct sockaddr *) &cliaddr, len)<0){
					fprintf(stderr, "Send failed: %s\n", strerror(errno));
				}
				continue;
			}
			//format: <type, 1byte> <numofIPs, 1byte><IP1,port1>..<IPN,portN>
			unsigned msg_len = 2*sizeof(char)+consumer_num*sizeof(sockaddr_in);
			char* msg = (char*) malloc (msg_len);
			char type=3;
			//write type
			memcpy(msg, &type, sizeof(char));
			//write number of sockaddresses
			memcpy(msg+sizeof(char), &consumer_num, sizeof(char));
			for (unsigned i=0; i< consumer_num; i++){
				sockaddr_in* tmp_cliaddr = consumers+ i*(sizeof(sockaddr_in));
				//write socketAddr
				std::memcpy(msg+2*sizeof(char)+i*sizeof(tmp_cliaddr), tmp_cliaddr, sizeof(tmp_cliaddr));
				//print log
				char ip[INET_ADDRSTRLEN];
				inet_ntop (AF_INET, &(tmp_cliaddr->sin_addr), ip, sizeof (ip));
				uint16_t port = htons (tmp_cliaddr->sin_port);
				printf ("Send consumer %s:%d\n", ip, port);
			}
			if (sendto(serverSocket, msg, msg_len, MSG_CONFIRM, (const struct sockaddr *) &cliaddr, len)<0){
					fprintf(stderr, "Send failed: %s\n", strerror(errno));
			}
			delete msg;
			continue;
		}else if (receivedPacket[0] == 5 ){
			printf("Got msg forward request (len: %d)\n", recChunkLen);
			//received a chunk that should be forwarded to a remote consumer
			sockaddr_in* tmp_cliaddr = (sockaddr_in*)malloc(sizeof(sockaddr_in));
			memcpy(tmp_cliaddr, receivedPacket+sizeof(char), sizeof(sockaddr_in));

			char ip[INET_ADDRSTRLEN];
			inet_ntop (AF_INET, &(tmp_cliaddr->sin_addr), ip, sizeof (ip));
			uint16_t port = htons (tmp_cliaddr->sin_port);
			printf ("Forwarding to consumer IP %s:%d\n", ip, port);
			char* payload = receivedPacket + sizeof(char) + sizeof(sockaddr_in);
			unsigned payload_len = recChunkLen - sizeof(char) - sizeof(sockaddr_in);
			if (sendto(serverSocket, payload, payload_len, MSG_CONFIRM, (const struct sockaddr *) tmp_cliaddr, len)<0){
				fprintf(stderr, "Send failed: %s\n", strerror(errno));
			}
			free (tmp_cliaddr);
		}else{
			printf("WARNING::Unknown message type.\n");
			}
	}//END WHILE LOOP
		
    return 0;
}//end_main
