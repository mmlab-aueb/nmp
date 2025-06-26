// receiver_client.c

#include <gst/gst.h>
#include <gst/webrtc/webrtc.h>
//#include <gst/rtp/gstrtpbin.h>
#include <glib.h>
#include <libwebsockets.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "message_protocol.h"   // For create_json_message() and parse_json_message()
#include "message_handling.h"   // For send_data_to_client()
#include "client_utils.h"       // For print_lws_* and read_pipelines_from_file()

/* --- Global Variables --- */
static GstElement *pipeline      = NULL;
static GstElement *webrtcbin     = NULL;
static struct lws_context *ws_context = NULL;
static struct lws *ws_client     = NULL;
static struct signaling_state *client_state = NULL;

// role assignment
static bool is_sender     = false;
static bool role_received = false;

// Define a structure to hold all allocated resources.
typedef struct AppResources {
    GstElement *pipeline;
    GstElement *webrtcbin;
    struct lws_context *ws_context;
    struct lws *ws_client;
    GMainLoop *loop;
    struct signaling_state *client_state;
    void **argtable;
    size_t argtable_count;
} AppResources;

static void free_app_resources(AppResources *res)
{
    if (!res) return;

    /* 1. Cleanup GStreamer pipeline */
    if (res->pipeline) {
        gst_element_set_state(res->pipeline, GST_STATE_NULL);
        if (res->webrtcbin)
            gst_object_unref(res->webrtcbin);
        gst_object_unref(res->pipeline);
    }

    /* 2. Destroy libwebsockets context */
    if (res->ws_context)
        lws_context_destroy(res->ws_context);

    /* 3. Unreference the GLib main loop */
    if (res->loop)
        g_main_loop_unref(res->loop);

    /* 4. Cleanup client_state */
    if (res->client_state) {
        free(res->client_state->sdp_offer);
        free(res->client_state->sdp_answer);
        if (res->client_state->ice_candidates) {
            for (guint i = 0; i < res->client_state->ice_candidates->len; i++)
                free(g_ptr_array_index(res->client_state->ice_candidates, i));
            g_ptr_array_free(res->client_state->ice_candidates, TRUE);
        }
        free(res->client_state);
    }

    /* 5. Free argtable resources, if used */
    if (res->argtable)
        arg_freetable(res->argtable, res->argtable_count);
}

/* --- State‑update Helpers for receiver --- */
static void receiver_update_offer_state(const char *sdp_str)
{
    if (!client_state->sdp_offer || strcmp(client_state->sdp_offer, sdp_str) != 0) {
        free(client_state->sdp_offer);
        client_state->sdp_offer = strdup(sdp_str);
        client_state->offer_version = ++client_state->global_version_counter;
        g_print("[State] Updated remote SDP offer (version %lu).\n", client_state->offer_version);
    } else {
        g_print("[State] Received identical SDP offer; no update needed.\n");
    }
}

static void receiver_update_answer_state(const char *sdp_str)
{
    if (!client_state->sdp_answer || strcmp(client_state->sdp_answer, sdp_str) != 0) {
        free(client_state->sdp_answer);
        client_state->sdp_answer = strdup(sdp_str);
        client_state->answer_version = ++client_state->global_version_counter;
        g_print("[State] Updated local SDP answer (version %lu).\n", client_state->answer_version);
    } else {
        g_print("[State] Received identical SDP answer; no update needed.\n");
    }
}

static void receiver_add_ice_candidate_state(const char *candidate)
{
    g_ptr_array_add(client_state->ice_candidates, strdup(candidate));
    g_print("[State] Added ICE candidate to state: %s.\n", candidate);
}

/* --- State‑update Helpers for sender (in case role flips) --- */
static void sender_update_offer_state(const char *sdp_str)
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

static void sender_update_answer_state(const char *sdp_str)
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

static void sender_add_ice_candidate_state(const char *candidate)
{
    g_ptr_array_add(client_state->ice_candidates, strdup(candidate));
    g_print("[State] Added ICE candidate to state.\n");
}

/* --- WebRTC Callbacks for sender role --- */
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

    sender_update_offer_state(sdp_str);
    g_signal_emit_by_name(webrtcbin, "set-local-description", offer, NULL);

    char *json = create_json_message("sdp_offer", sdp_str);
    if (json) {
        if (send_data_to_client(ws_client, json) == 0)
            g_print("[WebRTC] Sent SDP offer.\n");
        else
            g_printerr("[WebRTC] Failed to send SDP offer.\n");
        free(json);
    } else {
        g_printerr("[WebRTC] Couldn't create JSON for offer.\n");
    }

    g_free(sdp_str);
    gst_webrtc_session_description_free(offer);
}

static void on_negotiation_needed(GstElement *webrtc, gpointer user_data)
{
    g_print("[WebRTC] Negotiation needed, creating offer...\n");
    GstPromise *promise = gst_promise_new_with_change_func(
        (GstPromiseChangeFunc)on_offer_created, NULL, NULL);
    g_signal_emit_by_name(webrtc, "create-offer", NULL, promise);
}

