## Overview

The project include the CPP source code of the consumer and producer applications for the volumetric streaming application that were developed during the TENEMP project [1]. 

Detailed description of the applications can be found in [2].


## Compile and run
The applications have been tested only in linux ubuntu 24.04 with gcc 11.4.0.

To run the applications, the node running the producer application should be equiped with a realsense depth camera (tested only with Intel D453i)


#### consumer.CPP
Dependencies: draco CPP library[4], glfw [5], GL [6] 

Command for building and executing (from the root directory):
```console
$ reset && CAPP2=consumer;  g++ ${CAPP2}.CPP -o $CAPP2 -lrealsense2 -lglfw -lGL -lGLU -ldraco -lpthread  -g  && ./${CAPP2} 
```
#### producer.CPP 
Dependencies: draco CPP library [4], intel realsense CPP library [7], jpeg [9]

Command for building and executing (from the root directory):
```console
$ reset && CAPP=producer_multithreaded;  g++ ${CAPP}.CPP -o $CAPP -lrealsense2 -ldraco -ljpeg -lpthread -g  && ./${CAPP}
```
## Configuration parameters

#### producer application parameters
The following parameters are available for the producer application: 
* ENABLE_HOLE_PUNCHING 1 //0 true, 1 false
* ENABLE_SFU 1 //0 true, 1 false
* ENABLE_BnW: <boolean> Disables color rendering; ignores colour information of point clouds.
* COLOR_MODE: <unsigned> 0 for sending RGB for each point, 1 for sending color frame and textures; the latter is still under development
* ENABLE_FRAME_HASHING: <boolean> Enables estimating the hash values of received point clouds for estimated (post-experiment) reliability. 
* ENABLE_DETAILED_TIMING: <boolean> Enables logging the time of individual operations during packet and point cloud processing.
* ENABLE_FEC: <boolean> Enables FEC supported by affect library [9] -- currently under development.
* STOP_AT: <unsigned> Stops application after sending N packets; can be omitted to run forever
* DROP_POINTS_RATE: <unsigned> Drops 1 of N points in a point cloud, e.g., 2 results in 50% reduction, 4 results in 25% reduction and so on. 
* EXCLUDE_A_COLOUR: <boolead> Ignored the transparency byte in RGB -- obsolete
* DRACO_COMPRESSION_LEVEL: <unsigned> The compression level of draco encoder
* QUANTIZATION_BITS: <unsigned> The quantization bits of draco encoder
* HOLE_PUNCHING_SIGN_SRV: <unsigned> The IPv4 address for communicating with the UDP hole punching server, the SFU or MCU.
* HOLE_PUNCHER_PORT: <unsigned> The port for communicating with the UDP hole punching server, the SFU or MCU.
* CONSUMER_IP: <unsigned> The IPv4 address for sending packets from the consumer app.
* CONSUMER_PORT: <unsigned> The port for sending packets from the consumer app.
* NUM_OF_ENCODING_THREADS: <unsigned> the number of threads used during compression
* CHUNK_SIZE: <unsigned> The size of chunks that will be transmitted over UDP



#### consumer application parameters
The following parameters are available for the consumer application: 
* ENABLE_HOLE_PUNCHING: <boolean> Enables the mediation of a UDP hole punching server. Feature is tested but not documented.
* ENABLE_SFU: <boolean> Enables the mediation of a UDP hole punching server. Feature is tested but not documented.
* ENABLE_UI: <boolean> Enables the openGL window for rendering the 3D point clouds to the nodes screen.
* ENABLE_BnW: <boolean> Disables color rendering; ignores colour information of point clouds. Must be same with producer
* ENABLE_FRAME_HASHING: <boolean> Enables estimating the hash values of received point clouds for estimated (post-experiment) reliability. 
* ENABLE_DETAILED_TIMING: <boolean> Enables logging the time of individual operations during packet and point cloud processing.
* ENABLE_FEC: <boolean> Enables FEC supported by affect library [9] -- currently under development.
* HOLE_PUNCHING_SIGN_SRV: <String> The IPv4 address for communicating with the UDP hole punching server, the SFU or MCU.
* HOLE_PUNCHER_PORT: <unsigned> The port for communicating with the UDP hole punching server, the SFU or MCU.
* LISTENING_PORT: <unsigned> The port for receiving packets from the producer app.
* NUM_OF_ENCODING_THREADS: <unsigned> the number of threads used during compression. Must be same with consumer app


## Output/Logs
Details on the output or logs of the applications is available in [2].
 
## Parsers
Four parsers are included in directory ./parsers. The parsers can be applied to log files like the ones available at [3]. Similar files can be created by simply directing the output of the applications to a file during application execuring (using the ">" symbol). E.g., the following command compiles and executes the consumer app sending output to file "myfile"
```console
$ reset && CAPP2=consumer;  g++ ${CAPP2}.CPP -o $CAPP2 -lglfw -lGL -lGLU -ldraco  -g  && ./${CAPP2} > myfile 
```
The parsers assume that consumer and producer logfiles for the same execution have the same timestamp in their filename.

#### endtoend_latency.sh  
Estimates end-to-end latency per point cloud from before capturing frame (at producer) until rendering point cloud to openGL window (at consumer). NTP over ethernet should be used to synchronize node clocks.

#### hash_checker.sh  
Estimates reliability of transmission by check the number of point clouds that were received intact at the receiver.

The script finds all producer files, then finds the consumer file for same experiment (with same timestamp). then reads the hash values of point clouds in producer file and searches them in the consumer file, finally prints the number of found hash values.

#### parser_consumer.sh 
Estimates the metrics at the consumer application.

#### parser_producer.sh
Estimates the metrics at the producer application.


## License

This project is licensed under the GNU General Public License v3.0. See the [LICENSE](LICENSE) file for details.

Some portions of the code are derived from third-party software licensed under the Apache License 2.0. These parts retain their original licensing, and their notices are preserved in [NOTICE](NOTICE) and [LICENSE-APACHE](LICENSE-APACHE).

## References

1. TENEMP Project site, https://mmlab-aueb.github.io/tenemp-site/
2. Thomas, Y. and Xylomenos, G., "Ultra-low Latency Point Cloud Streaming in 5G", EuroXR International Conference, 2025
3. DataSet DOI: 10.5281/zenodo.15736910
4. Google Draco encoder, https://github.com/google/draco
5. glfw, https://github.com/glfw/glfw
6. openGL, http://www.opengl.org/
7. Intel realsense SDK, https://github.com/IntelRealSense/librealsense
8. AFF3CT: A Fast Forward Error Correction Toolbox!, https://github.com/aff3ct/aff3ct
0. jpg CPP library, https://www.ijg.org/files/
