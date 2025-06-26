cmake_minimum_required(VERSION 3.10)
project(TENEMP C)

# Enable debugging symbols by default
set(CMAKE_BUILD_TYPE Debug)  # Enable Debug Mode
set(CMAKE_C_FLAGS_DEBUG "-g -O0")  # Include debug symbols and disable optimizations
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
find_package(PkgConfig REQUIRED)
include(FetchContent)

# ---------------------------------------------------------------------
# Fetch libwebsockets
# ---------------------------------------------------------------------
FetchContent_Declare(
    libwebsockets
    GIT_REPOSITORY https://libwebsockets.org/repo/libwebsockets
    GIT_TAG        main  
)
FetchContent_MakeAvailable(libwebsockets)

# ---------------------------------------------------------------------
# Fetch cJSON
# ---------------------------------------------------------------------
FetchContent_Declare(
    cjson
    GIT_REPOSITORY https://github.com/DaveGamble/cJSON.git
    GIT_TAG        v1.7.15
)
FetchContent_MakeAvailable(cjson)

# ---------------------------------------------------------------------
# Fetch argtable3
# ---------------------------------------------------------------------
FetchContent_Declare(
    argtable3
    GIT_REPOSITORY https://github.com/argtable/argtable3.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(argtable3)

# ---------------------------------------------------------------------
# Find required packages
# ---------------------------------------------------------------------
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)
pkg_check_modules(GSTREAMER_WEBRTC REQUIRED gstreamer-webrtc-1.0)
pkg_check_modules(GSTREAMER_SDP REQUIRED gstreamer-sdp-1.0)
pkg_check_modules(CAIRO REQUIRED cairo)
pkg_check_modules(GTK REQUIRED gtk+-3.0)

find_package(Threads REQUIRED)

if(NOT GSTREAMER_FOUND)
    message(FATAL_ERROR "GStreamer not found!")
endif()
if(NOT GSTREAMER_WEBRTC_FOUND)
    message(FATAL_ERROR "GStreamer WebRTC not found!")
endif()
if(NOT GSTREAMER_SDP_FOUND)
    message(FATAL_ERROR "GStreamer SDP not found!")
endif()
if(NOT CAIRO_FOUND)
    message(FATAL_ERROR "Cairo not found!")
endif()
if(NOT GTK_FOUND)
    message(FATAL_ERROR "GTK not found! Install with: sudo apt install libgtk-3-dev")
endif()

# Check libwebsockets target
get_target_property(LWS_SHARED_EXISTS websockets_shared TYPE)
if (NOT TARGET websockets_shared)
    message(FATAL_ERROR "websockets_shared target not found!")
endif()

message(STATUS "Libwebsockets source directory: ${libwebsockets_SOURCE_DIR}")
message(STATUS "Libwebsockets binary directory: ${libwebsockets_BINARY_DIR}")
message(STATUS "GStreamer include dirs: ${GSTREAMER_INCLUDE_DIRS}")
message(STATUS "Cairo include dirs: ${CAIRO_INCLUDE_DIRS}")
message(STATUS "GTK include dirs: ${GTK_INCLUDE_DIRS}")

add_definitions(-DGST_USE_UNSTABLE_API)

