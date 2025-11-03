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

//compile with: reset && g++ -o MCUhopePunch MCUhopePunch.cpp -lstdc++ -lpthread -ldraco  `pkg-config --cflags --libs opencv4 libturbojpeg` -g && ./MCUhopePunch

#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h> // inet_addr()
#include <iostream>
#include <fstream>
#include <netinet/tcp.h>
#include <cstring>
#include <unistd.h>
#include <vector>
#include "extra/helper.hpp"
#include <cassert>
#include <thread>
#include <turbojpeg.h>
#include <stdexcept>
#include "draco/core/decoder_buffer.h"
#include "draco/mesh/mesh.h"
#include "draco/point_cloud/point_cloud.h"
#include <draco/compression/encode.h>
#include "draco/point_cloud/point_cloud_builder.h"
#include "draco/io/ply_decoder.h"
#include "draco/compression/point_cloud/point_cloud_kd_tree_decoder.h"
#include <opencv2/opencv.hpp>
#include <limits>
#include <fstream>

#define ENABLE_FRAME_HASHING 0 //0 for true, 1 for false
#define ENABLE_BnW 1 //0 for true, 1 for false
unsigned CONVERT_TO_2D  = 0;  //0 for true, 1 for false
#define ENABLE_DETAILED_TIMING 0 //0 for true, 1 for false

unsigned NUM_OF_ENCODING_THREADS = 8;  //number of threads used for encoding at the producer. WARNING:: currently working only for 1 thread
unsigned NUM_OF_STREAMS = 2;  //number of received streams per consumer, used only for MCU
const unsigned MAX_NUM_OF_STREAMS = 10; //used for allocating memory. This is the max value of NUM_OF_STREAMS which is configurable during runtime
unsigned CHUNK_SIZE = 1450;  //number of received streams per consumer, used only for MCU

unsigned MAX_CONSUMER_NUM = 10;
struct sockaddr_in* consumers;
unsigned consumer_num = 0;

unsigned OPENGL_WIDTH = 1280; //we assume that pcs are appended horizontally (in x-axis)
unsigned OPENGL_HEIGHT = 720;

unsigned char JPEG_QUALITY = 100; //90 is high quality, lower values reduce size and quality

#define WRITE_JPG_TO_FILE 1 //0 for true, 1 for false //for debugging purposes

uint8_t PAYLOAD_TYPE = 1; //0 for point cloud, 1 for JPG..
uint8_t MESSAGE_TYPE; //1-4 for control plane, 5 for data plane

unsigned LISTENING_PORT = 8888;

//server socket, shared by all threads
int serverSocket = socket(AF_INET, SOCK_DGRAM, 0);

//variables for inter-thread communication
std::vector<void*>  v_to_handlerThread;
pthread_mutex_t m_to_handlerThread = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t c_to_handlerThread = PTHREAD_COND_INITIALIZER;

std::vector<void*>  v_to_transcodingThread;
pthread_mutex_t m_to_transcodingThread = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t c_to_transcodingThread = PTHREAD_COND_INITIALIZER;

std::vector<void*>  v_to_decompressingThread;
pthread_mutex_t m_to_decompressingThread = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t c_to_decompressingThread = PTHREAD_COND_INITIALIZER;

std::vector<void*>  v_to_sendingThread;
pthread_mutex_t m_to_sendingThread = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t c_to_sendingThread = PTHREAD_COND_INITIALIZER;

/**
 * Iterates through the consumers struct to find the IP provided in _dest.
 * Returns the ID which is the place of IP in consumers struct (array).
 * Returns -1 is search failed.
 * */
unsigned getConsumerId(const sockaddr_in* _dest){
	char ip_dest[INET_ADDRSTRLEN];
	inet_ntop (AF_INET, &(_dest->sin_addr), ip_dest, sizeof (ip_dest));
	for (unsigned i =0 ; i< consumer_num; i++){
		sockaddr_in*  temp_dest = (sockaddr_in*)consumers+i*sizeof(sockaddr_in);
		char ip_dest2[INET_ADDRSTRLEN];
		inet_ntop (AF_INET, &(temp_dest->sin_addr), ip_dest2, sizeof (ip_dest));
		if ( strcmp (ip_dest2, ip_dest) ==0 ){
			return i;
			}
		}
	return -1;
	}
	
/**
 * returns the sock addr for a given consumer/dest Id
 * */
const sockaddr_in* getConsumerAddr(const uint8_t& destId){
	assert (destId<consumer_num);
	return	(sockaddr_in*)  (consumers+destId*sizeof(sockaddr_in));
	}
	
/**
 * Sends a string, describing the state of the MCU, to the original sender of _msg
 * */
