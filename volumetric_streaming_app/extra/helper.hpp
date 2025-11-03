// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2015-2017 Intel Corporation. All Rights Reserved.


#include <unistd.h>
#include <mutex>
#include <cstdarg>
#include <pthread.h>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <ctime>

/**
 * Thread safe logging. Arguments are in format similar to printf
 * */
 std::mutex print_mutex;
void thread_safe_print(const char* format, ...) {
	
	auto now = std::chrono::system_clock::now();
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
	
    std::lock_guard<std::mutex> lock(print_mutex);
    printf("%ld ",millis);
    std::va_list args;
    va_start(args, format);
    vprintf(format, args);
    //printf("\n"); // Optional: add a newline automatically
    va_end(args);
}
/**
 * ========================================================
 * Inter-thread communication
 * ========================================================
 * */

 struct MessageToThread{ 
	char* buffer;
	unsigned bufferSize;
	unsigned frameId; //frame related to message
	uint8_t sourceId; //src of frame related to message
	uint8_t destId; //used internally by MCU to identify one or more destination IPs
	uint8_t MessageType; //message type (5 for data plane etc)
	struct sockaddr_in* senderIP;
	struct sockaddr_in* destIP;
	uint8_t payloadType;
	bool isReset;
	unsigned destThread;

	std::chrono::time_point<std::chrono::system_clock> creation_date;
	
	
	MessageToThread(const char* _buffer, const unsigned& _bufferSize, const uint8_t& _sourceId=255, const unsigned& _frameId=255, const unsigned& _destId=255, const unsigned& _destThread=255 ){
		buffer = (char*) malloc(_bufferSize);
		bufferSize = _bufferSize;
		memcpy(buffer, _buffer, bufferSize);
		frameId = _frameId;
		sourceId = _sourceId;
		destId = _destId;
		uint8_t payloadType = 0;
		senderIP = (sockaddr_in*) malloc(sizeof(sockaddr_in));
		destIP = (sockaddr_in*) malloc(sizeof(sockaddr_in));
		creation_date = std::chrono::system_clock::now();
		isReset = false;
		destThread = _destThread;
	}
	
	~MessageToThread(){
		free (buffer);
		free (senderIP);
		free (destIP);
		}
		
	void setDestId(uint8_t _dstId){
		destId = _dstId;
		}
		
	void print(){
		std::string str="";
		str+=" frameId "+std::to_string(frameId)+" sourceId "+std::to_string(sourceId)+" destId "+std::to_string(destId)+  " sennderIP "+inet_ntoa(senderIP->sin_addr)
			+  " destIP "+inet_ntoa(destIP->sin_addr)+ " isReset " +std::to_string(isReset);
	
		printf("%s\n", str);

		
		}
	
};

/**
 * Writes intra-snds message to the specified queue (thread safe)
 * */	   
void writeMessageToThread(std::vector<void*> &v, pthread_mutex_t &m,  pthread_cond_t &c,  void* msg){
  	pthread_mutex_lock (&m);
  	unsigned vector_limit = 10;
  	uint8_t destThread = ((MessageToThread*)msg)->destThread;
  	if ( destThread == 1 || destThread == 2){ // msges to thread one are packets/chunks not frames, hence need higher limit
		vector_limit = 1000;
		}
  	if (v.size() > vector_limit){ //drop packets in case the network is slow; avoid buffering pointclouds for too long
		MessageToThread *tmp_msg = (MessageToThread*)v.front();
		printf("dropped packet due to load. Packet destionation thread: %d\n", tmp_msg->destThread);
		delete tmp_msg;
		v.erase(v.begin());
	} 	
	v.push_back(msg);  
	pthread_cond_signal(&c);
	pthread_mutex_unlock (&m);
	return;
}   

/**
 * Reads intra-snds message from the specified queue (thread safe)
 * */
void* readMessageFromThread(std::vector<void*>  &v, pthread_mutex_t &m,  pthread_cond_t &c){
   pthread_mutex_lock (&m);
   int rc=0;
   while (v.empty() && rc==0){
	 	rc = pthread_cond_wait (&c, &m);
   }
   void* msg = v.front();
   v.erase(v.begin());
   pthread_mutex_unlock (&m);
   //printf("Pending frames in rendering buffer/vector: %d\n", v.size());
   return msg;
}  

void emptyMessageQueue(std::vector<void*> &v, pthread_mutex_t &m,  pthread_cond_t &c ){
	pthread_mutex_lock (&m);
  	while (v.size() > 0){ //drop packets in case the network is slow; avoid buffering pointclouds for too long
		delete v.front();
		v.erase(v.begin());
	} 	
	pthread_cond_signal(&c);
	pthread_mutex_unlock (&m);
	//log(__FUNCTION__, "Wrote packet and notified receiver.");
	return;
	
	}
/**
 * ========================================================
 * Etc.
 * ========================================================
 **/

unsigned long hash_memory(const void* data, size_t size) {
    const unsigned char* ptr = static_cast<const unsigned char*>(data);
    unsigned long hash = 5381;

    for (size_t i = 0; i < size; ++i) {
        hash = ((hash << 5) + hash) + ptr[i]; // hash * 33 + ptr[i]
    }

    return hash;
}
