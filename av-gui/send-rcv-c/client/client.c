#include "client.h"
#include "client_utils.h"



// Define a structure to hold all allocated resources.
typedef struct AppResources {
    GstElement *pipeline;
    GstElement *webrtcbin;
    CallbackHelper *callback_helper;
    struct lws_context *ws_context;
    struct lws *ws_client;
    GMainLoop *loop;
    struct signaling_state *client_state;
    struct signaling_context *sig_ctx;
    void **argtable;    
    size_t argtable_count; 
} AppResources;

/**
 * Frees all resources in the given AppResources struct.
 * Checks if each resource is allocated before attempting to free it.
 */
static void free_app_resources(AppResources *res)
{
    if (!res) return;

    if(res->callback_helper){
        free(res->callback_helper);
    }

    // 1. Cleanup GStreamer pipeline and its elements
    if (res->pipeline) {
        gst_element_set_state(res->pipeline, GST_STATE_NULL);
        if (res->webrtcbin) {
            gst_object_unref(res->webrtcbin);
            res->webrtcbin = NULL;
        }
        gst_object_unref(res->pipeline);
        res->pipeline = NULL;
    }

    // 2. Destroy the libwebsockets context
    if (res->ws_context) {
        lws_context_destroy(res->ws_context);
        res->ws_context = NULL;
    }

    // 2. Destroy the libwebsockets client
    if (res->ws_context) {
        res->ws_client = NULL;
    }

    // 3. Unreference the GLib main loop
    if (res->loop) {
        g_main_loop_unref(res->loop);
        res->loop = NULL;
    }

    // 4. Cleanup client_state (SDP strings and ICE candidate array)
    if (res->client_state) {
        if (res->client_state->sdp_offer) {
            free(res->client_state->sdp_offer);
            res->client_state->sdp_offer = NULL;
        }
        if (res->client_state->sdp_answer) {
            free(res->client_state->sdp_answer);
            res->client_state->sdp_answer = NULL;
        }
        if (res->client_state->ice_candidates) {
            for (guint i = 0; i < res->client_state->ice_candidates->len; i++) {
                free(g_ptr_array_index(res->client_state->ice_candidates, i));
            }
            g_ptr_array_free(res->client_state->ice_candidates, TRUE);
            res->client_state->ice_candidates = NULL;
        }
        free(res->client_state);
        res->client_state = NULL;
    }

    // 5. Cleanup signaling context
    if (res->sig_ctx) {
        destroy_signaling_context(res->sig_ctx);
        res->sig_ctx = NULL;
    }

    // 6. Free argtable resources, if used
    if (res->argtable) {
        arg_freetable(res->argtable, res->argtable_count);
        res->argtable = NULL;
    }
}

static void update_offer_state(struct signaling_state *client_state, const char *sdp_str) {
    if (!client_state->sdp_offer || strcmp(client_state->sdp_offer, sdp_str) != 0) {
        free(client_state->sdp_offer);
        client_state->sdp_offer = strdup(sdp_str);
        client_state->offer_version = ++client_state->global_version_counter;
        g_print("[State] Updated local SDP offer (version %lu).\n", client_state->offer_version);
    } else {
        g_print("[State] Received identical SDP offer; no update needed.\n");
    }
}

static void update_answer_state(struct signaling_state *client_state, const char *sdp_str) {
    if (!client_state->sdp_answer || strcmp(client_state->sdp_answer, sdp_str) != 0) {
        free(client_state->sdp_answer);
        client_state->sdp_answer = strdup(sdp_str);
        client_state->answer_version = ++client_state->global_version_counter;
        g_print("[State] Updated remote SDP answer (version %lu).\n", client_state->answer_version);
    } else {
        g_print("[State] Received identical SDP answer; no update needed.\n");
    }
}

static void add_ice_candidate_state(struct signaling_state *client_state, gchar *candidate) {
    g_ptr_array_add(client_state->ice_candidates, strdup(candidate));
    g_print("[State] Added ICE candidate: %s\n", candidate);
}

/* --- WebRTC Callbacks --- */