void sendBackState(const struct sockaddr_in* senderIP){
	std::string state_str = "MCU/SFU (0/1): "+std::to_string(CONVERT_TO_2D)+ " #Pointcloud_streams: "+std::to_string(NUM_OF_STREAMS)+ " #Decompressing_threads: "+std::to_string(NUM_OF_ENCODING_THREADS)+ " JPG_QUALITY: "+std::to_string(JPEG_QUALITY)+ " #Receivers: "+std::to_string(consumer_num)+ "\n" ;
	if (sendto(serverSocket, state_str.c_str(), state_str.length(), MSG_CONFIRM, (const struct sockaddr *) senderIP, sizeof(*senderIP))<0){
		fprintf(stderr, "Send failed: %s\n", strerror(errno));
	}
}

/** 
 * Received JPG buffers, fragments them in chunks and sends them to remote destination/enduser
 * */
void *sendingThread(void* ptr){
	unsigned chunkId;
	unsigned frameId=0;
	uint8_t sourceId = 0; 
	
	while (true){
		std::string log = "";
		MessageToThread* msg = (MessageToThread*)readMessageFromThread(v_to_sendingThread, m_to_sendingThread, c_to_sendingThread);
		frameId = msg->frameId;
		sourceId = msg->sourceId;
		
		const struct sockaddr_in *destAddress = getConsumerAddr(msg->destId);
		
		void * bufferToSend = (void*) msg->buffer; //thats our pointcloud
	    unsigned bufferToSend_len = msg->bufferSize;
		
		auto start = std::chrono::system_clock::now();
		/// send buffer to remote musician
		unsigned write_offset = 0;
		unsigned write_offset_pre = 0;

		char* chunkToSend = (char*)malloc(sizeof(sourceId)+sizeof(frameId)+sizeof(chunkId)+sizeof(PAYLOAD_TYPE)+CHUNK_SIZE);	

		/** Toy transmission protocol: [<frame id, 4bytes>, <chunk id, 4bytes>, <1400bytes content>] */
		////fragment buffer localy and send data -- not required, but usefull for transisioning to UDP
		//write sourceId
		memcpy(chunkToSend+write_offset_pre, &sourceId, sizeof(sourceId));
		write_offset_pre += sizeof(sourceId);
		//write frameId
		memcpy(chunkToSend+write_offset_pre, &frameId, sizeof(frameId));
		write_offset_pre += sizeof(frameId);
		
		chunkId = 0;
		unsigned curChunksize = 0;
		unsigned sentBytes = 0;
		for (unsigned i=0; i<bufferToSend_len; ){
			write_offset = write_offset_pre;
			//write chunkId
			memcpy(chunkToSend+write_offset, &chunkId, sizeof(chunkId));
			write_offset += sizeof(chunkId);
			memcpy(chunkToSend+write_offset, &PAYLOAD_TYPE, sizeof(PAYLOAD_TYPE));
			write_offset += sizeof(PAYLOAD_TYPE);
			curChunksize = std::min((unsigned)(bufferToSend_len - i), CHUNK_SIZE);
			memcpy(chunkToSend+write_offset, bufferToSend+i, curChunksize);
			write_offset += curChunksize;
			if (sendto(serverSocket, chunkToSend, write_offset, MSG_CONFIRM, (const struct sockaddr*) destAddress, sizeof(*destAddress))<0){ 
				printf("send failed: %s\n", strerror(errno));
			}
			i+=curChunksize;
			chunkId++;
			sentBytes += write_offset;
			if (frameId % 4 == 0)
				usleep(1); //critical to avoid packet bursts that lead to drops in the 1st router
		}	
		free(chunkToSend);	
		
		#if ENABLE_DETAILED_TIMING == 0
			auto transmission_end = std::chrono::system_clock::now();
			std::chrono::duration<double> transmission_seconds = transmission_end-start;
			std::chrono::duration<double> processing_seconds = transmission_end-msg->creation_date;
			thread_safe_print("T3 SrcID %d Frame %u Chunks %u Tx_b %u Pointcloud_b %lu Transmission_ms %f Processing_ms %f \n", sourceId, frameId, chunkId, sentBytes, bufferToSend_len, transmission_seconds.count()*1000, processing_seconds.count()*1000);
		#endif
	
		delete msg;
		
		frameId++;
		#ifdef STOP_AT 
		if (frameId == STOP_AT){
			printf("Reached experiment limit (\"STOP_AT\"). Exiting..\n");
			exit(1);
		}
		#endif
	}
}

/**
 * Receives a point cloud and converts it to a JPG which is returned in a vector of uchars
 * */
