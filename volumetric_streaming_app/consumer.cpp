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
#include <thread>
#include <iostream>
#include <fstream>
#include <netinet/tcp.h>
#include <cstdlib>
#include <jpeglib.h>
#include <opencv2/opencv.hpp>


#define ENABLE_HOLE_PUNCHING 0//0 true, 1 false

constexpr uint8_t NUM_OF_STREAMS = 2; //the maximum number of received point cloud streams; tested up to 2.

#define ENABLE_UI 0 // 0 true, 1 false
#define ENABLE_BnW 1 //0 true, 1 false
#define COLOR_MODE 0 //0 for sending RGB for each point, 1 for sending color frame and textures

#define ENABLE_FRAME_HASHING 0 //0 true, 1 false; just for debugging, not automated
#define ENABLE_DETAILED_TIMING 0 //0 true, 1 false
#define ENABLE_FEC 1 //0 true, 1 false 

#define WRITE_JPG_TO_FILE 1 //0 for true, 1 for false //for debugging purposes

unsigned NUM_OF_ENCODING_THREADS = 8;

#if ENABLE_FEC == 0
	//#include "extra/aff3ct_extra/structs.hpp"
	#include "/usr/local/include/aff3ct-3.0.2-152-g60b147a/aff3ct/aff3ct.hpp"
#endif

std::string HOLE_PUNCHING_SIGN_SRV = "192.168.1.241";
unsigned HOLE_PUNCHER_PORT = 8888;
unsigned LISTENING_PORT = 5001;
unsigned CHUNK_SIZE = 1450;
unsigned MAX_CHUNK_SIZE = 1500;

unsigned OPENGL_WIDTH = 1280*NUM_OF_STREAMS; //we assume that pcs are appended horizontally (in x-axis)
unsigned OPENGL_HEIGHT = 720;

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
/**
 * input params: window width, window height, state of opengl thing (..), pointcloud, and id of pointcloud producer/source (used for "fusing" multiple pcs in one x-axis) 
 * */
void draw_draco_pointcloud(float width, float height, glfw_state& app_state, const draco::PointCloud* points, const uint8_t& _sourceID)
{
	 if (!points)
        return;
	//printf("draw_draco_pointcloud");
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
    
    //#if ENABLE_BnW == 1 
			//assert (points->num_attributes()>1); //the encoder did not add color info
	//#endif

	///get points of pointcloud
	float tmppoint [3];
	uint8_t tmptexture [2];
	uint8_t tmpcolor [3];
	unsigned k =0;
	float scale = 1.0f / float(NUM_OF_STREAMS);
	float offset = -1.0f + scale * (2.0f * float(_sourceID) + 1.0f); // center segment offset
	for (draco::PointIndex i(0); i < points->num_points(); ++i) {
		#if ENABLE_BnW == 1 
			#if COLOR_MODE == 1 //texture is added in points
		points->attribute(1)->GetMappedValue(i, tmptexture); //texture
		//printf("v %d u %d\n", tmptexture[0], tmptexture[1], );
		glTexCoord2fv((float*)tmptexture); 	
			#else // RGB is added in points
		points->attribute(1)->GetMappedValue(i, tmpcolor); //texture
		float color[3] = { (tmpcolor[0]-0.5f) / 255.0f, tmpcolor[1] / 255.0f, tmpcolor[2] / 255.0f };
		glColor3fv(color);
			#endif
		#endif
		
		points->attribute(0)->GetMappedValue(i, tmppoint); //coords
        if (tmppoint[3]!=1){ //if there is depth information
			//make horizontal "fusion"
			tmppoint[0] = tmppoint[0]*scale+offset;
			glVertex3fv(tmppoint);
		}
	}	
	
	// OpenGL cleanup
    glEnd();
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glPopAttrib();
}
static bool textureInitialized[NUM_OF_STREAMS] = {false};
    // Static texture handle (allocated once)
 static GLuint textureIds[NUM_OF_STREAMS] = {0};
 static int texWidth = 0, texHeight = 0;

// Handles all the OpenGL calls needed to display the JPG
/**
 * input params: window width, window height, JPG, and id of frame producer/source (used for "fusing" multiple pcs in one x-axis) 
 * */