static void on_ice_candidate(
    GstElement *webrtc, 
    guint mline_index, 
    gchar *candidate, 
    gpointer user_data
) {
    // If the candidate is empty, log and exit early.
    if (!candidate || strlen(candidate) == 0) {
        g_print("[WebRTC] Ignoring empty ICE candidate message.\n");
        return;
    }
    
    CallbackHelper *curr_callback_helper = (CallbackHelper*) user_data;
    struct signaling_state *client_state = curr_callback_helper->client_state;
    struct lws *ws_client = curr_callback_helper->wsi;

    // Log the generated ICE candidate.
    g_print("[WebRTC] ICE candidate generated: %s\n", candidate);
    
    // Update the signaling state with the new ICE candidate.
    add_ice_candidate_state(client_state, candidate);

    // Create a JSON message for the ICE candidate.
    char *json = create_json_message("ice_candidate", candidate);
    if (json) {
        // Send the JSON message to the signaling server.
        if (send_data_to_client(ws_client, json) == 0)
            g_print("[WebRTC] Sent ICE candidate via signaling server.\n");
        else
            g_printerr("[WebRTC] Failed to send ICE candidate.\n");
        // Free the allocated JSON message.
        free(json);
    } else {
        // Log an error if JSON creation fails.
        g_printerr("[WebRTC] Failed to create JSON for ICE candidate.\n");
    }
}

static void on_answer_created(
    GstPromise *promise, 
    GstElement *webrtcbin,
    gpointer user_data
) {
    GstWebRTCSessionDescription *answer = NULL;
    const GstStructure *reply = gst_promise_get_reply(promise);
    gst_structure_get(reply, "answer", GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &answer, NULL);
    // Free the promise as it's no longer needed.
    gst_promise_unref(promise);

    // If no answer was created, log an error and return.
    if (!answer) {
        g_printerr("[WebRTC] Failed to create answer\n");
        return;
    }

    CallbackHelper *curr_callback_helper = (CallbackHelper*) user_data;
    struct signaling_state *client_state = curr_callback_helper->client_state;
    struct lws *ws_client = curr_callback_helper->wsi;

    // Convert the SDP answer to a text string.
    gchar *sdp_str = gst_sdp_message_as_text(answer->sdp);
    g_print("[WebRTC] Created SDP answer:\n%s\n", sdp_str);

    // Update the signaling state with the new SDP answer.
    update_answer_state(client_state, sdp_str);

    // Set the local description for webrtcbin using the created answer.
    g_signal_emit_by_name(webrtcbin, "set-local-description", answer, NULL);

    // Create a JSON message for the SDP answer.
    char *json = create_json_message("sdp_answer", sdp_str);
    if (json) {
        // Send the JSON message to the signaling server.
        if (send_data_to_client(ws_client, json) == 0)
            g_print("[WebRTC] Sent SDP answer via signaling server.\n");
        else
            g_printerr("[WebRTC] Failed to send SDP answer.\n");
        // Free the allocated JSON message.
        free(json);
    } else {
        // Log an error if JSON creation fails.
        g_printerr("[WebRTC] Failed to create JSON for SDP answer.\n");
    }

    // Free the allocated SDP text string.
    g_free(sdp_str);
    // Free the WebRTC session description.
    gst_webrtc_session_description_free(answer);
}

static void on_negotiation_needed(GstElement *webrtc, gpointer user_data) {
    g_print("[WebRTC] Negotiation needed, creating offer...\n");
    // Create a promise that will trigger on_answer_created when the offer is ready.
    GstPromise *promise = gst_promise_new_with_change_func(on_answer_created, NULL, NULL);
    // Emit the create-offer signal to start the negotiation process.
    g_signal_emit_by_name(webrtc, "create-offer", NULL, promise);
}

