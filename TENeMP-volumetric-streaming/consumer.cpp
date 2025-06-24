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
 * compile/execute command: reset && CAPP2=consumer;  g++ ${CAPP2}.cpp -o $CAPP2 -lrealsense2 -lglfw -lGL -lGLU -ldraco -lpthread  -g  && ./${CAPP2} > /tmp/consumer_$(date +%F-%T)
 * Results are stored in "/tmp/consumer_$(date +%F-%T)"
 * */

#include <librealsense2/rs.hpp> // Include RealSense Cross Platform API
#include "./extra/rs_extra/example.hpp"   // Include short list of convenience functions for rendering
#include "./extra/helper.hpp"          // Include short list of convenience functions for rendering

#include <algorithm>            // std::min, std::max
#include "draco/core/decoder_buffer.h"
#include "draco/mesh/mesh.h"
#include "draco/point_cloud/point_cloud.h"
#include <draco/compression/encode.h>
#include "draco/point_cloud/point_cloud_builder.h"
#include "draco/io/ply_decoder.h"
#include "draco/compression/point_cloud/point_cloud_kd_tree_decoder.h"

#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h> // inet_addr()
#include <pthread.h>
#include <iostream>
#include <fstream>
#include <netinet/tcp.h>
#include <cstdlib>
#include <jpeglib.h>

#define ENABLE_HOLE_PUNCHING 1//0 true, 1 false
#define ENABLE_SFU 1 //0 true, 1 false

#define ENABLE_UI 2//0 for realsense, 1 for draco, 2 for nothing
#define ENABLE_BnW 1 //0 true, 1 false
#define COLOR_MODE 0 //0 for sending RGB for each point, 1 for sending color frame and textures

#define ENABLE_FRAME_HASHING 0//0 true, 1 false; just for debugging, not automated
#define ENABLE_DETAILED_TIMING 0 //0 true, 1 false
#define ENABLE_FEC 1 //0 true, 1 false 

#if ENABLE_FEC == 0
	#include "/usr/local/include/aff3ct-3.0.2-152-g60b147a/aff3ct/aff3ct.hpp"
#endif

std::string HOLE_PUNCHING_SIGN_SRV = "195.251.234.16";
unsigned HOLE_PUNCHER_PORT = 8888;
unsigned LISTENING_PORT = 5555;
unsigned NUM_OF_ENCODING_THREADS = 4;
unsigned CHUNK_SIZE = 1400;

void print_config(){
	printf("Config: ");	
	#if ENABLE_UI == 0 
	printf("ENABLE_UI ");
	#endif
	#if ENABLE_BnW == 0
	printf("ENABLE_BnW ");
	#endif
	#if ENABLE_FRAME_HASHING == 0
	printf("ENABLE_FRAME_HASHING ");
	#endif
	#if ENABLE_HOLE_PUNCHING == 0
	printf("ENABLE_HOLE_PUNCHING ");
	#endif
	#if ENABLE_DETAILED_TIMING == 0
	printf("ENABLE_DETAILED_TIMING ");
	#endif
	#if ENABLE_FEC == 0
	printf("ENABLE_FEC ");
	#endif
	printf("NUM_OF_ENCODING_THREADS_%u ", NUM_OF_ENCODING_THREADS);

	printf("\n");
	}

// Helper functions
void register_glfw_callbacks(window& app, glfw_state& app_state);

// Handles all the OpenGL calls needed to display the point cloud
void draw_draco_pointcloud(float width, float height, glfw_state& app_state, const draco::PointCloud* points)
{
	 if (!points)
        return;
    // OpenGL commands that prep screen for the pointcloud
    glLoadIdentity();
    glPushAttrib(GL_ALL_ATTRIB_BITS);

    glClearColor(153.f / 255, 153.f / 255, 153.f / 255, 1);
    glClear(GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    gluPerspective(60, width / height, 0.01f, 10.0f);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    gluLookAt(0, 0, 0, 0, 0, 1, 0, -1, 0);

    glTranslatef(0, 0, +0.5f + app_state.offset_y * 0.05f);
    glRotated(app_state.pitch, 1, 0, 0);
    glRotated(app_state.yaw, 0, 1, 0);
    glTranslatef(0, 0, -0.5f);

    glPointSize(width / 640);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, app_state.tex.get_gl_handle());
    float tex_border_color[] = { 0.8f, 0.8f, 0.8f, 0.8f };
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, tex_border_color);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F); // GL_CLAMP_TO_EDGE
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F); // GL_CLAMP_TO_EDGE
    glBegin(GL_POINTS);
    
	///get points of pointcloud
	float tmppoint [3];
	uint8_t tmptexture [2];
	uint8_t tmpcolor [3];
	unsigned k =0;
	for (draco::PointIndex i(0); i < points->num_points(); ++i) {
		#if ENABLE_BnW == 1 
			#if COLOR_MODE == 1 //texture is added in points
		points->attribute(1)->GetMappedValue(i, tmptexture); //texture
		glTexCoord2fv((float*)tmptexture); 	
			#else // RGB is added in points
		points->attribute(1)->GetMappedValue(i, tmpcolor); //texture
		float color[3] = { tmpcolor[0] / 255.0f, tmpcolor[1] / 255.0f, tmpcolor[2] / 255.0f };
		glColor3fv(color);
			#endif
		#endif
		
		points->attribute(0)->GetMappedValue(i, tmppoint); //coords
        if (tmppoint[3]!=1){ //if there is depth information
			glVertex3fv(tmppoint);
		}
	}	//exit (1);
	
	// OpenGL cleanup
    glEnd();
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glPopAttrib();
}
//variables for inter-thread communication
std::vector<void*>  v_to_renderThread;
pthread_mutex_t m_to_renderThread = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t c_to_renderThread = PTHREAD_COND_INITIALIZER;