std::vector<uchar> fastPointcloudToJPGbuffer(const draco::PointCloud* pc){
	
	if (!pc || pc->num_points() == 0) {
        throw std::runtime_error("Empty or null point cloud");
    }

    // --- Step 1: Fetch raw buffers for positions and colors ---
    const draco::PointAttribute *pos_attr = pc->attribute(0); // assume position
    const draco::PointAttribute *color_attr = pc->attribute(1); // assume RGB

    // Safety check: attribute layout must match what we expect
    if (pos_attr->num_components() != 3 || pos_attr->data_type() != draco::DT_FLOAT32) {
        throw std::runtime_error("Unexpected position attribute format");
    }
    if (color_attr->num_components() != 3 || color_attr->data_type() != draco::DT_UINT8) {
        throw std::runtime_error("Unexpected color attribute format");
    }

    const size_t num_points = pc->num_points();
    const float *positions = reinterpret_cast<const float*>(pos_attr->buffer()->data());
    const uint8_t *colors  = reinterpret_cast<const uint8_t*>(color_attr->buffer()->data());

    // --- Step 2: Create black OpenCV image ---
    cv::Mat image(OPENGL_HEIGHT, OPENGL_WIDTH, CV_8UC3, cv::Scalar(0, 0, 0));

    const float focalLength = 525.0f; // adjust to your intrinsics
    const int cx = OPENGL_WIDTH / 2;
    const int cy = OPENGL_HEIGHT / 2;

    // --- Step 3: Project points directly using pointer arithmetic ---
    for (size_t i = 0; i < num_points; ++i) {
        const float *p = positions + 3 * i;
        if (p[2] <= 0.01f) continue; // skip points behind camera

        int u = static_cast<int>(cx + focalLength * p[0] / p[2]);
        int v = static_cast<int>(cy - focalLength * p[1] / p[2]);

        if (u >= 0 && u < OPENGL_WIDTH && v >= 0 && v < OPENGL_HEIGHT) {
            const uint8_t *c = colors + 3 * i;
            uchar *pix = image.ptr<uchar>(v) + 3 * u;
            pix[0] = c[0]; // B
            pix[1] = c[1]; // G
            pix[2] = c[2]; // R
        }
    }

    // --- Step 4: Encode image with TurboJPEG ---
    tjhandle handle = tjInitCompress();
    if (!handle) throw std::runtime_error("TurboJPEG init failed");

    unsigned char *jpegBuf = nullptr;
    unsigned long jpegSize = 0;
    int subsamp = TJSAMP_444;  // no chroma subsampling
    int flags = TJFLAG_FASTDCT;

    if (tjCompress2(handle,image.data, image.cols, 0, // pitch = auto
                    image.rows,TJPF_BGR,  // input format matches OpenCV Mat
                    &jpegBuf, &jpegSize, subsamp, JPEG_QUALITY, flags) != 0) {
        tjDestroy(handle);
        throw std::runtime_error(std::string("TurboJPEG compression failed: ") + tjGetErrorStr());
    }

    std::vector<uchar> jpegBuffer(jpegBuf, jpegBuf + jpegSize);

    tjFree(jpegBuf);
    tjDestroy(handle);

    return jpegBuffer;
}

/**
 * unifies a number of pointclouds into a single pointcloud, which is returned
 * */ 