static void on_pad_added(GstElement *element, GstPad *pad, gpointer user_data) {
    GstCaps *caps = gst_pad_get_current_caps(pad);
    if (!caps)
        caps = gst_pad_query_caps(pad, NULL);
    if (!caps)
        return;

    const gchar *caps_name = gst_structure_get_name(gst_caps_get_structure(caps, 0));
    g_print("[Unified] New pad '%s' added from element '%s'\n", caps_name, GST_ELEMENT_NAME(element));

    // Check for a video RTP stream.
    const gchar *media = gst_structure_get_string(gst_caps_get_structure(caps, 0), "media");
    if (!media || g_strcmp0(media, "video") != 0) {
        g_print("[Unified] Ignoring pad because media is not video.\n");
        gst_caps_unref(caps);
        return;
    }

    if (!g_str_has_prefix(caps_name, "application/x-rtp")) {
        g_print("[Unified] Ignoring pad: caps are not RTP.\n");
        gst_caps_unref(caps);
        return;
    }

    /* Create a new bin to hold the decode chain for the remote stream */
    GstElement *bin = gst_bin_new("decoder_bin");
    GstElement *depay = gst_element_factory_make("rtpvp8depay", "depay");
    GstElement *dec   = gst_element_factory_make("vp8dec", "dec");
    GstElement *conv  = gst_element_factory_make("videoconvert", "conv");
    GstElement *queue = gst_element_factory_make("queue", "queue");
    GstElement *sink  = gst_element_factory_make("autovideosink", "sink");

    if (!bin || !depay || !dec || !conv || !queue || !sink) {
        g_printerr("[Unified] Failed to create decode chain elements\n");
        if (bin)
            gst_object_unref(bin);
        gst_caps_unref(caps);
        return;
    }

    /* Optionally, disable sync on the sink so frames render as soon as they arrive */
    g_object_set(sink, "sync", FALSE, NULL);

    gst_bin_add_many(GST_BIN(bin), depay, dec, conv, queue, sink, NULL);
    if (!gst_element_link_many(depay, dec, conv, queue, sink, NULL)) {
        g_printerr("[Unified] Failed to link decode chain elements\n");
        gst_object_unref(bin);
        gst_caps_unref(caps);
        return;
    }

    /* Create a ghost pad for the bin using the depayloader’s sink pad */
    GstPad *sinkpad = gst_element_get_static_pad(depay, "sink");
    GstPad *ghost_pad = gst_ghost_pad_new("sink", sinkpad);
    gst_object_unref(sinkpad);
    gst_element_add_pad(bin, ghost_pad);

    /* Add the bin to the main pipeline */
    GstElement *parent_pipeline = GST_ELEMENT(gst_element_get_parent(element));
    gst_bin_add(GST_BIN(parent_pipeline), bin);
    gst_element_sync_state_with_parent(bin);

    /* Link the remote pad from webrtcbin to the ghost pad of our bin */
    GstPad *bin_sink_pad = gst_element_get_static_pad(bin, "sink");
    if (gst_pad_link(pad, bin_sink_pad) != GST_PAD_LINK_OK) {
        g_printerr("[Unified] Failed to link remote pad to decoder bin ghost pad\n");
    } else {
        g_print("[Unified] Successfully linked remote pad to decoder bin\n");
    }
    gst_object_unref(bin_sink_pad);
    gst_object_unref(parent_pipeline);
    gst_caps_unref(caps);
}

/* --- Libwebsockets Client Callback --- */
static int ws_client_callback(struct lws *wsi, enum lws_callback_reasons reason,
                              void *user, void *in, size_t len) {

    CallbackHelper *temp = (CallbackHelper*)user;

    if(temp == NULL){
        if (reason == LWS_CALLBACK_PROTOCOL_INIT) {
            g_print("[WS] Protocol initialized.\n");
            return 0;
        }
        g_printerr("[WS] Error: Received NULL user data.\n");
        return 0;
    }

    struct signaling_state* client_state = temp->client_state;
    struct lws *ws_client = temp->wsi;
    GstElement* webrtcbin = temp->webrtcbin;

    switch (reason) {

        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            g_print("[WS] Connected to signaling server.\n");
            ws_client = wsi;
            break;

        case LWS_CALLBACK_CLIENT_RECEIVE:
        {
            char *msg = strndup((char*)in, len);
            g_print("[WS] Received message: %s\n", msg);
            char *data = NULL;
            MessageType type = parse_json_message(msg, &data);
            free(msg);

            if (type == MSG_TYPE_SDP_OFFER && data) {
                g_print("[WS] Processing SDP offer.\n");
                GstSDPMessage *sdp = NULL;
                if (gst_sdp_message_new(&sdp) != GST_SDP_OK) {
                    g_printerr("[WS] Failed to allocate SDP message\n");
                    free(data);
                    break;
                }
                if (gst_sdp_message_parse_buffer((guint8*)data, strlen(data), sdp) != GST_SDP_OK) {
                    g_printerr("[WS] Failed to parse SDP offer\n");
                    gst_sdp_message_free(sdp);
                    free(data);
                    break;
                }
                GstWebRTCSessionDescription *offer =
                    gst_webrtc_session_description_new(GST_WEBRTC_SDP_TYPE_OFFER, sdp);
                update_offer_state(client_state, data);
                g_signal_emit_by_name(webrtcbin, "set-remote-description", offer, NULL);
                gst_webrtc_session_description_free(offer);

                /* Create an answer */
                GstPromise *promise = gst_promise_new_with_change_func(on_answer_created, NULL, NULL);
                g_signal_emit_by_name(webrtcbin, "create-answer", NULL, promise);
            } else if (type == MSG_TYPE_SDP_ANSWER && data) {
                g_print("[WS] Processing SDP answer.\n");
                GstSDPMessage *sdp = NULL;
                if (gst_sdp_message_new(&sdp) != GST_SDP_OK) {
                    g_printerr("[WS] Failed to allocate SDP message\n");
                    free(data);
                    break;
                }
                if (gst_sdp_message_parse_buffer((guint8*)data, strlen(data), sdp) != GST_SDP_OK) {
                    g_printerr("[WS] Failed to parse SDP answer\n");
                    gst_sdp_message_free(sdp);
                    free(data);
                    break;
                }
                GstWebRTCSessionDescription *answer =
                    gst_webrtc_session_description_new(GST_WEBRTC_SDP_TYPE_ANSWER, sdp);
                update_answer_state(client_state, data);
                g_signal_emit_by_name(webrtcbin, "set-remote-description", answer, NULL);
                gst_webrtc_session_description_free(answer);
            } else if (type == MSG_TYPE_ICE_CANDIDATE && data) {
                g_print("[WS] Processing ICE candidate: %s\n", data);
                add_ice_candidate_state(client_state, data);
                g_signal_emit_by_name(webrtcbin, "add-ice-candidate", 0, data);
            } else {
                g_print("[WS] Unknown or invalid signaling message received.\n");
            }
            free(data);
            break;
        }

        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            g_printerr("[WS] Connection error to signaling server.\n");
            ws_client = NULL;
            break;

        case LWS_CALLBACK_CLIENT_CLOSED:
            g_print("[WS] Signaling connection closed.\n");
            ws_client = NULL;
            break;

        default:
            break;
    }
    return 0;
}