void *renderingThread(void* ptr){
#if ENABLE_UI != 2
	// Create a simple OpenGL window for rendering:
	window app(1280, 720, "TeNEMP Pointcloud");
	// Construct an object to manage view state
	glfw_state app_state;
	// register callbacks to allow manipulation of the pointcloud
	register_glfw_callbacks(app, app_state);
#endif

	//draco objects
	draco::DecoderOptions dec_options;	
	unsigned counter = 0;
#if ENABLE_UI != 2
    while (app){
#else 
	while (true){
#endif
		std::string log="";
	    MessageToThread* msg = (MessageToThread*)readMessageFromThread(v_to_renderThread, m_to_renderThread, c_to_renderThread);
	    char * buffer = msg->buffer; //thats our pointcloud
	    unsigned bufferSize = msg->bufferSize;
	    if (bufferSize < CHUNK_SIZE) { free(buffer); continue;} //when consumer starts in the middle of transmission, 1st frame will be zero bytes.. @TODO: find the source of this issue..
	#if ENABLE_FRAME_HASHING == 0
		log+="Hash " + std::to_string(hash_memory(buffer,  bufferSize))+ " ";
	#endif
		unsigned sub_bufferSize;
		char* sub_buffer;
		unsigned index = 0;
	#if ENABLE_BnW == 1 && COLOR_MODE == 1
		//first read the JPG frame
		int color_data_size, width, height, channels ;
		width = 1280;
		height = 720;
		memcpy(&color_data_size, buffer+index, sizeof(color_data_size));
		index+=sizeof(color_data_size);
		char* color_data = (char*) malloc (color_data_size);
		if (color_data_size >=bufferSize){ //in case of many loses, check that entire pc is received.. if not, skip rendering.
			thread_safe_print("T2 Signifficant loss. Can not render pointcloud.\n");
			continue;
		}
		memcpy(color_data, buffer+index, color_data_size);
		index+=color_data_size;
	
		#if ENABLE_UI != 2
		app_state.tex.upload_from_raw_data(color_data, 5, 1280, 720, 2, 0);
		#endif
		
		free (color_data);
	#endif	
		for (unsigned i = 0; i<NUM_OF_ENCODING_THREADS; i++){
			memcpy(&sub_bufferSize, buffer+index, sizeof(unsigned));
			index+=sizeof(unsigned); 
			sub_buffer = buffer+index;
			sub_bufferSize = std::min (sub_bufferSize, bufferSize-index); //in case of lost/corrupted packets, received size can be lower than exepcted, thus causing seg fault 
			index+= sub_bufferSize; 
			if (index > bufferSize) {
				thread_safe_print("Broken subpointcloud indexing. Skipping pointcloud. \n");
				break;
				}
			////decode frame	
			std::unique_ptr<draco::PointCloud> out_pc(new draco::PointCloud());
			auto capturing_end = std::chrono::system_clock::now();
			draco::DecoderBuffer dec_buffer;
			dec_buffer.Init(sub_buffer, sub_bufferSize);
			draco::PointCloudKdTreeDecoder decoder;
			decoder.Decode(dec_options, &dec_buffer, out_pc.get());
		#if ENABLE_DETAILED_TIMING == 0
			auto decoding_end = std::chrono::system_clock::now();
			std::chrono::duration<double> dracodec_seconds = decoding_end - capturing_end;
			log+="DracoDecoding_time " + std::to_string(dracodec_seconds.count()*1000)+ " ";
		#endif
			/** openGL */
		#if ENABLE_UI != 2
			// Draw the pointcloud		
			draw_draco_pointcloud(app.width(), app.height(), app_state, out_pc.get());
			//app();
			#if ENABLE_DETAILED_TIMING == 0
			auto rendering_end = std::chrono::system_clock::now();
			std::chrono::duration<double> render_seconds = rendering_end - decoding_end;
			log+= "Rendering_time "+ std::to_string(render_seconds.count()*1000)+ " ";
			#endif
		#endif
		}
		if (strcmp(log.c_str(), "")!=0){
			thread_safe_print("T2 %s\n", log.c_str());
		}	
		delete msg;
	}
}

int main(int argc, char * argv[]) {
	
	printf("Notice: If loss is significant consider increased the kernel UDP buffer, e.g., $sudo sysctl -w net.core.wmem_default=99999999\n");
	print_config();
	/**
	 * Start openGL UI
	 */
	//start thread that decodes and renders the point cloud
	pthread_t renderThread;
	pthread_create (&renderThread, NULL, renderingThread, (void*) NULL);
	/**
	 * Start server
	 */
	int serverSocket = socket(AF_INET, SOCK_DGRAM, 0);
		
	sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(LISTENING_PORT);
    serverAddress.sin_addr.s_addr = INADDR_ANY;

	bind(serverSocket, (struct sockaddr*)&serverAddress, sizeof(serverAddress));
    // listening to the assigned socket
	printf("Waiting for packets..\n");

	char* buffer = new char [9999999]; //store the chunks of the frame
	char receivedChunk[1500] = {0};
		
	struct sockaddr_in cliaddr; 
	socklen_t len = sizeof(cliaddr);
#if ENABLE_HOLE_PUNCHING == 0
	/**Register to UDP hole punching service */
	sockaddr_in UHPAddress{};
    UHPAddress.sin_port = htons(HOLE_PUNCHER_PORT);
	UHPAddress.sin_family = AF_INET;
	UHPAddress.sin_port = htons(HOLE_PUNCHER_PORT);
	
	UHPAddress.sin_addr.s_addr = inet_addr(HOLE_PUNCHING_SIGN_SRV.c_str());
	char type = 0;	 
	if (sendto(serverSocket, &type, sizeof(type), MSG_CONFIRM, (const struct sockaddr *) &UHPAddress,  sizeof(UHPAddress))<0){
		fprintf(stderr, "Request to UDP hole puncking server failed: %s\n", strerror(errno));
		exit(1);
	}
	printf("Sent request to UDP hole punching server\n");

#endif

	unsigned currentFrameId=0;
	unsigned receivedBytes = 0;
	unsigned pointCloudSize = 0;
	unsigned receivedChunks = 0;	

	auto start = std::chrono::system_clock::now();
	while (true){
		////////////// timer start 
		int recChunkLen = recvfrom(serverSocket, receivedChunk, CHUNK_SIZE + 2*sizeof(unsigned) , MSG_WAITALL, ( struct sockaddr *) &cliaddr, &len);
		if (recChunkLen ==0){ 
			printf("Received empty message\n"); 
			break;
		}else {
			/**
			* Toy transmission protocol: [<frame id, 4bytes>, <chunk id, 4bytes>, <1400bytes content>]
			* */
			//read frameId
			unsigned frameId =  *((unsigned int*)receivedChunk);
			//read chunkId
			unsigned chunkId = *((unsigned int*)(receivedChunk+sizeof(frameId)));
			if (frameId < currentFrameId){
				///got a late, reordered chunk, ignore it
				thread_safe_print("T1 Got late chunk of frame %u, while receiving frame %u. Ignoring it. \n", frameId, currentFrameId); 
				continue;
			}else if (frameId > currentFrameId){
				///got chunk for next frame, render current
				MessageToThread *msg = new MessageToThread(buffer, pointCloudSize);
				writeMessageToThread(v_to_renderThread, m_to_renderThread, c_to_renderThread, (void*) msg);
	
				if(frameId!=currentFrameId+1){
					printf("Warning: Serious reordering. Got chunk for frame %u.\n", frameId);
				}
									
				auto end = std::chrono::system_clock::now();
				std::chrono::duration<double> elapsed_seconds = end-start;
				std::time_t end_time = std::chrono::system_clock::to_time_t(end);
				start=end; //reset timer
		 
				thread_safe_print("T1 Frame %u Chunks %u Rx_b %u PointCloud_b %u Interframe_Transm_ms %f \n", currentFrameId, receivedChunks, receivedBytes, pointCloudSize, elapsed_seconds.count()*1000);
				auto start = std::chrono::system_clock::now();
				
				currentFrameId = frameId;
				receivedBytes = 0;
				pointCloudSize=0;
				receivedChunks = 0;
			}
			///got another chunk of the current frame
			//read payload
			memcpy(buffer+chunkId*CHUNK_SIZE, receivedChunk+sizeof(frameId)+sizeof(chunkId), recChunkLen-sizeof(frameId)-sizeof(chunkId));
			//// fragment buffer localy and send data -- not required, but usefull for transisioning to UDP
			receivedBytes+=recChunkLen;
			pointCloudSize += recChunkLen -sizeof(frameId)-sizeof(chunkId);
			receivedChunks++;
			}
		}
    return EXIT_SUCCESS;
}//end_main
