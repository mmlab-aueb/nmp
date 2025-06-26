#include <gst/gst.h>
#include <gst/webrtc/webrtc.h>
#include <glib.h>
#include <libwebsockets.h>
#include <string.h>
#include <stdlib.h>
#include <message_protocol.h>   // For create_json_message() and parse_json_message()
#include <message_handling.h>   // For send_data_to_client() and process_incoming_message()

/* --- Global Variables --- */
static GstElement *pipeline = NULL;
static GstElement *webrtcbin = NULL;
static struct lws_context *ws_context = NULL;
static struct lws *ws_client = NULL;

/* Global signaling state for the client.
 * This state stores the local SDP offer, remote SDP answer, and ICE candidates.
 */
static struct signaling_state *client_state = NULL;

/* --- Helper Functions to Update signaling_state --- */
static void update_offer_state(const char *sdp_str)
{
    if (!client_state->sdp_offer || strcmp(client_state->sdp_offer, sdp_str) != 0) {
        free(client_state->sdp_offer);
        client_state->sdp_offer = strdup(sdp_str);
        client_state->offer_version = ++client_state->global_version_counter;
        g_print("[State] Updated local SDP offer (version %lu).\n", client_state->offer_version);
    } else {
        g_print("[State] Received identical SDP offer; no update needed.\n");
    }
}

static void update_answer_state(const char *sdp_str)
{
    if (!client_state->sdp_answer || strcmp(client_state->sdp_answer, sdp_str) != 0) {
        free(client_state->sdp_answer);
        client_state->sdp_answer = strdup(sdp_str);
        client_state->answer_version = ++client_state->global_version_counter;
        g_print("[State] Updated remote SDP answer (version %lu).\n", client_state->answer_version);
    } else {
        g_print("[State] Received identical SDP answer; no update needed.\n");
    }
}

static void add_ice_candidate_state(const char *candidate)
{
    /* For simplicity, we just add every candidate.
       In a full implementation, you might want to check for duplicates. */
    g_ptr_array_add(client_state->ice_candidates, strdup(candidate));
    g_print("[State] Added ICE candidate to state.\n");
}

/* --- WebRTC Callbacks --- */

/* Called when an SDP offer has been created.
 * It sets the local description, updates the signaling state, and sends the offer.
 */
static void on_offer_created(GstPromise *promise, gpointer user_data)
{
    GstWebRTCSessionDescription *offer = NULL;
    const GstStructure *reply = gst_promise_get_reply(promise);
    gst_structure_get(reply, "offer", GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &offer, NULL);
    gst_promise_unref(promise);

    if (!offer) {
        g_printerr("[WebRTC] Failed to create offer\n");
        return;
    }

    gchar *sdp_str = gst_sdp_message_as_text(offer->sdp);
    g_print("[WebRTC] Created SDP offer:\n%s\n", sdp_str);

    /* Update our local signaling state with the new offer */
    update_offer_state(sdp_str);

    /* Set the local description on webrtcbin */
    g_signal_emit_by_name(webrtcbin, "set-local-description", offer, NULL);

    /* Create a JSON message and send it via the signaling server */
    char *json = create_json_message("sdp_offer", sdp_str);
    if (json) {
        if (send_data_to_client(ws_client, json) == 0)
            g_print("[WebRTC] Sent SDP offer via signaling server.\n");
        else
            g_printerr("[WebRTC] Failed to send SDP offer.\n");
        free(json);
    } else {
        g_printerr("[WebRTC] Failed to create JSON for SDP offer.\n");
    }

    g_free(sdp_str);
    gst_webrtc_session_description_free(offer);
}

/* Called when negotiation is needed (typically at startup) */
static void on_negotiation_needed(GstElement *webrtc, gpointer user_data)
{
    g_print("[WebRTC] Negotiation needed, creating offer...\n");
    /* Create a promise that will trigger on_offer_created() when the offer is ready */
    GstPromise *promise = gst_promise_new_with_change_func(
        (GstPromiseChangeFunc) on_offer_created, user_data, NULL);
    g_signal_emit_by_name(webrtc, "create-offer", NULL, promise);
}


/* Called when an ICE candidate is generated.
 * It updates the state and sends the candidate via the signaling server.
 */
static void on_ice_candidate(GstElement *webrtc, guint mline_index, gchar *candidate, gpointer user_data)
{
    if (candidate == NULL || strlen(candidate) == 0) {
        g_print("[WebRTC] Ignoring empty ICE candidate message.\n");
        return;
    }
    g_print("[WebRTC] ICE candidate generated: %s\n", candidate);
    add_ice_candidate_state(candidate);
    char *json = create_json_message("ice_candidate", candidate);
    if (json) {
        if (send_data_to_client(ws_client, json) == 0)
            g_print("[WebRTC] Sent ICE candidate via signaling server.\n");
        else
            g_printerr("[WebRTC] Failed to send ICE candidate.\n");
        free(json);
    } else {
        g_printerr("[WebRTC] Failed to create JSON for ICE candidate.\n");
    }
}

