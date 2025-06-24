// This file includes portions of source code originally licensed under the Apache License 2.0:
// Copyright(c) 2015-2017 Intel Corporation. All Rights Reserved.
// 
// Modifications and additional code by mmlab-AUEB, 2025.
//
// -----------------------------------------------------------------------------
//
// Additional code in this file is licensed under the GNU General Public License v3.0:
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
/**
 * compile/execute command: reset && CAPP=producer_multithreaded;  g++ ${CAPP}.cpp -o $CAPP -lrealsense2 -lglfw -lGL -lGLU -ldraco -ljpeg -lpthread -g  && ./${CAPP} > /tmp/producer_$(date +%F-%T)
 * Results are stored in "/tmp/producer_$(date +%F-%T)"
 * 
 * to include FEC add -I/usr/local/include/aff3ct-3.0.2-152-g60b147a/aff3ct/ -I/usr/local/include/aff3ct-3.0.2-152-g60b147a/cli/ -I/usr/local/include/aff3ct-3.0.2-152-g60b147a/streampu -I/usr/local/include/aff3ct-3.0.2-152-g60b147a/rang -I/usr/local/include/aff3ct-3.0.2-152-g60b147a/MIPP -laff3ct-3.0.2-152-g60b147a 
*/

#include <librealsense2/rs.hpp> // Include RealSense Cross Platform API
#include "./extra/rs_extra/example.hpp"          // Include short list of convenience functions for rendering
#include "./extra/helper.hpp"          // Include short list of convenience functions for rendering

#include <algorithm>            // std::min, std::max
#include "draco/core/decoder_buffer.h"
#include "draco/mesh/mesh.h"
#include "draco/point_cloud/point_cloud.h"
#include <draco/compression/encode.h>
#include "draco/point_cloud/point_cloud_builder.h"
#include "draco/compression/point_cloud/point_cloud_kd_tree_encoder.h"

#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h> // inet_addr()

#include <iostream>
#include <fstream>
#include <unistd.h>
#include <netinet/tcp.h>

#include <mutex>
#include <cstdarg>
#include <jpeglib.h>

#define ENABLE_HOLE_PUNCHING 1 //0 true, 1 false
#define ENABLE_SFU 1 //0 true, 1 false

#define ENABLE_FRAME_HASHING 0 //0 true, 1 false 
#define ENABLE_DETAILED_TIMING 0 //0 true, 1 false

#define ENABLE_BnW 1 //0 true, 1 false 
#define COLOR_MODE 0 //0 for sending RGB for each point, 1 for sending color frame and textures

#define ENABLE_FEC 1 //0 true, 1 false 
#define STOP_AT 500 //exit after sending N packets
#define DROP_POINTS_RATE 2 //Drops 1 of N points
#define EXCLUDE_A_COLOUR 1 //0 true, 1 false
int DRACO_COMPRESSION_LEVEL = 4;//I think 1 is the fastest, 9 reduces size the most
int QUANTIZATION_BITS = 16; //16 is default
unsigned NUM_OF_ENCODING_THREADS = 4;

#if ENABLE_FEC == 0
	#include "/usr/local/include/aff3ct-3.0.2-152-g60b147a/aff3ct/aff3ct.hpp"
#endif

std::string HOLE_PUNCHING_SIGN_SRV =  "195.251.234.16";
std::string CONSUMER_IP =  "192.168.1.210";
unsigned HOLE_PUNCHER_PORT = 8888;
unsigned CONSUMER_PORT = 5555;

sockaddr_in consumerAddress; //address received by UHP server
sockaddr_in UHPAddress;

void register_glfw_callbacks(window& app, glfw_state& app_state);