std::unique_ptr<draco::PointCloud> fastMergePointClouds(draco::PointCloud* subpcs  []){
	// Count total points and find how many subpcs we actually got.
    unsigned total_points = 0;
    unsigned n = NUM_OF_ENCODING_THREADS;
    for (unsigned k = 0; k < n; ++k) {
        if (!subpcs[k]) throw std::runtime_error("Null subpc pointer");
        total_points += subpcs[k]->num_points();
    }

    if (total_points == 0) {
        // Empty input: return empty cloud
        draco::PointCloudBuilder empty_builder;
        empty_builder.Start(0);
        return empty_builder.Finalize(false);
    }

    // Initialize builder using first subcloud's attribute layout
    draco::PointCloudBuilder builder;
    builder.Start(total_points);

    const int num_attributes = subpcs[0]->num_attributes();
    std::vector<int> builder_attr_ids(num_attributes);
    std::vector<size_t> attr_stride_bytes(num_attributes);

    // Add attributes to builder and compute stride (bytes per point) for each attr
    for (int a = 0; a < num_attributes; ++a) {
        const auto *a0 = subpcs[0]->attribute(a);
        builder_attr_ids[a] = builder.AddAttribute(
            a0->attribute_type(), a0->num_components(), a0->data_type()
        );

        // bytes per component helper
        size_t bytes_per_component = draco::DataTypeLength(a0->data_type());
        attr_stride_bytes[a] = static_cast<size_t>(a0->num_components()) * bytes_per_component;
    }

    // Finalize builder to get merged cloud object (attributes/buffers allocated)
    constexpr bool deduplicate_points = false;
    std::unique_ptr<draco::PointCloud> merged = builder.Finalize(deduplicate_points);

    // Retrieve destination attribute pointers (mutable)
    // NOTE: merged->attribute(...) commonly returns a pointer type; ensure it's non-null
    std::vector<draco::PointAttribute*> dst_attrs(num_attributes);
    for (int a = 0; a < num_attributes; ++a) {
        // const_cast only if API returns const pointer — adjust if API differs.
        dst_attrs[a] = const_cast<draco::PointAttribute*>( merged->attribute(builder_attr_ids[a]) );
        if (!dst_attrs[a]) throw std::runtime_error("Failed to get destination attribute");
    }

    // Now do the memcpy per attribute. We'll track how many points we've already written.
    unsigned points_written = 0;
    for (unsigned k = 0; k < n; ++k) {
        const unsigned sub_points = subpcs[k]->num_points();
        if (sub_points == 0) continue;

        for (int a = 0; a < num_attributes; ++a) {
            const draco::PointAttribute *src_attr = subpcs[k]->attribute(a);
            draco::PointAttribute *dst_attr = dst_attrs[a];

            // number of entries in attribute buffers (should be num_points)
            const size_t src_entries = src_attr->size(); // usually equals sub_points
            if (src_entries != sub_points) {
                // Might be possible if attribute has different indexing, but we don't support it here.
                throw std::runtime_error("Attribute size mismatch (index remapping not supported)");
            }

            size_t stride = attr_stride_bytes[a]; // bytes per point for this attribute
            size_t copy_bytes = static_cast<size_t>(sub_points) * stride;

            // get raw pointers
            const uint8_t *src_ptr = reinterpret_cast<const uint8_t*>( src_attr->buffer()->data() );
            uint8_t *dst_ptr = reinterpret_cast<uint8_t*>( dst_attr->buffer()->data() );

            if (!src_ptr || !dst_ptr) throw std::runtime_error("Null attribute data buffer");

            // compute destination byte offset for this attribute (points_written * stride)
            size_t dst_byte_offset = static_cast<size_t>(points_written) * stride;

            // safety check: ensure we don't overflow destination buffer
            size_t dst_buffer_bytes = static_cast<size_t>( dst_attr->size() ) * stride;
            if (dst_byte_offset + copy_bytes > dst_buffer_bytes) {
                // Defensive: abort instead of corrupting memory
                std::ostringstream oss;
                oss << "Destination buffer overflow for attribute " << a
                    << ": dst_buf=" << dst_buffer_bytes
                    << ", required_offset+size=" << (dst_byte_offset + copy_bytes);
                throw std::runtime_error(oss.str());
            }

            // Finally do the high-perf copy
            std::memcpy(dst_ptr + dst_byte_offset, src_ptr, copy_bytes);
        }

        points_written += sub_points;
    }

    // points_written should equal total_points
    if (points_written != total_points) {
        throw std::runtime_error("Internal error: total points mismatch after memcpy");
    }

    return merged;
}

static draco::DecoderOptions dec_options;	

/**
 * writes decoded pointcloud to first argument.
 * */
void decodingThread( draco::PointCloud* pc, const char* buffer, const unsigned& bufferSize ){
	draco::DecoderBuffer dec_buffer;
	dec_buffer.Init(buffer, bufferSize);
	draco::PointCloudKdTreeDecoder decoder;
	decoder.Decode(dec_options, &dec_buffer, pc);		
}
	
/**
 * Decompresses pointcloud and creates JPG object
 * */ 	
