#include "client_utils.h"

// Function to print lws_context_creation_info struct
void print_argtable(void *argtable[]) {
    printf("=== [LIBWEBSOCKETS] ARGTABLE ===\n");
    struct arg_int *port_arg = (struct arg_int *)argtable[1];
    struct arg_str *address_arg = (struct arg_str *)argtable[2];
    struct arg_str *path_arg = (struct arg_str *)argtable[3];
    struct arg_int *ssl_arg = (struct arg_int *)argtable[4];
    struct arg_str *pipeline_file_arg = (struct arg_str *)argtable[5];
    struct arg_int *pipeline_index_arg = (struct arg_int *)argtable[6];

    const int port = port_arg->ival[0];
    const int ssl = ssl_arg->ival[0];
    const char *address = address_arg->sval[0];
    const char *path = path_arg->sval[0];
    const char *pipeline_file = pipeline_file_arg->sval[0];
    const int pipeline_index = pipeline_index_arg->ival[0];

    printf("Port           : %d\n", port);
    printf("Address        : %s\n", address);
    printf("Path           : %s\n", path);
    printf("Use SSL        : %d\n", ssl);
    printf("Pipeline File  : %s\n", pipeline_file);
    printf("Pipeline Index : %d\n", pipeline_index);
    printf("====================================\n");
}
// Function to print lws_context_creation_info struct
void print_lws_context_info(struct lws_context_creation_info *ctx_info) {
    printf("=== [LIBWEBSOCKETS] Context Info ===\n");
    printf("Port: %d\n", ctx_info->port);
    printf("Protocol Pointer: %p\n", (void *)ctx_info->protocols);
    printf("====================================\n");
}

// Function to print lws_client_connect_info struct
void print_lws_client_info(struct lws_client_connect_info *client_info) {
    printf("=== [LIBWEBSOCKETS] Client Connection Info ===\n");
    printf("Address: %s\n", client_info->address);
    printf("Port: %d\n", client_info->port);
    printf("Path: %s\n", client_info->path);
    printf("SSL Connection: %d\n", client_info->ssl_connection);
    printf("Context Pointer: %p\n", (void *)client_info->context);
    printf("=============================================\n");
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PIPELINES 10
#define MAX_LINE_LENGTH 512

int read_pipelines_from_file(const char *filename, char *pipelines[MAX_PIPELINES][MAX_LINE_LENGTH], int *pipeline_count) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("Error opening pipeline file");
        return -1;
    }

    printf("\n=== [GSTREAMER] Reading Pipelines from: %s ===\n", filename);

    char line[MAX_LINE_LENGTH];
    *pipeline_count = 0;

    while (fgets(line, sizeof(line), file) && *pipeline_count < MAX_PIPELINES) {
        line[strcspn(line, "\n")] = 0;
        if (line[0] == '#' || strlen(line) == 0) continue;
        printf("Read line from file: %s\n", line);
        strncpy(pipelines[*pipeline_count], line, MAX_LINE_LENGTH);
        (*pipeline_count)++;
    }

    fclose(file);

    if (*pipeline_count == 0) {
        printf("No valid pipelines found in the file!\n");
        return -1;
    }

    printf("=========================================\n");

    return 0;
}