void print_config(){
	printf("Config: ");	
	#if ENABLE_FRAME_HASHING == 0 
	printf("ENABLE_FRAME_HASHING ");
	#endif
	#if ENABLE_HOLE_PUNCHING == 0
	printf("ENABLE_HOLE_PUNCHING ");
	#endif
	#if ENABLE_SFU == 0 
	printf("ENABLE_SFU ");
	#endif
	#if ENABLE_DETAILED_TIMING  == 0
	printf("ENABLE_DETAILED_TIMING ");
	#endif
	#if ENABLE_BnW == 0 
	printf("ENABLE_BnW ");
	#endif
	#ifdef COLOR_MODE 
	printf("COLOR_MODE_%u ", COLOR_MODE);
	#endif
	#if ENABLE_FEC == 0
	printf("ENABLE_FEC ");
	#endif
	#ifdef DROP_POINTS_RATE 
	printf("DROP_POINTS_RATE_%u ", DROP_POINTS_RATE);
	#endif
	#if EXCLUDE_A_COLOUR == 0 
	printf("EXCLUDE_A_COLOUR ");
	#endif
	#ifdef STOP_AT 
	printf("STOP_AT_%u ", STOP_AT);
	#endif

	printf("DRACO_COMPRESSION_LEVEL_%u ", DRACO_COMPRESSION_LEVEL);
	printf("QUANTIZATION_BITS_%u ", QUANTIZATION_BITS);
	printf("NUM_OF_ENCODING_THREADS_%u ", NUM_OF_ENCODING_THREADS);
	printf("\n");
	}
	
//variables for inter-thread communication
std::vector<void*>  v_to_senderThread;
pthread_mutex_t m_to_senderThread = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t c_to_senderThread = PTHREAD_COND_INITIALIZER;

/**
 * Thread function that reads buffer (of pointcloud) and pushes it to the network. 
 * A simple transport protocol is used to mark the different chunks and allow pointcloud reconstruction at the other end.
 * */
