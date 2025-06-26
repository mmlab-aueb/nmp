#ifndef CLIENT_UTILS_H
#define CLIENT_UTILS_H

#include <libwebsockets.h>
#include <gst/gst.h>
#include <argtable3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include "client.h"

#define MAX_PIPELINES 10
#define MAX_LINE_LENGTH 512

typedef struct {
    const gchar *signal_name;  // The name of the signal (e.g., "on-negotiation-needed")
    GCallback callback;       // The callback function pointer
    gpointer user_data;       // User data to be passed to the callback
} SignalCallback;

// Function to print lws_context_creation_info struct
void print_lws_context_info(struct lws_context_creation_info *ctx_info);

// Function to print lws_client_connect_info struct
void print_lws_client_info(struct lws_client_connect_info *client_info);

void print_argtable(void *argtable[]);


int read_pipelines_from_file(
    const char *filename, 
    char *pipelines[MAX_PIPELINES][MAX_LINE_LENGTH], 
    int *pipeline_count
);

int initialize_values_of_libwebsocket_state(
    void *argtable[],
    struct lws_context_creation_info *ws_ctx_info,
    struct lws_client_connect_info *ws_client_info,
    struct lws_context **ws_context,
    const struct lws_protocols *protocols,
    CallbackHelper *callback_helper
);

char** parse_for_gstreamer_args(void *argtable[]);

int initialize_gstreamer_state(
    GstElement *pipeline,
    GstElement *webrtcbin,
    SignalCallback *callbacks,
    const gchar *pipeline_desc
);

#endif // CLIENT_UTILS_H