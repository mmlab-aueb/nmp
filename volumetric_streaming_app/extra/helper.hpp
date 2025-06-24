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
    va_end(args);
}
/**
 * ========================================================
 * Inter-thread communication
 * ========================================================
 * */

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
   return msg;
}  
 
/**
 * Writes intra-snds message to the specified queue (thread safe)
 * */	   
void writeMessageToThread(std::vector<void*> &v, pthread_mutex_t &m,  pthread_cond_t &c,  void* msg){
  	pthread_mutex_lock (&m);
  	if (v.size() > 10){ //drop packets in case the network is slow; avoid buffering pointclouds for too long
		v.erase(v.begin());
	} 	
	v.push_back(msg);  
	pthread_cond_signal(&c);
	pthread_mutex_unlock (&m);
	return;
}   

struct MessageToThread{ 
	char* buffer;
	unsigned bufferSize;
	
	MessageToThread(const char* _buffer, const unsigned& _bufferSize){
		buffer = (char*) malloc(_bufferSize);
		bufferSize = _bufferSize;
		memcpy(buffer, _buffer, bufferSize);
	}
	~MessageToThread(){
		free (buffer);
		}
	
};