void *decompressingThread(void* ptr){

	while (true){
		std::string log = "";
		MessageToThread* msg = (MessageToThread*)readMessageFromThread(v_to_decompressingThread, m_to_decompressingThread, c_to_decompressingThread);
		uint8_t sourceId = msg->sourceId;
	    unsigned frameId = msg->frameId;
	    log+="SrcID " + std::to_string(sourceId)+ " Frame "+std::to_string(frameId)+ " ";
	    char * buffer = msg->buffer; //thats our pointcloud
	    unsigned bufferSize = msg->bufferSize;
	    if (bufferSize < CHUNK_SIZE) { free(msg); continue;} //when consumer starts in the middle of transmission, 1st frame will be zero bytes.. @TODO: find the source of this issue..
	#if ENABLE_FRAME_HASHING == 0
		log+="Hash " + std::to_string(hash_memory(buffer,  bufferSize))+ " ";
	#endif
		unsigned sub_bufferSize[NUM_OF_ENCODING_THREADS] = { 0 };
		char* sub_buffer[NUM_OF_ENCODING_THREADS] = { nullptr };
		unsigned index = 0;
		auto decoding_start = std::chrono::system_clock::now();

		std::thread dec_threads [NUM_OF_ENCODING_THREADS] ;
		draco::PointCloud* out_pcs  [NUM_OF_ENCODING_THREADS] = { nullptr };
		bool brokenpointcloud = false;
		for (unsigned i = 0; i<NUM_OF_ENCODING_THREADS; i++){
			memcpy(&sub_bufferSize[i], buffer+index, sizeof(unsigned));
			index+=sizeof(unsigned); 
			sub_buffer[i] = buffer+index;
			sub_bufferSize[i] = std::min (sub_bufferSize[i], bufferSize-index); //in case of lost/corrupted packets, received size can be lower than exepcted, thus causing seg fault 
			index+= sub_bufferSize[i]; 
			if (index > bufferSize) {
				printf("index %d bufferSize %d \n", index, bufferSize);
				thread_safe_print("T2 Broken subpointcloud indexing. Skipping pointcloud. \n");
				brokenpointcloud = true;
				break;
			}
			out_pcs[i] = new draco::PointCloud();
			dec_threads[i] = std::thread(decodingThread, out_pcs[i], sub_buffer[i], sub_bufferSize[i]);
		}
		
		//unify sub point-clouds
		for (unsigned i = 0; i<NUM_OF_ENCODING_THREADS; i++){
			if (dec_threads[i].joinable())
				dec_threads[i].join();		
		}
		#if ENABLE_DETAILED_TIMING == 0
			auto decoding_end = std::chrono::system_clock::now();
			std::chrono::duration<double> dracodec_seconds = decoding_end - decoding_start;
			log+="DracoDecoding_time " + std::to_string(dracodec_seconds.count()*1000)+ " ";
		#endif
		    
	    std::unique_ptr<draco::PointCloud> out_pc;
		if (!brokenpointcloud) {
			out_pc = fastMergePointClouds (out_pcs);
		}
		//free memory
		for (unsigned i = 0; i<NUM_OF_ENCODING_THREADS; i++){
			delete out_pcs[i];	
		}
		
		if (brokenpointcloud){delete msg; continue;} 
		
		#if ENABLE_DETAILED_TIMING == 0
			auto merging_end = std::chrono::system_clock::now();
			std::chrono::duration<double> merging_seconds = merging_end - decoding_end;
			log+="SubPCMerging_time " + std::to_string(merging_seconds.count()*1000)+ " ";
		#endif
		    
			std::vector<uchar> jpg_vec = fastPointcloudToJPGbuffer(out_pc.get());
		
		#if ENABLE_DETAILED_TIMING == 0
			auto jpg_encoding_end = std::chrono::system_clock::now();
			std::chrono::duration<double> jpg_encoding_sec = jpg_encoding_end - merging_end;
			log+="JPG_encoding_time " + std::to_string(jpg_encoding_sec.count()*1000)+ " ";
		#endif
		
			//// Write to a file for debugging
		#if WRITE_JPG_TO_FILE == 0
			std::ofstream file("debug_frame_"+std::to_string(frameId)+".jpg", std::ios::binary);
			file.write(reinterpret_cast<const char*>(jpg_vec.data()), jpg_vec.size());
			file.close();
		#endif
			assert (jpg_vec.size() > 0);
			
			log+="JPG_size "+ std::to_string(jpg_vec.size()) +" ";
			MessageToThread *msg_2 = new MessageToThread(reinterpret_cast<char*>(jpg_vec.data()), jpg_vec.size(), sourceId, frameId );
			msg_2->creation_date = msg->creation_date;
			msg_2->destId = msg->destId;
			msg->destThread = 5;
			writeMessageToThread(v_to_sendingThread, m_to_sendingThread, c_to_sendingThread, (void*) msg_2);			
	
		if (strcmp(log.c_str(), "")!=0){
			thread_safe_print("T2 %s\n", log.c_str());
		}			
		delete msg;	
	}	
}

/***
 * resets state, namely, receivers and frame counters in transcoding thread
 * */
void resetState(){
	consumer_num = 0;	
	//send Reset Message to transcoding thread
	MessageToThread* msg = new MessageToThread("reset", 5); //parameters are not used here
	msg->isReset = true;
	emptyMessageQueue(v_to_transcodingThread, m_to_transcodingThread, c_to_transcodingThread);
	msg->destThread = 3;
	writeMessageToThread(v_to_transcodingThread, m_to_transcodingThread, c_to_transcodingThread, (void*) msg);
}

/**
 * Receives packets of frames that must be 
 * (a) restructred into frame
 * (b) sends to decompress thread (where PC is decompressed from draco, converted to JPG, fragmented to packets and send to remote destination)
 * TODO:: most of the code is reused from consumer app. Locate shared parts and make a library(?)..
 * */