/* --- WebRTC Callbacks shared by both roles --- */
static void on_ice_candidate(GstElement *webrtc, guint mline_index, gchar *candidate, gpointer user_data)
{
    if (!candidate || strlen(candidate) == 0) {
        g_print("[WebRTC] Ignoring empty ICE candidate.\n");
        return;
    }
    g_print("[WebRTC] ICE candidate: %s\n", candidate);

    if (is_sender)
        sender_add_ice_candidate_state(candidate);
    else
        receiver_add_ice_candidate_state(candidate);

    char *json = create_json_message("ice_candidate", candidate);
    if (json) {
        if (send_data_to_client(ws_client, json) == 0)
            g_print("[WebRTC] Sent ICE candidate.\n");
        else
            g_printerr("[WebRTC] Failed to send ICE candidate.\n");
        free(json);
    }
}

/* --- WebRTC Callback for receiver role --- */
static void receiver_on_answer_created(GstPromise *promise, gpointer user_data)
{
    GstWebRTCSessionDescription *answer = NULL;
    const GstStructure *reply = gst_promise_get_reply(promise);
    gst_structure_get(reply, "answer", GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &answer, NULL);
    gst_promise_unref(promise);
    if (!answer) {
        g_printerr("[WebRTC] Failed to create answer\n");
        return;
    }

    gchar *sdp_str = gst_sdp_message_as_text(answer->sdp);
    g_print("[WebRTC] Created SDP answer:\n%s\n", sdp_str);

    receiver_update_answer_state(sdp_str);
    g_signal_emit_by_name(webrtcbin, "set-local-description", answer, NULL);

    char *json = create_json_message("sdp_answer", sdp_str);
    if (json) {
        if (send_data_to_client(ws_client, json) == 0)
            g_print("[WebRTC] Sent SDP answer.\n");
        else
            g_printerr("[WebRTC] Failed to send SDP answer.\n");
        free(json);
    }

    g_free(sdp_str);
    gst_webrtc_session_description_free(answer);
}

/* --- Setup pipeline after role is known --- */
static void setup_pipeline_for_role()
{
    if (is_sender) {
        g_signal_connect(webrtcbin, "on-negotiation-needed",
                         G_CALLBACK(on_negotiation_needed), NULL);
        g_signal_connect(webrtcbin, "on-ice-candidate",
                         G_CALLBACK(on_ice_candidate), NULL);
        g_print("[ROLE] Sender mode: hooked negotiation + ICE callbacks.\n");
    } else {
        g_signal_connect(webrtcbin, "on-ice-candidate",
                         G_CALLBACK(on_ice_candidate), NULL);
        g_print("[ROLE] Receiver mode: hooked ICE callback.\n");
    }

    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    g_print("[ROLE] Pipeline set to PLAYING.\n");
}

/* --- Libwebsockets Client Callback --- */
static int ws_client_callback(struct lws *wsi,
                              enum lws_callback_reasons reason,
                              void *user, void *in, size_t len)
{
    switch (reason) {

    case LWS_CALLBACK_CLIENT_ESTABLISHED:
        g_print("[WS] Connected to signaling server.\n");
        ws_client = wsi;
        break;

    case LWS_CALLBACK_CLIENT_RECEIVE:
    {
        char *msg = strndup((char*)in, len);
        g_print("[WS] Received: %s\n", msg);
        char *data = NULL;
        MessageType type = parse_json_message(msg, &data);
        free(msg);

        if (type == MSG_TYPE_ROLE && data) {
            is_sender     = (strcmp(data, "sender") == 0);
            role_received = true;
            g_print("[WS] Role assigned: %s\n", data);
            setup_pipeline_for_role();
            free(data);
            break;
        }

        if (!role_received) {
            g_print("[WS] Ignoring message until role is assigned.\n");
            free(data);
            break;
        }

        if (!is_sender && type == MSG_TYPE_SDP_OFFER && data) {
            // receiver: got an offer → set-remote + create-answer
            g_print("[WS] Processing SDP offer.\n");
            GstSDPMessage *sdp = NULL;
            if (gst_sdp_message_new(&sdp) != GST_SDP_OK ||
                gst_sdp_message_parse_buffer((guint8*)data, strlen(data), sdp) != GST_SDP_OK) {
                g_printerr("[WS] SDP offer parse error\n");
                if (sdp) gst_sdp_message_free(sdp);
                free(data);
                break;
            }
            GstWebRTCSessionDescription *offer =
                gst_webrtc_session_description_new(GST_WEBRTC_SDP_TYPE_OFFER, sdp);
            receiver_update_offer_state(data);
            g_signal_emit_by_name(webrtcbin, "set-remote-description", offer, NULL);
            gst_webrtc_session_description_free(offer);

            GstPromise *promise = gst_promise_new_with_change_func(
                (GstPromiseChangeFunc)receiver_on_answer_created, NULL, NULL);
            g_signal_emit_by_name(webrtcbin, "create-answer", NULL, promise);
        }
        else if (is_sender && type == MSG_TYPE_SDP_ANSWER && data) {
            // sender: got an answer → set-remote
            g_print("[WS] Processing SDP answer.\n");
            GstSDPMessage *sdp = NULL;
            if (gst_sdp_message_new(&sdp) != GST_SDP_OK ||
                gst_sdp_message_parse_buffer((guint8*)data, strlen(data), sdp) != GST_SDP_OK) {
                g_printerr("[WS] SDP answer parse error\n");
                if (sdp) gst_sdp_message_free(sdp);
                free(data);
                break;
            }
            GstWebRTCSessionDescription *answer =
                gst_webrtc_session_description_new(GST_WEBRTC_SDP_TYPE_ANSWER, sdp);
            sender_update_answer_state(data);
            g_signal_emit_by_name(webrtcbin, "set-remote-description", answer, NULL);
            gst_webrtc_session_description_free(answer);
        }
        else if (type == MSG_TYPE_ICE_CANDIDATE && data) {
            // both roles: add ICE
            g_print("[WS] Processing ICE candidate: %s\n", data);
            g_signal_emit_by_name(webrtcbin, "add-ice-candidate", 0, data);
            if (is_sender)
                sender_add_ice_candidate_state(data);
            else
                receiver_add_ice_candidate_state(data);
        }

        free(data);
        break;
    }

    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
        g_printerr("[WS] Connection error.\n");
        ws_client = NULL;
        break;

    case LWS_CALLBACK_CLIENT_CLOSED:
        g_print("[WS] Connection closed.\n");
        ws_client = NULL;
        break;

    default:
        break;
    }
    return 0;
}