int initialize_values_of_libwebsocket_state(
    void *argtable[],
    struct lws_context_creation_info *ws_ctx_info,
    struct lws_client_connect_info *ws_client_info,
    struct lws_context **ws_context,
    const struct lws_protocols *protocols,
    CallbackHelper *callback_helper
){

    printf("[LIBWEBSOCKETS] Initializing libwebsocket state...\n");
    struct arg_int *port_arg = (struct arg_int *)argtable[1];
    struct arg_str *address_arg = (struct arg_str *)argtable[2];
    struct arg_str *path_arg = (struct arg_str *)argtable[3];
    struct arg_int *ssl_arg = (struct arg_int *)argtable[4];

    const int port = port_arg->ival[0];
    const int ssl = ssl_arg->ival[0];
    const char *address = address_arg->sval[0];
    const char *path = path_arg->sval[0];

    memset(ws_ctx_info, 0, sizeof(struct lws_context_creation_info));
    ws_ctx_info->port = CONTEXT_PORT_NO_LISTEN;
    ws_ctx_info->protocols = protocols;

    *ws_context = lws_create_context(ws_ctx_info);

    if(!*ws_context){
        printf("Failed to initialize libwebsocket context\n");
        return -1;
    }

    memset(ws_client_info, 0, sizeof(struct lws_client_connect_info)); 
    ws_client_info->port = port;
    ws_client_info->address = address;
    ws_client_info->path = path;
    ws_client_info->ssl_connection = ssl;
    ws_client_info->context = *ws_context;
    ws_client_info->userdata = callback_helper;

    printf("[LIBWEBSOCKETS] Initialized libwebsocket state successfully...\n");

    print_lws_client_info(ws_client_info);
    print_lws_context_info(ws_ctx_info);

    return 0;
}


char** parse_for_gstreamer_args(void *argtable[]){
    printf("[GSTREAMER] Parsing for GStreamer command args...\n");

    struct arg_int *port_arg = (struct arg_int *)argtable[1];
    struct arg_str *address_arg = (struct arg_str *)argtable[2];
    struct arg_str *path_arg = (struct arg_str *)argtable[3];
    struct arg_int *ssl_arg = (struct arg_int *)argtable[4];

    // Max 10 digits for 2^32 integers
    char port_str[10], ssl_str[10];
    snprintf(port_str, sizeof(port_str), "%d", port_arg->ival[0]);
    snprintf(ssl_str, sizeof(ssl_str), "%d", ssl_arg->ival[0]);

    const char *address = address_arg->sval[0];
    const char *path = path_arg->sval[0];

    char *argv[] = { "client", port_str, ssl_str, (char *)address, (char *)path, NULL};
    int argc = sizeof(argv) / sizeof(argv[0]) - 1;  // Exclude NULL

    printf("[GSTREAMER] argc: %d\n", argc);

    printf("[GSTREAMER] argv values:\n");
    for (int i = 0; i < argc; i++) {
        printf("  argv[%d]: %s\n", i, argv[i]);
    }

    return argv;
}

int initialize_gstreamer_state(
    GstElement *pipeline,
    GstElement *webrtcbin,
    SignalCallback *callbacks,
    const gchar *pipeline_desc
){
    printf("[GSTREAMER] Initializing GStreamer state...\n");

    /* Create the unified pipeline.
       This pipeline captures video from /dev/video0,
       tees it to both a local preview and the WebRTC encoder,
       and sends local media via webrtcbin. */
    printf("[GSTREAMER] Setting pipeline description %s...\n", pipeline_desc);
    pipeline = gst_parse_launch(pipeline_desc, NULL);

    if (!pipeline) {
        g_printerr("[GSTREAMER] Failed to create pipeline\n");
        return -1;
    }


    /* Retrieve the webrtcbin element and set properties */
    webrtcbin = gst_bin_get_by_name(GST_BIN(pipeline), "webrtcbin");
    if (!webrtcbin) {
        g_printerr("[Main] Failed to get webrtcbin element\n");
        return -1;
    }

    g_object_set(webrtcbin, "stun-server", "stun://stun.l.google.com:19302", NULL);

    for (int i = 0; callbacks[i].signal_name != NULL; i++) {
        if (callbacks[i].signal_name && callbacks[i].callback) {
            g_signal_connect(webrtcbin, callbacks[i].signal_name, callbacks[i].callback, callbacks[i].user_data);
            printf("[GSTREAMER] Connected signal: %s\n", callbacks[i].signal_name);
        }
    }

    // Set the pipeline to PLAYING state.
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    printf("[GSTREAMER] Pipeline initialized and set to PLAYING.\n");
}