void *transcodingThread(void* ptr){	
	/**
	 * Initialize per source variables
	 * */
	 //int clientSocket = accept(serverSocket, nullptr, nullptr);
	unsigned* currentFrameId= (unsigned *) malloc(sizeof(unsigned)*MAX_NUM_OF_STREAMS);
	unsigned* receivedBytes = (unsigned *) malloc(sizeof(unsigned)*MAX_NUM_OF_STREAMS);
	unsigned* pointCloudSize = (unsigned *) malloc(sizeof(unsigned)*MAX_NUM_OF_STREAMS);
	unsigned* receivedChunks = (unsigned *) malloc(sizeof(unsigned)*MAX_NUM_OF_STREAMS);
	std::chrono::time_point<std::chrono::system_clock>* start = (std::chrono::time_point<std::chrono::system_clock> *)	malloc(sizeof(std::chrono::time_point<std::chrono::system_clock>)*MAX_NUM_OF_STREAMS);
	char** buffer = (char**) malloc(MAX_NUM_OF_STREAMS * sizeof(char *));

	for (uint8_t i=0; i< MAX_NUM_OF_STREAMS; i++){
		currentFrameId[i] = 0;
		receivedBytes[i] = 0;
		pointCloudSize[i] = 0;
		receivedChunks[i] = 0;
		start[i] = std::chrono::system_clock::now();
		buffer[i] = new char [9999999]; //store the chunks of the frame; at first byte add source ID
		memcpy(buffer, &i, sizeof(i));
		}
		
	struct sockaddr_in* senderAddr;
	struct sockaddr_in* destAddr;
	char* payloadChunk;
	char packetType; //this will be always 5. added only to make code easier to read
	socklen_t addrlen;
	unsigned recChunkLen;
	unsigned headerSize=0;
	while (true){
		MessageToThread* msg = (MessageToThread*)readMessageFromThread(v_to_transcodingThread, m_to_transcodingThread, c_to_transcodingThread);
		if (msg->isReset){
			//reset state
			for (uint8_t i=0; i< MAX_NUM_OF_STREAMS; i++){ 
				currentFrameId[i] = 0;
				receivedBytes[i] = 0;
				pointCloudSize[i] = 0;
				receivedChunks[i] = 0;
				start[i] = std::chrono::system_clock::now();
				memcpy(buffer, &i, sizeof(i));
			}
			printf("Transcoding treead: state reset. \n");
			delete msg;
			continue;
		}
		//find consumer/dest id
		unsigned destId = getConsumerId(msg->destIP);
		assert (destId>=0);
		
		//parse packet: sender addr, packet type, dest addr and payload
		senderAddr = msg->senderIP;
		packetType = (char) *(msg->buffer);
		assert (msg->MessageType == 5);
		destAddr = msg->destIP;
		payloadChunk = msg->buffer;
		recChunkLen = msg->bufferSize;	
		/**
		* Toy transmission protocol: [<source id, 1byte>, <frame id, 4bytes>, <chunk id, 4bytes>, <1400bytes content>]
		* source id is the serial number of the source, which is within [0, NUM_OF_STREAMS)
		**/
		uint8_t sourceId =  *((uint8_t*)payloadChunk);
		headerSize = sizeof(sourceId);
		//read frameId
		assert (sourceId < NUM_OF_STREAMS);
		unsigned frameId =  *((unsigned int*)(payloadChunk+headerSize));
		headerSize += sizeof(frameId);
		//read chunkId
		unsigned chunkId = *((unsigned int*)(payloadChunk+headerSize));
		headerSize += sizeof(chunkId);
		//read payloadType
		uint8_t payloadType =  *((uint8_t*)payloadChunk+headerSize);
		headerSize += sizeof(payloadType);
		if (frameId < currentFrameId[sourceId]){
			///got a late, reordered chunk, ignore it
			thread_safe_print("T1 ScrId %u Got late chunk %u of frame %u, while receiving frame %u. Ignoring it. \n", (unsigned) sourceId, chunkId, frameId, currentFrameId); 
			continue;
		}else if (frameId > currentFrameId[sourceId]){
			///got chunk for next frame, render current
			MessageToThread *msg_2 = new MessageToThread(buffer[sourceId], pointCloudSize[sourceId], sourceId, currentFrameId[sourceId]);
			msg_2->creation_date = msg->creation_date;
			msg_2->destId = destId;
			msg_2->destThread = 4;
			writeMessageToThread(v_to_decompressingThread, m_to_decompressingThread, c_to_decompressingThread, (void*) msg_2);
		
			if(frameId!=currentFrameId[sourceId]+1){
				printf("T1 Warning: Serious reordering. Got chunk for frame %u.\n", frameId);
			}
								
			auto end = std::chrono::system_clock::now();
			std::chrono::duration<double> elapsed_seconds = end-start[sourceId];
			std::time_t end_time = std::chrono::system_clock::to_time_t(end);
			start[sourceId]=end; //reset timer
	 
			thread_safe_print("T1 ScrId %u Frame %u Chunks %u Rx_b %u PointCloud_b %u Interframe_Transm_ms %f \n", (unsigned) sourceId, currentFrameId[sourceId], receivedChunks[sourceId], receivedBytes[sourceId], pointCloudSize[sourceId], elapsed_seconds.count()*1000);
			start[sourceId] = std::chrono::system_clock::now();
			
			currentFrameId[sourceId] = frameId;
			receivedBytes[sourceId] = 0;
			pointCloudSize[sourceId]= 0; 
			receivedChunks[sourceId] = 0;
		}
		///got another chunk of the current frame
		//read payload
		memcpy(buffer[sourceId]+chunkId*CHUNK_SIZE, payloadChunk+headerSize, recChunkLen-headerSize);
		//// fragment buffer localy and send data -- not required, but usefull for transisioning to UDP
		receivedBytes[sourceId] += recChunkLen;
		pointCloudSize[sourceId] += recChunkLen - headerSize;
		receivedChunks[sourceId]++;
		
		delete msg;
		}	
}