void draw_JPG(int viewportWidth, int viewportHeight, const cv::Mat& decodedJPG, const uint8_t& _sourceID)
{
    if (decodedJPG.empty())
        return;

    // Setup viewport
    glViewport(0, 0, viewportWidth, viewportHeight);

    // OpenGL setup
    glLoadIdentity();
    glPushAttrib(GL_ALL_ATTRIB_BITS);

	//glClearColor(0.6f, 0.6f, 0.6f, 1.0f);
	//glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Orthographic projection
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
	//glOrtho(0, viewportWidth, 0, viewportHeight, -1, 1);  // left, right, bottom, top
	glOrtho(0, 2*texWidth, 0, texHeight, -1, 1);
	
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glEnable(GL_TEXTURE_2D);

    if (textureIds[_sourceID] == 0) {
        glGenTextures(1, &textureIds[_sourceID]);
        texWidth  = decodedJPG.cols;
        texHeight = decodedJPG.rows;
    }

    glBindTexture(GL_TEXTURE_2D, textureIds[_sourceID]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    if (!textureInitialized[_sourceID] || decodedJPG.cols != texWidth || decodedJPG.rows != texHeight) {
        texWidth  = decodedJPG.cols;
        texHeight = decodedJPG.rows;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, texWidth, texHeight, 0,
                     GL_RGB, GL_UNSIGNED_BYTE, decodedJPG.data);
        textureInitialized[_sourceID] = true;
                   
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, texWidth, texHeight,
                        GL_RGB, GL_UNSIGNED_BYTE, decodedJPG.data);
         GLenum err = glGetError();
		if (err != GL_NO_ERROR) printf("glTexImage2D error: %x\n", err);
    }

    // Draw quad filling viewport
    int x0 = _sourceID * texWidth;             // left of slot
	int x1 = x0 + texWidth;                    // right of slot
	int y0 = 0;                                          // bottom
	int y1 = texHeight;          

	glBegin(GL_QUADS);
	glTexCoord2f(0,0); glVertex2i(x0, y0);
	glTexCoord2f(1,0); glVertex2i(x1, y0);
	glTexCoord2f(1,1); glVertex2i(x1, y1);
	glTexCoord2f(0,1); glVertex2i(x0, y1);
	glEnd();
	
    glDisable(GL_TEXTURE_2D);

    // Restore matrices and attributes
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glPopAttrib();
}

static draco::DecoderOptions dec_options;	
/**
 * writes decoded pointcloud to first argument.
 * */