static gboolean lws_service_cb(gpointer user_data) {
    struct lws_context *ws_context = (struct lws_context *)user_data;

    if (!ws_context) {
        g_printerr("[WS] ERROR: ws_context is NULL in lws_service_cb!\n");
        return FALSE;  // Stop the callback if ws_context is invalid
    }

    g_print("[LWS_SERVICE_CB] Calling lws_service...\n");
    lws_service(ws_context, 0);  // Process WebSocket events

    return TRUE;  // Keep running the callback
}

/* --- Main Function --- */
int main(int argc, char **argv) {

    //lws_set_log_level(0xFFFFFFFF, NULL);  // Enable all logging

    // Zero-initialize our resources structure.
    AppResources res = {0};

    struct arg_lit *help    = arg_lit0(NULL, "help", "Show help message");
    struct arg_int *port    = arg_int0("p", "port", "<port>", "Specify the signaling server port to connect to (default: 9000)");
    struct arg_str *address = arg_str0("a", "address", "<address>", "Specify the signaling server address to connect to (default: localhost)");
    struct arg_str *path    = arg_str0("pa", "path", "<path>", "Specify the path the signaling server accepts connections on (default: /)");
    struct arg_int *ssl     = arg_int0("ssl", "ssl", "<ssl>", "Specify whether to use SSL to connect to the signaling server (default: 0)");
    struct arg_str *pipeline_file = arg_str0("pf", "pipeline-file", "<pipeline-file>", "The name of the file to read the GStreamer pipelines. (default: pipelines.txt)");
    struct arg_int *pipeline_index = arg_int0("pi", "pipeline-index", "<pipeline-index>", "The index of the pipeline inside the pipeline file to use in the GStreamer pipelines. (default: 0)");
    struct arg_end *end     = arg_end(20);

    void *argtable[] = {help, port, address, path, ssl, pipeline_file, pipeline_index, end};

    static const int default_port = 9000;
    static const char *default_address = "localhost";
    static const char *default_path = "/";
    static const int default_ssl = 0;
    static const char *default_pipeline_file = "./pipelines.txt";
    static const int default_pipeline_index = 0;

    int nerrors = arg_parse(argc, argv, argtable);

    if (help->count > 0) {
        printf("Usage:\n");
        arg_print_syntax(stdout, argtable, "\n");
        arg_print_glossary(stdout, argtable, "  %-25s %s\n");
        goto cleanup;
        return 0;
    }

    if (nerrors > 0){
        arg_print_errors(stderr, end, argv[0]);
        printf("Try '--help' for more information.\n");
        goto cleanup;
        return 1;
    }

    // Assign default values if user hasn't provided any
    if (port->count == 0) port->ival[0] = default_port;
    if (address->count == 0) address->sval[0] = default_address;
    if (path->count == 0) path->sval[0] = default_path;
    if (ssl->count == 0) ssl->ival[0] = default_ssl;
    if (pipeline_file->count == 0) pipeline_file->sval[0] = default_pipeline_file;
    if (pipeline_index->count == 0) pipeline_index->ival[0] = default_pipeline_index;

    print_argtable(argtable);

    res.argtable = argtable;
    res.argtable_count = sizeof(argtable)/sizeof(void*);

    /* Allocate and initialize signaling state and context */
    struct signaling_state *current_signaling_state = calloc(1, sizeof(struct signaling_state));

    if(!current_signaling_state){
        printf("Failed to allocate signaling_state.\n");
        goto cleanup;
    }

    res.client_state = current_signaling_state;

    struct signaling_context *current_signaling_context = calloc(1, sizeof(struct signaling_context));

    if(!current_signaling_context){
        printf("Failed to allocate signaling_context.\n");
        goto cleanup;
    }

    res.sig_ctx = current_signaling_context;

    /* Set up libwebsockets client context */
    struct lws_context *ws_context;
    struct lws_context_creation_info ctx_creation_info;
    struct lws_client_connect_info client_connect_info;


    /* --- Libwebsockets Protocols --- */
    const struct lws_protocols ws_protocols[] = {
        {
            .name = "webrtc-protocol",
            .callback = ws_client_callback,
            .per_session_data_size = 0,
            .rx_buffer_size = 4096,
        },
        { NULL, NULL, 0, 0 }
    };

    CallbackHelper *curr_callback_helper = calloc(1, sizeof(CallbackHelper));

    if (!curr_callback_helper) {
        perror("Failed to allocate memory for CallbackHelper");
        return -1;
    }

    curr_callback_helper->client_state = current_signaling_state;

    res.callback_helper = curr_callback_helper;

    int result = initialize_values_of_libwebsocket_state(
        argtable, 
        &ctx_creation_info,
        &client_connect_info,
        &ws_context,
        ws_protocols,
        curr_callback_helper
    );

    if(result == -1){
        goto cleanup;
    }

    res.ws_context = ws_context;

    struct lws *ws_client = lws_client_connect_via_info(&client_connect_info);
    if (!ws_client) {
        g_printerr("[WS] Failed to connect to signaling server\n");
        goto cleanup;
    }

    curr_callback_helper->wsi = ws_client;
    res.ws_client = ws_client;

    GstElement *pipeline = NULL;
    GstElement *webrtcbin = NULL;
    int pipeline_count = 0;
    char pipelines[MAX_PIPELINES][MAX_LINE_LENGTH];

    result = read_pipelines_from_file(pipeline_file->sval[0], &pipelines, &pipeline_count);

    if(result == -1){
        goto cleanup;
    }
    
    char** custom_argv = parse_for_gstreamer_args(argtable);
    int custom_argc = sizeof(argv) / sizeof(argv[0]) - 1;  // Exclude NULL
    //This is required to be called inside main
    gst_init(&custom_argc, &custom_argv);

    curr_callback_helper->webrtcbin = webrtcbin;

    // Define an array of SignalCallback entries.
    SignalCallback callbacks[] = {
        {"on-negotiation-needed", G_CALLBACK(on_negotiation_needed), NULL},
        {"on-ice-candidate",      G_CALLBACK(on_ice_candidate),      curr_callback_helper},
        {"pad-added",             G_CALLBACK(on_pad_added),          NULL},
        {NULL, NULL, NULL} // Sentinel entry to mark the end
        // Add more callbacks here as needed in the future.
    };

    result = initialize_gstreamer_state(pipeline, webrtcbin, callbacks, pipelines[pipeline_index->ival[0]]);

    if(result == -1){
        goto cleanup;
    }

    res.pipeline = pipeline; 
    res.webrtcbin = webrtcbin;
    /* Create and run the GLib main loop, adding a timeout to pump lws_service */
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);

    res.loop = loop;

    g_timeout_add(100, (GSourceFunc)lws_service_cb, ws_context);
    g_print("[Main] Starting main loop...\n");
    g_main_loop_run(loop);



    cleanup:
        free_app_resources(&res);

    return 0;
}