/**
 * Received packets from main thread, directly after packets are received.
 * Registers consumers and respond to consumer requests made by producers.
 * Forwards to destination in case of SFU operation.
 * Pushes packets to transcoding thread in case of MCU operation.
 * */
void *handlerThread(void* ptr){
	unsigned recChunkLen;
	
	consumers = (sockaddr_in*) malloc(sizeof(sockaddr_in) * MAX_CONSUMER_NUM);
	bool msgforwarded;
	while (true){
		msgforwarded = false;
		MessageToThread* msg = (MessageToThread*)readMessageFromThread(v_to_handlerThread, m_to_handlerThread, c_to_handlerThread);
		//parse sender addr and received packet (payload)
		/// simple UDP hole punching protocol
		/// <REQUEST_TYPE (1byte)>  : 0 for consumer reqistration, 1 for producer lookup (for consumer) 
		/// 2 is empty response to consumer lookup, 3 is full response to consumer lookup
		/// 4 is ACK to registration request
		/// 5 is data plane packet (to be forwarded)
		/**new consumer registration */
		switch (msg->MessageType){
		case 0: {
			char ip[INET_ADDRSTRLEN];
			struct sockaddr_in* senderIP = msg->senderIP;
			inet_ntop (AF_INET, &(senderIP->sin_addr), ip, sizeof (ip));
			uint16_t port = htons (senderIP->sin_port);
			printf ("Got new consumer registration. host %s:%d\n", ip, port);
			std::memcpy(consumers+sizeof(*senderIP)*consumer_num, senderIP, sizeof (*senderIP));
			consumer_num++;
			/////send back confirmation
			//char type  = 4;
			//if (sendto(serverSocket, &type, sizeof(type), MSG_CONFIRM, (const struct sockaddr *) &cliaddr, len)<0){
					//fprintf(stderr, "Send failed: %s\n", strerror(errno));
			//}	
			break;
			}
		/**producer lookup for consumer */
		case 1:{
			struct sockaddr_in* senderIP = msg->senderIP;
			printf("Got new consumer lookup\n");	
			//if no consumers have registered yet!
			if (consumer_num==0){
				printf("No consumers to send. Empty response.\n");
				//send empty response, singalled by char 2.
				char type = 2;
				if (sendto(serverSocket, &type, sizeof(char), MSG_CONFIRM, (const struct sockaddr *) senderIP, sizeof(*senderIP))<0){
					fprintf(stderr, "Send failed: %s\n", strerror(errno));
				}
				break;
			}
			//format: <type, 1byte> <numofIPs, 1byte><IP1,port1>..<IPN,portN>
			unsigned msg2_len = 2*sizeof(char)+consumer_num*sizeof(sockaddr_in);
			char* msg2 = (char*) malloc (msg2_len);
			char type=3;
			//write type
			memcpy(msg2, &type, sizeof(char));
			//write number of sockaddresses
			memcpy(msg2+sizeof(char), &consumer_num, sizeof(char));
			for (unsigned i=0; i< consumer_num; i++){
				sockaddr_in* tmp_cliaddr = consumers+ i*(sizeof(sockaddr_in));
				//write socketAddr
				std::memcpy(msg2+2*sizeof(char)+i*sizeof(tmp_cliaddr), tmp_cliaddr, sizeof(tmp_cliaddr));
				//print log
				char ip[INET_ADDRSTRLEN];
				inet_ntop (AF_INET, &(tmp_cliaddr->sin_addr), ip, sizeof (ip));
				uint16_t port = htons (tmp_cliaddr->sin_port);
				printf ("Send consumer %s:%d\n", ip, port);
			}
			if (sendto(serverSocket, msg2, msg2_len, MSG_CONFIRM, (const struct sockaddr *) senderIP, sizeof(*senderIP))<0){
					fprintf(stderr, "Send failed: %s\n", strerror(errno));
			}
			break;
			}
		/** data packet that needs to be forwarded to remote destination */
		case 5:{
		//If PC must be converted to 2D (MCU), send to transcodgingThread
			if (CONVERT_TO_2D == 0){ 
				msg->destThread = 2;
				writeMessageToThread(v_to_transcodingThread, m_to_transcodingThread, c_to_transcodingThread, (void*) msg);
				msgforwarded=true;
			}else{
		//If packets must be simply forwarded (SFU)	
				struct sockaddr_in* senderIP = msg->senderIP;
				struct sockaddr_in*  destIP = msg->destIP;
				char ip[INET_ADDRSTRLEN];
				inet_ntop (AF_INET, &(destIP->sin_addr), ip, sizeof (ip));
				uint16_t port = htons (destIP->sin_port);
				char* payload = msg->buffer;// + sizeof(char) + sizeof(sockaddr_in);
				unsigned payload_len = msg->bufferSize;// - sizeof(char) - sizeof(sockaddr_in);
				if (sendto(serverSocket, payload, payload_len, MSG_CONFIRM, (const struct sockaddr *) destIP, sizeof(*destIP))<0){
					fprintf(stderr, "Send failed: %s\n", strerror(errno));
				}		
				auto now_t = std::chrono::system_clock::now();
				std::chrono::duration<double> switching_seconds = now_t-msg->creation_date;
				thread_safe_print("T3 switching_ms %f \n", switching_seconds.count()*1000);
				}
			break;
			}
		case 10: {
			printf("Got controller command: reset\n");
			resetState();
			sendBackState(msg->senderIP);
			break;
			}
		case 11: {		
			printf("Got controller command: mcu\n");
			CONVERT_TO_2D = 0;
			resetState();
			sendBackState(msg->senderIP);
			break;
			}
		case 12: {
			printf("Got controller command: sfu\n");
			CONVERT_TO_2D = 1;
			resetState();
			sendBackState(msg->senderIP);
			break;
			}
		case 13: {
			printf("Got controller command: threads\n");
			int treadnum_tmp = (int)*(msg->buffer);
			NUM_OF_ENCODING_THREADS = std::max( 1 , treadnum_tmp);
			NUM_OF_ENCODING_THREADS = std::min( 16 , treadnum_tmp);
			resetState();
			sendBackState(msg->senderIP);
			break;
			}
		case 14: {
			printf("Got controller command: JPG\n");
			int JPEG_QUALITY_tmp = (int)*(msg->buffer);
			JPEG_QUALITY = std::max( 1 , JPEG_QUALITY_tmp);
			JPEG_QUALITY = std::min( 100 , JPEG_QUALITY_tmp);
			resetState();
			sendBackState(msg->senderIP);
			break;
			}
		case 15: {
			printf("Got controller command: state\n");
			sendBackState(msg->senderIP);
			break;
			}
		case 16: {
			printf("Got controller command: streams\n");
			int treadnum_tmp = (int)*(msg->buffer);
			NUM_OF_STREAMS = std::max( 1 , treadnum_tmp);
			NUM_OF_STREAMS = std::min( (int)MAX_NUM_OF_STREAMS , treadnum_tmp);
			resetState();
			sendBackState(msg->senderIP);
			break;
			}
		default:{
			printf("WARNING::Unknown message type: %u.\n", msg->MessageType);
			}
		}//end switch
		if (!msgforwarded){
			delete msg;
		}
	}//while (true)
}