void decodingThread( draco::PointCloud* pc, const char* buffer, const unsigned& bufferSize ){
	//printf("starting decoding subpc \n");
	draco::DecoderBuffer dec_buffer;
	dec_buffer.Init(buffer, bufferSize);
	draco::PointCloudKdTreeDecoder decoder;
	decoder.Decode(dec_options, &dec_buffer, pc);		
	//printf("Finished decoding subpc \n");
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

//variables for inter-thread communication
std::vector<void*>  v_to_renderThread;
pthread_mutex_t m_to_renderThread = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t c_to_renderThread = PTHREAD_COND_INITIALIZER;

void *renderingThread(void* ptr){
#if ENABLE_UI != 1
	// Create a simple OpenGL window for rendering:
	window app(OPENGL_WIDTH, OPENGL_HEIGHT, "TeNEMP Pointcloud");
	// Construct an object to manage view state
	glfw_state app_state;
	// register callbacks to allow manipulation of the pointcloud
	register_glfw_callbacks(app, app_state);
#endif

	//draco objects
	draco::DecoderOptions dec_options;	
	unsigned counter = 0;
#if ENABLE_UI != 1
    while (app){
#else 
	while (true){
#endif
		std::string log="";
	    MessageToThread* msg = (MessageToThread*)readMessageFromThread(v_to_renderThread, m_to_renderThread, c_to_renderThread);
	    uint8_t sourceId = msg->sourceId;
	    unsigned frameId = msg->frameId;
	    uint8_t payloadType = msg->payloadType;
	    //log+="SrcID " + std::to_string(sourceId)+ " Frame "+std::to_string(frameId)+ " PayloadType "+ std::to_string(payloadType);
	    char * buffer = msg->buffer; //thats our pointcloud (or JPG)
	    unsigned bufferSize = msg->bufferSize;
	    if (bufferSize < CHUNK_SIZE) { free(msg); continue;} //when consumer starts in the middle of transmission, 1st frame will be zero bytes.. @TODO: find the source of this issue..
		
	    log+="SrcID " + std::to_string(sourceId)+ " Frame "+std::to_string(frameId)+ " ";
		/**
		 *  If Point Cloud is received
		 * */
		if (payloadType == 0){
		#if ENABLE_FRAME_HASHING == 0
			log+="Hash " + std::to_string(hash_memory(buffer,  bufferSize))+ " ";
		#endif

			unsigned index = 0;
		#if ENABLE_BnW == 1 && COLOR_MODE == 1
			//first read the JPG frame
			int color_data_size, width, height, channels ;
			width = app.width();
			height = app.height();
			memcpy(&color_data_size, buffer+index, sizeof(color_data_size));
			index+=sizeof(color_data_size);
			char* color_data = (char*) malloc (color_data_size);
			if (color_data_size >=bufferSize){ //in case of many loses, check that entire pc is received.. if not, skip rendering.
				thread_safe_print("T2 SrcID"+ std::to_string(sourceId)+" Frame "+std::to_string(frameId)+" Signifficant loss. Can not render pointcloud.\n");
				continue;
			}
			memcpy(color_data, buffer+index, color_data_size);
			index+=color_data_size;
			
			#if ENABLE_UI != 1
			app_state.tex.upload_from_raw_data(color_data, 5, app.width(), app.height(), 2, 0);
			#endif
			
			free (color_data);
		#endif	
			
			/**
			 * Multi-Threaded draco decomperssion
			 * */
			unsigned sub_bufferSize[NUM_OF_ENCODING_THREADS] = { 0 };
			char* sub_buffer[NUM_OF_ENCODING_THREADS] = { nullptr };
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
			for (unsigned i = 0; i<NUM_OF_ENCODING_THREADS; i++){
				if (dec_threads[i].joinable())
					dec_threads[i].join();		
			}
			#if ENABLE_DETAILED_TIMING == 0
				auto decoding_end = std::chrono::system_clock::now();
				std::chrono::duration<double> dracodec_seconds = decoding_end - decoding_start;
				log+="DracoDecoding_time " + std::to_string(dracodec_seconds.count()*1000)+ " ";
			#endif
			/// MERGE point clouds
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
			std::chrono::duration<double> merging_seconds = merging_end - decoding_end;				//printf("DracoDecoding_time  %f ", dracodec_seconds.count()*1000);
			log+="SubPCMerging_time " + std::to_string(merging_seconds.count()*1000)+ " ";
		#endif
				
		#if ENABLE_UI != 1
			// Draw the pointcloud		
			draw_draco_pointcloud(app.width(), app.height(), app_state, out_pc.get(), sourceId);
			#if ENABLE_DETAILED_TIMING == 0
			auto rendering_end = std::chrono::system_clock::now();
			std::chrono::duration<double> render_seconds = rendering_end - merging_end;
			log+="PC_Rendering_time "+ std::to_string(render_seconds.count()*1000) + " ";
			#endif
		#endif		
				
			/**
			 *  if JPG is received
			 * */
			}else if (payloadType == 1){
				auto capturing_end = std::chrono::system_clock::now();

				////decode JPG
				//convert char* to vector<char>
				std::vector<char> buffer_vec(buffer, buffer + bufferSize);
				cv::Mat decodedJPG = cv::imdecode(buffer_vec, cv::IMREAD_COLOR); 
				
				cv::cvtColor(decodedJPG, decodedJPG, cv::COLOR_BGR2RGB);
			#if ENABLE_DETAILED_TIMING == 0
				auto decoding_end = std::chrono::system_clock::now();
				std::chrono::duration<double> decoding_seconds = decoding_end - capturing_end;
				log+= "JPG_Decoding_time "+ std::to_string(decoding_seconds.count()*1000)+ " ";
			#endif
			
			
			#if ENABLE_UI == 0
				draw_JPG(app.width(), app.height(), decodedJPG, sourceId);
			#if ENABLE_DETAILED_TIMING == 0
				auto rendering_end = std::chrono::system_clock::now();
				std::chrono::duration<double> render_seconds = 
				rendering_end - decoding_end;
				log+= "JPG_Rendering_time "+ std::to_string(render_seconds.count()*1000)+ " ";
			#endif
				
			#endif
				//// Write to a file for debugging
				#if WRITE_JPG_TO_FILE == 0
				std::ofstream file("debug_frame_"+std::to_string(frameId)+".jpg", std::ios::binary);
				file.write(reinterpret_cast<const char*>(buffer_vec.data()), buffer_vec.size());
				file.close();
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
	
	/**
	 * Initialize per source variables
	 * */
	unsigned* currentFrameId= (unsigned *) malloc(sizeof(unsigned)*NUM_OF_STREAMS);
	unsigned* receivedBytes = (unsigned *) malloc(sizeof(unsigned)*NUM_OF_STREAMS);
	unsigned* pointCloudSize = (unsigned *) malloc(sizeof(unsigned)*NUM_OF_STREAMS);
	unsigned* receivedChunks = (unsigned *) malloc(sizeof(unsigned)*NUM_OF_STREAMS);
	std::chrono::time_point<std::chrono::system_clock>* start = (std::chrono::time_point<std::chrono::system_clock> *)	malloc(sizeof(std::chrono::time_point<std::chrono::system_clock>)*NUM_OF_STREAMS);
	char** buffer = (char**) malloc(NUM_OF_STREAMS * sizeof(char *));

	for (uint8_t i=0; i< NUM_OF_STREAMS; i++){
		currentFrameId[i] = 0;
		receivedBytes[i] = 0;
		pointCloudSize[i] = 0;
		receivedChunks[i] = 0;
		start[i] = std::chrono::system_clock::now();
		buffer[i] = new char [9999999]; //store the chunks of the frame; at first byte add source ID
		memcpy(buffer, &i, sizeof(i));
		}

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
	unsigned headerSize; 
	while (true){
		////////////// timer start 
		int recChunkLen = recvfrom(serverSocket, receivedChunk, MAX_CHUNK_SIZE , MSG_WAITALL, ( struct sockaddr *) &cliaddr, &len);
		if (recChunkLen ==0){ 
			printf("Received empty message\n"); 
			break; //or continue?
		}
		/**
		* Toy transmission protocol: [<source id, 1byte>, <frame id, 4bytes>, <chunk id, 4bytes>, <1400bytes content>]
		* source id is the serial number of the source, which is within [0, NUM_OF_STREAMS)
		* */
		uint8_t sourceId =  *((uint8_t*)receivedChunk);
		//read frameId
		assert (sourceId < NUM_OF_STREAMS);
		headerSize = sizeof(sourceId);
		unsigned frameId =  *((unsigned int*)(receivedChunk+headerSize));
		headerSize += sizeof(frameId);
		//read chunkId
		unsigned chunkId = *((unsigned int*)(receivedChunk+headerSize));
		headerSize += sizeof(chunkId);
		uint8_t payloadType = *((uint8_t*)(receivedChunk+headerSize));
		headerSize += sizeof(payloadType);
		if (frameId < currentFrameId[sourceId]){
			///got a late, reordered chunk, ignore it
			thread_safe_print("T1 ScrId %u Got late chunk %u of frame %u, while receiving frame %u. Ignoring it. \n", (unsigned) sourceId, chunkId, frameId, currentFrameId); 
			continue;
		}else if (frameId > currentFrameId[sourceId]){
			///got chunk for next frame, render current
			MessageToThread *msg = new MessageToThread(buffer[sourceId], pointCloudSize[sourceId], sourceId, currentFrameId[sourceId]);
			msg->payloadType = payloadType;
			writeMessageToThread(v_to_renderThread, m_to_renderThread, c_to_renderThread, (void*) msg);
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
			//buffer[sourceId] could be also reset.. 
		}
		///got another chunk of the current frame
		//read payload
		memcpy(buffer[sourceId]+chunkId*CHUNK_SIZE, receivedChunk+headerSize, recChunkLen-headerSize);
		//// fragment buffer localy and send data -- not required, but usefull for transisioning to UDP
		receivedBytes[sourceId] += recChunkLen;
		pointCloudSize[sourceId] += recChunkLen -sizeof(sourceId) - sizeof(frameId)-sizeof(chunkId);
		receivedChunks[sourceId]++;
	} //while end
	
	//std::close(clientSocket);
    return EXIT_SUCCESS;
}//end_main