/* Libwebsockets protocols */
static const struct lws_protocols ws_protocols[] = {
    { .name               = "webrtc-protocol",
      .callback           = ws_client_callback,
      .per_session_data_size = 0,
      .rx_buffer_size     = 4096 },
    { NULL, NULL, 0, 0 }
};

/* GLib main loop pump */
static gboolean lws_service_cb(gpointer user_data)
{
    if (ws_context)
        lws_service(ws_context, 0);
    return TRUE;
}

/* --- Main Function --- */
int main(int argc, char **argv)
{
    AppResources res = {0};

    gst_init(&argc, &argv);

    /* Read pipelines from file */
    int pipeline_count = 0;
    char pipelines[MAX_PIPELINES][MAX_LINE_LENGTH];
    if (read_pipelines_from_file("./../client/pipelines2.txt",
                                 pipelines, &pipeline_count) < 0) {
        g_printerr("[Main] Failed to read pipelines file\n");
        return -1;
    }

    /* Allocate signaling state */
    client_state = calloc(1, sizeof(struct signaling_state));
    if (!client_state) {
        g_printerr("[Main] Failed to allocate signaling_state\n");
        goto cleanup;
    }
    client_state->ice_candidates = g_ptr_array_new();
    res.client_state = client_state;

    /* Build the GStreamer pipeline */
    pipeline = gst_parse_launch(pipelines[0], NULL);
    if (!pipeline) {
        g_printerr("[Main] Failed to create pipeline\n");
        goto cleanup;
    }
    res.pipeline = pipeline;

    webrtcbin = gst_bin_get_by_name(GST_BIN(pipeline), "webrtcbin");
    if (!webrtcbin) {
        g_printerr("[Main] Failed to get webrtcbin\n");
        goto cleanup;
    }
    res.webrtcbin = webrtcbin;

    /* Defer hooking signals and starting until after role assignment */

    /* Create libwebsockets context */
    struct lws_context_creation_info info = {0};
    info.port      = CONTEXT_PORT_NO_LISTEN;
    info.protocols = ws_protocols;
    print_lws_context_info(&info);
    ws_context = lws_create_context(&info);
    if (!ws_context) {
        g_printerr("[Main] Failed to create lws context\n");
        goto cleanup;
    }
    res.ws_context = ws_context;

    /* Connect to signaling server */
    struct lws_client_connect_info ccinfo = {0};
    ccinfo.context        = ws_context;
    ccinfo.address        = "localhost";
    ccinfo.port           = 9000;
    ccinfo.path           = "/";
    ccinfo.host           = lws_canonical_hostname(ws_context);
    ccinfo.origin         = "origin";
    ccinfo.protocol       = ws_protocols[0].name;
    ccinfo.ssl_connection = 0;
    print_lws_client_info(&ccinfo);
    ws_client = lws_client_connect_via_info(&ccinfo);
    if (!ws_client) {
        g_printerr("[Main] Failed to connect to signaling server\n");
        goto cleanup;
    }
    res.ws_client = ws_client;

    /* Run the GLib main loop */
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    res.loop = loop;
    g_timeout_add(10, lws_service_cb, NULL);
    g_print("[Main] Starting main loop...\n");
    g_main_loop_run(loop);

cleanup:
    free_app_resources(&res);
    return 0;
}