/**
 * starts threads, receives packets and pushes them to the forwarding thread.
 **/
int main(void) {
	
	/** Setup UDP server */
	sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(LISTENING_PORT);
    serverAddress.sin_addr.s_addr = INADDR_ANY;

	bind(serverSocket, (struct sockaddr*)&serverAddress, sizeof(serverAddress));
    // listening to the assigned socket
	printf("Waiting for requests.\n");

	/** start processing thread */
	pthread_t procThread;
	pthread_create (&procThread, NULL, handlerThread, (void*) NULL);
	pthread_t transThread;
	pthread_create (&transThread, NULL, transcodingThread, (void*) NULL);
	pthread_t decompThread;
	pthread_create (&decompThread, NULL, decompressingThread, (void*) NULL);
	pthread_t senderThread;
	pthread_create (&senderThread, NULL, sendingThread, (void*) NULL);

	char* buffer = new char [9999999]; //store the chunks of the frame
	char receivedPacket[1500] = {0};
	unsigned CHUNK_SIZE = 1400;
	
	struct sockaddr_in cliaddr; 
	socklen_t len = sizeof(cliaddr);
	
	while (true){
		//receive a packet and send it to the forwarding thread
		int recChunkLen = recvfrom(serverSocket, receivedPacket, 1500 , MSG_WAITALL, (struct sockaddr *) &cliaddr, &len);
		MessageToThread *msg;
		//remove MCU header (message type+senderIP) from buffer and add it as attribute(s) to msg
		uint8_t type = *(receivedPacket); //copy message Type
		if (type  == 5 ){ //DATA PLANE has type and destIP in the header
			msg = new MessageToThread(receivedPacket+sizeof(MESSAGE_TYPE)+sizeof(cliaddr), recChunkLen-sizeof(MESSAGE_TYPE)-sizeof(cliaddr)); //write received packet's "payload"
			memcpy(msg->destIP, receivedPacket+sizeof(MESSAGE_TYPE), sizeof(cliaddr)); //copy destIP to msg
		}else{ //CONTROL PLANE has only type in the header
			msg = new MessageToThread(receivedPacket+sizeof(MESSAGE_TYPE), recChunkLen-sizeof(MESSAGE_TYPE)); //write received packet's "payload"
		}
		memcpy(msg->senderIP, &cliaddr, len); // copy sender's IP
		msg->MessageType = *(receivedPacket); //copy message Type
		msg->destThread = 1;
		writeMessageToThread(v_to_handlerThread, m_to_handlerThread, c_to_handlerThread, (void*) msg);
	}//END WHILE LOOP
		
    return 0;
}//end_main