void *sendingThread(void* ){
	int clientSocket = socket(AF_INET, SOCK_DGRAM, 0);

	int chunkId;
	unsigned frameId=0;
	while (true){
		std::string log="";
	    MessageToThread* msg = (MessageToThread*)readMessageFromThread(v_to_senderThread, m_to_senderThread, c_to_senderThread);
	    void * bufferToSend = (void*) msg->buffer; //thats the pointcloud
	    unsigned bufferToSend_len = msg->bufferSize;
		
		auto start = std::chrono::system_clock::now();
		/// send buffer to remote musician
		unsigned CHUNK_SIZE = 1400;	
		unsigned index = 0;
	#if ENABLE_SFU == 0	
		///add pre-header for SFU: <TYPE><sockaddr_in><Payload>
		char* chunkToSend = (char*)malloc(sizeof(char)/*type*/+ sizeof(sockaddr_in)+ sizeof(frameId)+sizeof(chunkId)+CHUNK_SIZE);	
		char type = 5;
		memcpy(chunkToSend+index, &type, sizeof(char));	index+=sizeof(char);
		memcpy(chunkToSend+index, &consumerAddress, sizeof(sockaddr_in)); index+=sizeof(sockaddr_in);

		char ip[INET_ADDRSTRLEN];
		inet_ntop (AF_INET, &(consumerAddress.sin_addr), ip, sizeof (ip));
		uint16_t port = htons (consumerAddress.sin_port);
		printf ("Request to forward to consumer IP %s:%d (SFU header size: %d)\n", ip, port, index);
	#else
		char* chunkToSend = (char*)malloc(sizeof(frameId)+sizeof(chunkId)+CHUNK_SIZE);	
	#endif
		/** Toy transmission protocol: [<frame id, 4bytes>, <chunk id, 4bytes>, <1400bytes content>] */
		////fragment buffer localy and send data -- not required, but usefull for transisioning to UDP
		//write frameId
		memcpy(chunkToSend+index, &frameId, sizeof(frameId));
		chunkId = 0;
		unsigned curChunksize = 0;
		unsigned sentBytes = 0;
		for (unsigned i=0; i<bufferToSend_len; ){
			//write chunkId
			memcpy(chunkToSend+index+sizeof(frameId), &chunkId, sizeof(chunkId));
			curChunksize = std::min((unsigned)(bufferToSend_len - i), CHUNK_SIZE);
			memcpy(chunkToSend+index+sizeof(frameId)+sizeof(chunkId), bufferToSend+i, curChunksize);
		#if ENABLE_SFU == 0	
			if (sendto(clientSocket, chunkToSend, index+sizeof(frameId)+sizeof(chunkId)+curChunksize, MSG_CONFIRM, (const struct sockaddr *) &UHPAddress, sizeof(UHPAddress))<0){
				fprintf(stderr, "send failed: %s\n", strerror(errno));}
		#else
			if (sendto(clientSocket, chunkToSend, sizeof(frameId)+sizeof(chunkId)+curChunksize, MSG_CONFIRM, (const struct sockaddr *) &consumerAddress, sizeof(consumerAddress))<0){
				fprintf(stderr, "send failed: %s\n", strerror(errno));}
		#endif
			i+=curChunksize;
			chunkId++;
		#if ENABLE_SFU == 0	
			sentBytes+=curChunksize+sizeof(frameId)+sizeof(chunkId)+index;
		#else
			sentBytes+=curChunksize+sizeof(frameId)+sizeof(chunkId);
		#endif
		}	
		free(chunkToSend);	
		
		#if ENABLE_DETAILED_TIMING == 0
			auto transmission_end = std::chrono::system_clock::now();
			std::chrono::duration<double> transmission_seconds = transmission_end-start;
			thread_safe_print("T2 Frame %u Chunks %u Tx_b %u Pointcloud_b %lu Transmission_ms %f \n", frameId, chunkId, sentBytes, bufferToSend_len, transmission_seconds.count()*1000);
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

/** struct to group parameters for arguments of encoder thread. Data and data_len are updated by the thread. */
struct EncoderThreadArgs{
	unsigned num_points;
	const rs2::vertex* vertices;
	const rs2::texture_coordinate* textures;
	const uint8_t* RGBs;
	char * data;
	unsigned data_len;
	};
/**
 * Generates point cloud from vertices and vectors (of realsense lib). The function returns the compressed pointcloud .
 * */ 
void* dracoEncoderThread(void * _encoderThreadArgs) {
		EncoderThreadArgs* encoderThreadArgs = (EncoderThreadArgs*) _encoderThreadArgs;
		
		unsigned _num_points = encoderThreadArgs->num_points;
		const rs2::vertex* _vertices = encoderThreadArgs->vertices;
		const rs2::texture_coordinate* _textures = encoderThreadArgs->textures; 
		const uint8_t* _rgbs = encoderThreadArgs->RGBs; 
		
		// Create Draco point cloud
		draco::PointCloudBuilder builder;
		// Initialize the builder for a given number of points (required).
		builder.Start(_num_points);
		const int pos_att_id = builder.AddAttribute(draco::GeometryAttribute::POSITION, 3, draco::DT_FLOAT32); 
	#if ENABLE_BnW == 1 // do include colour attribute/texture
		#if COLOR_MODE == 0 //send RGB for each point
		const int color_att_id = builder.AddAttribute(draco::GeometryAttribute::COLOR, 3, draco::DT_UINT8);
		#else  //send 2D texture (and color frame)
		const int texture_att_id = builder.AddAttribute(draco::GeometryAttribute::TEX_COORD, 2, draco::DT_UINT8);
		#endif
	#endif
	
		uint32_t ii=0;
		for (draco::PointIndex i(0); i < _num_points; ++i) {				
			builder.SetAttributeValueForPoint(pos_att_id, i, _vertices[ii]);
		#if ENABLE_BnW == 1
			#if COLOR_MODE == 0 //send RGB for each point
			uint8_t tmprgb[3] = {_rgbs[ii*3], _rgbs[ii*3+1],_rgbs[ii*3+2]};
			builder.SetAttributeValueForPoint(color_att_id, i, &tmprgb[0]);
			#else // send textures
			builder.SetAttributeValueForPoint(texture_att_id, i, _textures[ii]);
			#endif
		#endif
			ii++;
		#ifdef DROP_POINTS_RATE
			if (ii % DROP_POINTS_RATE == 0){
				ii++; //skip that point
			}
		#endif
		}
		
		// Get the final PointCloud.
		constexpr bool deduplicate_points = false;
		std::unique_ptr<draco::PointCloud> draco_pc = builder.Finalize(deduplicate_points);
		//encode with Draco			
		draco::EncoderBuffer enc_buffer;
		draco::PointCloudKdTreeEncoder encoder;
		draco::EncoderOptions options = draco::EncoderOptions::CreateDefaultOptions();
	
		options.SetGlobalInt("quantization_bits", QUANTIZATION_BITS);
		options.SetSpeed(10 - DRACO_COMPRESSION_LEVEL, 10 - DRACO_COMPRESSION_LEVEL);
		encoder.SetPointCloud(*draco_pc);
			
		encoder.Encode(options, &enc_buffer);
		
		encoderThreadArgs->data = (char*) malloc(enc_buffer.size());
		memcpy (encoderThreadArgs->data, enc_buffer.data(), enc_buffer.size());
		encoderThreadArgs->data_len = enc_buffer.size();
		return encoderThreadArgs;
	}

int main(int argc, char * argv[]) try
{
	printf("Notice: If loss is significant consider increased the kernel UDP buffer, e.g., $sudo sysctl -w net.core.rmem_default=99999999 \n");
	print_config();
	/** Start sender socket	 */
	int clientSocket = socket(AF_INET, SOCK_DGRAM, 0);

#if ENABLE_HOLE_PUNCHING == 0
	
	UHPAddress.sin_family = AF_INET;
	UHPAddress.sin_port = htons(HOLE_PUNCHER_PORT);
	
	UHPAddress.sin_addr.s_addr = inet_addr(HOLE_PUNCHING_SIGN_SRV.c_str());
	char type = 1;	 
	if (sendto(clientSocket, &type, sizeof(type), MSG_CONFIRM, (const struct sockaddr *) &UHPAddress,  sizeof(UHPAddress))<0){
		fprintf(stderr, "Request to UDP hole puncking server failed: %s\n", strerror(errno));
		exit(1);
	}
	printf("Sent request to UDP hole punching server\n");
		
	struct sockaddr server_addr;
	socklen_t len = sizeof(server_addr);

	char recv_buffer[1500];
	int rcv_bytes = recvfrom(clientSocket, recv_buffer, sizeof(recv_buffer), 0, (struct sockaddr *)&server_addr, &len);
	if (rcv_bytes < 0) {  perror("Receive from UDP hole punching server failed\n");
		close(clientSocket);
		return 1;
	}
	if (recv_buffer[0] == 2){
		printf("No consumers registered yet. Exiting.\n");
		close(clientSocket);
		return 0;
	}else if (recv_buffer[0] == 3){
		memcpy(&consumerAddress, recv_buffer+sizeof(char)+sizeof(char), sizeof(sockaddr_in));
		char ip[INET_ADDRSTRLEN];
		inet_ntop (AF_INET, &(consumerAddress.sin_addr), ip, sizeof (ip));
		uint16_t port = htons (consumerAddress.sin_port);
		printf ("Got consumer %s:%d\n", ip, port);
	}else{
		printf("Unkown message type. Exiting.\n");
		exit(1);
	}
#else
	consumerAddress.sin_family = AF_INET;
	consumerAddress.sin_port = htons(CONSUMER_PORT);
	consumerAddress.sin_addr.s_addr = inet_addr(CONSUMER_IP.c_str());
#endif
	/**
	 * Start sender thread
	 * */
	//start thread that decodes and renders the point cloud
	pthread_t senderThread;
	pthread_create (&senderThread, NULL, sendingThread, NULL);

	/**
	 * Start realsense
	 */
    // Declare pointcloud object, for calculating pointclouds and texture mappings
    rs2::pointcloud pc; ///class defined in librealsense/include/librealsense2/hpp/rs_processing.hpp
    // We want the points object to be persistent so we can display the last cloud when a frame drops
    rs2::points points;
    // Declare RealSense pipeline, encapsulating the actual device and sensors
    rs2::pipeline pipe;
    // Start streaming with default 
    pipe.start();

	thread_safe_print("Started pipeline with camera.\n");

	unsigned frameId = 0;
    while (true) // 
    {
		std::string logText;
		////////////// timer start 
		auto start = std::chrono::system_clock::now();
		auto last_end = start;
        // Wait for the next set of frames from the camera
        auto frames = pipe.wait_for_frames();
        auto color = frames.get_color_frame(); //returns a video_frame instance
     #if ENABLE_BnW == 1  //prepare to send the color frame 
        const char* color_data = reinterpret_cast<const char*>(color.get_data());
        unsigned color_data_size = color.get_data_size();
     #endif   
        void* bufferToSend;
        unsigned bufferToSend_len;
		
	#if ENABLE_DETAILED_TIMING == 0
		auto capturing_end = std::chrono::system_clock::now();
		std::chrono::duration<double> capturing_seconds = capturing_end-start;
		last_end = capturing_end;
		logText+= "Capture_ms "+ std::to_string(capturing_seconds.count()*1000) + " ";
	#endif		
        // For cameras that don't have RGB sensor, we'll map the pointcloud to infrared instead of color
        if (!color)
            color = frames.get_infrared_frame();
        // Tell pointcloud object to map to this color frame
        pc.map_to(color);
        auto depth = frames.get_depth_frame();
        // Generate the pointcloud and texture mappings
        points = pc.calculate(depth);
      
		const auto vertices = points.get_vertices();
	#if ENABLE_BnW == 1 
		const auto textures = points.get_texture_coordinates();
		#if COLOR_MODE == 0 //get RGB for each point in pointcloud
		uint8_t* RGBs = (uint8_t*) malloc(points.size()*3); // 3 bytes per point 
		auto tex_coords = points.get_texture_coordinates(); // UV coordinates

		// Access the color frame data
		int color_width = color.get_width();
		int color_height = color.get_height();
		int bytes_per_pixel = color.get_bytes_per_pixel(); // Should be 3 for RGB or 4 for RGBA

		for (int i = 0; i < points.size(); ++i) {
			const rs2::vertex& v = vertices[i];        // 3D point
			const rs2::texture_coordinate& uv = tex_coords[i]; // Texture UV

			// Convert normalized UV [0,1] to image coordinates
			int x = static_cast<int>(uv.u * color_width);
			int y = static_cast<int>(uv.v * color_height);

			// Check bounds
			if (x < 0 || x >= color_width || y < 0 || y >= color_height)
				continue;

			int idx = (y * color_width + x) * bytes_per_pixel;

			RGBs[i*3] = color_data[idx];
			RGBs[i*3+1] = color_data[idx + 1];
			RGBs[i*3+2] = color_data[idx + 2];
			//printf("r %d g %d b %d \n", RGBs[i*3], RGBs[i*3+1], RGBs[i*2]);
		}
		#endif
	#endif
	
	#ifdef DROP_POINTS_RATE
		int num_points = points.size() - points.size()/DROP_POINTS_RATE;
	#else
		int num_points = points.size();
	#endif	
		logText+= "#Points " + std::to_string(num_points) + " ";
	#if ENABLE_DETAILED_TIMING == 0
		auto pointcl_end = std::chrono::system_clock::now();
		std::chrono::duration<double> pointcl_seconds = pointcl_end - last_end;
		last_end = pointcl_end;
		logText+="Pointcloud_ms "+  std::to_string(pointcl_seconds.count()*1000) + " ";
	#endif	
		//// Create Draco point cloud using multiple threads
		pthread_t threads[NUM_OF_ENCODING_THREADS];
		
		EncoderThreadArgs* argsArray[NUM_OF_ENCODING_THREADS];
		unsigned points_per_thread = num_points/NUM_OF_ENCODING_THREADS;
		for (int i = 0; i < NUM_OF_ENCODING_THREADS; i++) {
			argsArray[i]= (EncoderThreadArgs*)malloc (sizeof (EncoderThreadArgs));
			argsArray[i]->num_points = points_per_thread;
			argsArray[i]->vertices = &vertices[points_per_thread*i];
		#if ENABLE_BnW == 1
			#if COLOR_MODE == 1 //add texture
			argsArray[i]->textures = &textures[points_per_thread*i];
			#elif COLOR_MODE == 0 //add RGB
			argsArray[i]->RGBs = &RGBs[points_per_thread*i*3];
			#endif
		#endif
		}

		for (int i = 0; i < NUM_OF_ENCODING_THREADS; i++) {
			pthread_create(&threads[i], nullptr, dracoEncoderThread, (void*)argsArray[i]);
		}

		//wait for threads and estimate total size of subpointclouds
		bufferToSend_len=0;
		for (int i = 0; i < NUM_OF_ENCODING_THREADS; i++) {
			pthread_join(threads[i], nullptr);
			bufferToSend_len += argsArray[i]->data_len;
		}
		//unify subpointclouds: <pointcloudLen><pointcloud_data><pointcloudLen><pointcloud_data>...
		//also add JPEG in the beginning
		unsigned index  = 0;
		#if ENABLE_BnW == 1 && COLOR_MODE == 1 //send color frame
			bufferToSend = (char*) malloc (sizeof(unsigned)/*color frame size*/ + color_data_size + bufferToSend_len + sizeof(unsigned)*NUM_OF_ENCODING_THREADS);
			memcpy(bufferToSend+index, &color_data_size, sizeof (color_data_size));
			index+=sizeof(color_data_size);
			memcpy(bufferToSend+index, color_data, color_data_size);
			index+=color_data_size;
		#else
			//malloc space to unify subpointclouds (dont include color frame)
			bufferToSend = (char*) malloc (bufferToSend_len + sizeof(unsigned)*NUM_OF_ENCODING_THREADS);
		#endif
		
		for (int i = 0; i < NUM_OF_ENCODING_THREADS; i++) {
			memcpy(bufferToSend + index, &argsArray[i]->data_len, sizeof(unsigned));
			index += sizeof(unsigned);
			memcpy(bufferToSend + index, argsArray[i]->data, argsArray[i]->data_len);
			index += argsArray[i]->data_len;
			free(argsArray[i]->data);
			free(argsArray[i]);
		}
		bufferToSend_len = index;
		
		#if ENABLE_BnW == 1 && COLOR_MODE ==0
		free (RGBs);
		#endif 
		
	#if ENABLE_DETAILED_TIMING == 0
		auto dracoenc_end = std::chrono::system_clock::now();			
		std::chrono::duration<double> draco_seconds = dracoenc_end - last_end ;
		last_end = dracoenc_end;
		logText+="DracoEnc_ms "+ std::to_string(draco_seconds.count()*1000) + " ";
	#endif
		
	#if ENABLE_FEC == 0			
		unsigned size_in_bytes = bufferToSend_len;//*8;
		std::unique_ptr<aff3ct::module::Encoder_repetition_sys<int>> fec_encoder = std::unique_ptr<aff3ct::module::Encoder_repetition_sys<int>>( new aff3ct::module::Encoder_repetition_sys<int>(size_in_bytes, size_in_bytes*2)); 
			
   	    int* bits_array = (int*)malloc(size_in_bytes*sizeof (int));
   	    memcpy (bits_array, (int*)bufferToSend, bufferToSend_len); 
		
		int* with_fec_array = (int*) malloc(sizeof (int)*size_in_bytes*2);
		fec_encoder->encode(bits_array, with_fec_array);
		free (bits_array);
		bufferToSend = with_fec_array;
		bufferToSend_len = size_in_bytes*2;
	
	#if ENABLE_DETAILED_TIMING == 0
		auto fec_end = std::chrono::system_clock::now();
		std::chrono::duration<double> fec_seconds = fec_end - last_end ;
		last_end = fec_end;
		logText+="FEC_ms "+ std::to_string(fec_seconds.count()*1000) + " ";		
	#endif
		
	#endif		
		/** write frame to senderThread */
		MessageToThread *msg = new MessageToThread((const char*)bufferToSend, bufferToSend_len);
		writeMessageToThread(v_to_senderThread, m_to_senderThread, c_to_senderThread, (void*) msg);
		
	#if ENABLE_FRAME_HASHING == 0
		logText+="Hash "+ std::to_string(hash_memory(bufferToSend, bufferToSend_len)) + " ";
	#endif		
	
		free(bufferToSend);
		
 		auto end = std::chrono::system_clock::now();
		std::chrono::duration<double> elapsed_seconds = end-start;
		std::time_t end_time = std::chrono::system_clock::to_time_t(end);
 
		logText+="Interframe_ms "+ std::to_string(elapsed_seconds.count()*1000) + " ";
		thread_safe_print("T1 Frame %d %s \n", frameId++, logText.c_str());
		
	#if ENABLE_FEC == 0			
		//clean memory
		free(with_fec_array);
	#endif

	
	
    } /// end loop
    
    //std::close(clientSocket);

    return EXIT_SUCCESS;
} /// end main
catch (const rs2::error & e)
{
    std::cerr << "RealSense error calling " << e.get_failed_function() << "(" << e.get_failed_args() << "):\n    " << e.what() << std::endl;
    return EXIT_FAILURE;
}
catch (const std::exception & e)
{
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
}