/* --- Libwebsockets Client Callback --- */
static int ws_client_callback(struct lws *wsi, enum lws_callback_reasons reason,
                              void *user, void *in, size_t len)
{
    switch (reason) {

    case LWS_CALLBACK_CLIENT_ESTABLISHED:
        g_print("[WS] Connected to signaling server.\n");
        ws_client = wsi;
        break;

    case LWS_CALLBACK_CLIENT_RECEIVE:
    {
        /* Process incoming signaling messages */
        char *msg = strndup((char*)in, len);
        g_print("[WS] Received message: %s\n", msg);
        char *data = NULL;
        MessageType type = parse_json_message(msg, &data);
        free(msg);

        if (type == MSG_TYPE_SDP_ANSWER && data) {
            g_print("[WS] Processing SDP answer.\n");
            GstSDPMessage *sdp;
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
            /* Update our remote answer state */
            update_answer_state(data);
            g_signal_emit_by_name(webrtcbin, "set-remote-description", answer, NULL);
            gst_webrtc_session_description_free(answer);
        } else if (type == MSG_TYPE_ICE_CANDIDATE && data) {
            g_print("[WS] Processing ICE candidate: %s\n", data);
            add_ice_candidate_state(data);
            /* For simplicity, assume mline_index 0; adjust as needed */
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

/* Libwebsockets protocols for the client */
static const struct lws_protocols ws_protocols[] = {
    {
        .name = "webrtc-protocol",
        .callback = ws_client_callback,
        .per_session_data_size = 0,
        .rx_buffer_size = 4096,
    },
    { NULL, NULL, 0, 0 }
};

/* --- GLib Main Loop Timeout for lws_service --- */
static gboolean lws_service_cb(gpointer user_data)
{
    if (ws_context)
        lws_service(ws_context, 0);
    return TRUE; /* continue calling */
}

/* --- Main Function --- */
int main(int argc, char **argv)
{
    gst_init(&argc, &argv);

    /* Allocate and initialize our client signaling state */
    client_state = calloc(1, sizeof(struct signaling_state));
    if (!client_state) {
        g_printerr("[State] Failed to allocate client signaling state.\n");
        return -1;
    }
    client_state->ice_candidates = g_ptr_array_new();

    /* Create the GStreamer pipeline.
     * (Adjust the pipeline string to add audio if desired.)
     */
    pipeline = gst_parse_launch(
        "v4l2src device=/dev/video0 ! videoconvert ! tee name=t "
        "t. ! queue ! videoconvert ! autovideosink sync=false "
        "t. ! queue ! videoconvert ! vp8enc ! rtpvp8pay ! rtprtxsend ! "
        "webrtcbin name=webrtcbin",
        NULL);
    if (!pipeline) {
        g_printerr("[Main] Failed to create pipeline\n");
        return -1;
    }

    /* Retrieve webrtcbin from the pipeline */
    webrtcbin = gst_bin_get_by_name(GST_BIN(pipeline), "webrtcbin");
    if (!webrtcbin) {
        g_printerr("[Main] Failed to get webrtcbin element\n");
        gst_object_unref(pipeline);
        return -1;
    }

    /* Connect WebRTC signals */
    g_signal_connect(webrtcbin, "on-negotiation-needed", G_CALLBACK(on_negotiation_needed), NULL);
    g_signal_connect(webrtcbin, "on-ice-candidate", G_CALLBACK(on_ice_candidate), NULL);

    /* Start playing the pipeline */
    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    /* Set up the libwebsockets client context */
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = CONTEXT_PORT_NO_LISTEN; /* Client mode */
    info.protocols = ws_protocols;
    ws_context = lws_create_context(&info);
    if (!ws_context) {
        g_printerr("[WS] Failed to create libwebsockets context\n");
        gst_object_unref(webrtcbin);
        gst_object_unref(pipeline);
        return -1;
    }

    /* Connect to the signaling server.
     * Adjust the address, port, and path as needed.
     */
    struct lws_client_connect_info ccinfo = {0};
    ccinfo.context = ws_context;
    ccinfo.address = "localhost";   /* Signaling server hostname */
    ccinfo.port = 9000;             /* Signaling server port */
    ccinfo.path = "/";
    ccinfo.host = lws_canonical_hostname(ws_context);
    ccinfo.origin = "origin";
    ccinfo.protocol = ws_protocols[0].name;
    ccinfo.ssl_connection = 0;      /* Use SSL if required */
    ws_client = lws_client_connect_via_info(&ccinfo);
    if (!ws_client) {
        g_printerr("[WS] Failed to connect to signaling server\n");
        lws_context_destroy(ws_context);
        gst_object_unref(webrtcbin);
        gst_object_unref(pipeline);
        return -1;
    }

    /* Set up a GLib main loop and add a timeout to pump lws_service */
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_timeout_add(10, lws_service_cb, NULL);
    g_print("[Main] Starting main loop...\n");
    g_main_loop_run(loop);

    /* Cleanup */
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(webrtcbin);
    gst_object_unref(pipeline);
    lws_context_destroy(ws_context);
    g_main_loop_unref(loop);

    /* Free our signaling state */
    free(client_state->sdp_offer);
    free(client_state->sdp_answer);
    if (client_state->ice_candidates) {
        for (guint i = 0; i < client_state->ice_candidates->len; i++) {
            free(g_ptr_array_index(client_state->ice_candidates, i));
        }
        g_ptr_array_free(client_state->ice_candidates, TRUE);
    }
    free(client_state);

    return 0;
}