# ---------------------------------------------------------------------
# Include directories
# ---------------------------------------------------------------------
include_directories(
    ${GSTREAMER_INCLUDE_DIRS}
    ${CAIRO_INCLUDE_DIRS}
    ${GTK_INCLUDE_DIRS}
    ${libwebsockets_SOURCE_DIR}/include
    ${libwebsockets_BINARY_DIR}/include
    ${cjson_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/signaling_server
    ${CMAKE_CURRENT_SOURCE_DIR}/message_protocol
    ${CMAKE_CURRENT_SOURCE_DIR}/message_handling
    ${CMAKE_CURRENT_SOURCE_DIR}/client
    ${argtable3_SOURCE_DIR}/src
)

# ---------------------------------------------------------------------
# Link directories
# ---------------------------------------------------------------------
link_directories(
    ${GSTREAMER_LIBRARY_DIRS}
    ${CAIRO_LIBRARY_DIRS}
    ${GTK_LIBRARY_DIRS}
    ${libwebsockets_BINARY_DIR}/lib
)

# ---------------------------------------------------------------------
# Define executables
# ---------------------------------------------------------------------
add_executable(receiver_client        client/receiver_client.c)
add_executable(sender_client          client/sender_client.c)
add_executable(client client/client.c)
add_executable(signaling_server      signaling_server/signaling_server.c)
add_executable(test_message_protocol message_protocol/test_message_protocol.c)
add_executable(sender_client_ui       client/sender_client_ui.c)
add_executable(receiver_client_ui     client/receiver_client_ui.c)

# ---------------------------------------------------------------------
# Unified client executable
# ---------------------------------------------------------------------
add_executable(unified_client         client/unified_client.c)

# ---------------------------------------------------------------------
# Define libraries
# ---------------------------------------------------------------------
add_library(message_handling STATIC message_handling/message_handling.c)
add_library(signaling_context STATIC message_handling/signaling_context.c)
add_library(message_protocol STATIC message_protocol/message_protocol.c)
add_library(client_utils STATIC client/client_utils.c)

# Link argtable3 into client_utils so its users don’t have to
target_link_libraries(client_utils
    PUBLIC
      argtable3
)

# ---------------------------------------------------------------------
# Apply debugging flags to all targets
# ---------------------------------------------------------------------
set_target_properties(
    receiver_client
    sender_client
    client
    signaling_server
    sender_client_ui
    receiver_client_ui
    unified_client                 # <-- added unified_client here
    PROPERTIES COMPILE_FLAGS "-g -O0" LINK_FLAGS "-g"
)

# ---------------------------------------------------------------------
# Link libraries
# ---------------------------------------------------------------------
target_link_libraries(message_protocol PUBLIC cjson)

target_link_libraries(
    message_handling PRIVATE
      websockets_shared
      ${GSTREAMER_LIBRARIES}
      ${GSTREAMER_WEBRTC_LIBRARIES}
      ${GSTREAMER_SDP_LIBRARIES}
)

target_link_libraries(
    signaling_context PRIVATE
      websockets_shared
      ${GSTREAMER_LIBRARIES}
      ${GSTREAMER_WEBRTC_LIBRARIES}
      ${GSTREAMER_SDP_LIBRARIES}
)

target_link_libraries(test_message_protocol PRIVATE message_protocol)

target_link_libraries(receiver_client PRIVATE
    websockets_shared
    ${GSTREAMER_LIBRARIES}
    ${GSTREAMER_WEBRTC_LIBRARIES}
    ${GSTREAMER_SDP_LIBRARIES}
    Threads::Threads
    message_protocol
    message_handling
    client_utils
)

target_link_libraries(client PRIVATE
    websockets_shared
    ${GSTREAMER_LIBRARIES}
    ${GSTREAMER_WEBRTC_LIBRARIES}
    ${GSTREAMER_SDP_LIBRARIES}
    Threads::Threads
    message_protocol
    message_handling
    signaling_context
    argtable3
    client_utils
)

target_link_libraries(sender_client PRIVATE
    websockets_shared
    ${GSTREAMER_LIBRARIES}
    ${GSTREAMER_WEBRTC_LIBRARIES}
    ${GSTREAMER_SDP_LIBRARIES}
    Threads::Threads
    message_protocol
    message_handling
    client_utils
)

target_link_libraries(signaling_server PRIVATE
    websockets_shared
    ${GSTREAMER_LIBRARIES}
    ${GSTREAMER_WEBRTC_LIBRARIES}
    ${GSTREAMER_SDP_LIBRARIES}
    Threads::Threads
    message_protocol
    message_handling
    signaling_context
)

target_link_libraries(sender_client_ui PRIVATE
    websockets_shared
    ${GSTREAMER_LIBRARIES}
    ${GSTREAMER_WEBRTC_LIBRARIES}
    ${GSTREAMER_SDP_LIBRARIES}
    ${CAIRO_LIBRARIES}
    ${GTK_LIBRARIES}
    Threads::Threads
)

target_link_libraries(receiver_client_ui PRIVATE
    websockets_shared
    ${GSTREAMER_LIBRARIES}
    ${GSTREAMER_WEBRTC_LIBRARIES}
    ${GSTREAMER_SDP_LIBRARIES}
    ${CAIRO_LIBRARIES}
    ${GTK_LIBRARIES}
    Threads::Threads
)

target_link_libraries(unified_client PRIVATE
    websockets_shared
    ${GSTREAMER_LIBRARIES}
    ${GSTREAMER_WEBRTC_LIBRARIES}
    ${GSTREAMER_SDP_LIBRARIES}
    Threads::Threads
    message_protocol
    message_handling
    client_utils
)

# ---------------------------------------------------------------------
# Custom targets for running the UI executables with LD_PRELOAD
# ---------------------------------------------------------------------
add_custom_target(run_receiver_client_ui
    COMMAND env LD_PRELOAD=/lib/x86_64-linux-gnu/libpthread.so.0 ./receiver_client_ui
    DEPENDS receiver_client_ui
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

set(RECEIVER_UI_LAUNCHER "${CMAKE_BINARY_DIR}/receiver_client_ui.sh")
file(WRITE ${RECEIVER_UI_LAUNCHER} "#!/bin/bash\n")
file(APPEND ${RECEIVER_UI_LAUNCHER} "export LD_PRELOAD=/lib/x86_64-linux-gnu/libpthread.so.0\n")
file(APPEND ${RECEIVER_UI_LAUNCHER} "exec \"${CMAKE_BINARY_DIR}/receiver_client_ui\" \"$@\"\n")
add_custom_target(receiver_ui_launcher ALL COMMAND chmod +x ${RECEIVER_UI_LAUNCHER})

set(SENDER_UI_LAUNCHER "${CMAKE_BINARY_DIR}/sender_client_ui.sh")
file(WRITE ${SENDER_UI_LAUNCHER} "#!/bin/bash\n")
file(APPEND ${SENDER_UI_LAUNCHER} "export LD_PRELOAD=/lib/x86_64-linux-gnu/libpthread.so.0\n")
file(APPEND ${SENDER_UI_LAUNCHER} "exec \"${CMAKE_BINARY_DIR}/sender_client_ui\" \"$@\"\n")
add_custom_target(sender_ui_launcher ALL COMMAND chmod +x ${SENDER_UI_LAUNCHER})
