#include "unified_client.h"

/* ─── Globals ─────────────────────────────────────────────────────────────── */
GstElement             *pipeline      = NULL;
GstElement             *webrtcbin     = NULL;
struct lws_context     *ws_context    = NULL;
struct lws             *ws_client     = NULL;
struct signaling_state *client_state  = NULL;
bool is_sender     = false;
bool role_received = false;

/* ─── Resource cleanup ───────────────────────────────────────────────────── */
void free_app_resources(AppResources *res) {
    if (!res) return;
    if (res->pipeline) {
        gst_element_set_state(res->pipeline, GST_STATE_NULL);
        if (res->webrtcbin) gst_object_unref(res->webrtcbin);
        gst_object_unref(res->pipeline);
    }
    if (res->ws_context) lws_context_destroy(res->ws_context);
    if (res->loop)      g_main_loop_unref(res->loop);
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
    if (res->argtable)
        arg_freetable(res->argtable, res->argtable_count);
}

/* ─── Merged State‑update helpers ─────────────────────────────────────────── */
void state_update_offer(bool local, const char *sdp) {
    char **field = &client_state->sdp_offer;
    if (!*field || strcmp(*field, sdp) != 0) {
        free(*field);
        *field = strdup(sdp);
        client_state->offer_version = ++client_state->global_version_counter;
        g_print("[State] Updated %s SDP offer (version %lu)\n",
                local ? "local" : "remote",
                client_state->offer_version);
    }
}

void state_update_answer(bool local, const char *sdp) {
    char **field = &client_state->sdp_answer;
    if (!*field || strcmp(*field, sdp) != 0) {
        free(*field);
        *field = strdup(sdp);
        client_state->answer_version = ++client_state->global_version_counter;
        g_print("[State] Updated %s SDP answer (version %lu)\n",
                local ? "local" : "remote",
                client_state->answer_version);
    }
}

void state_add_ice(const char *cand) {
    g_ptr_array_add(client_state->ice_candidates, strdup(cand));
    g_print("[State] Added ICE candidate to state\n");
}

/* ─── WebRTC Callbacks ───────────────────────────────────────────────────── */
void on_offer_created(GstPromise *promise, gpointer _) {
    GstWebRTCSessionDescription *offer = NULL;
    const GstStructure *reply = gst_promise_get_reply(promise);
    gst_structure_get(reply, "offer",
                     GST_TYPE_WEBRTC_SESSION_DESCRIPTION,
                     &offer, NULL);
    gst_promise_unref(promise);

    gchar *sdp = gst_sdp_message_as_text(offer->sdp);
    g_print("[WebRTC] Created SDP offer:\n%s\n", sdp);

    state_update_offer(true, sdp);
    g_signal_emit_by_name(webrtcbin, "set-local-description", offer, NULL);

    char *msg = create_json_message("sdp_offer", sdp);
    send_data_to_client(ws_client, msg);
    free(msg);

    g_free(sdp);
    gst_webrtc_session_description_free(offer);
}

void on_negotiation_needed(GstElement *webrtc, gpointer _) {
    g_print("[WebRTC] Negotiation needed\n");
    GstPromise *p = gst_promise_new_with_change_func(
        (GstPromiseChangeFunc)on_offer_created, NULL, NULL);
    g_signal_emit_by_name(webrtc, "create-offer", NULL, p);
}

void on_ice_candidate(GstElement *webrtc, guint mline, gchar *candidate, gpointer _) {
    if (!candidate || !*candidate) return;
    g_print("[WebRTC] ICE candidate generated: %s\n", candidate);

    state_add_ice(candidate);

    char *msg = create_json_message("ice_candidate", candidate);
    send_data_to_client(ws_client, msg);
    free(msg);
}

void on_answer_created(GstPromise *promise, gpointer _) {
    GstWebRTCSessionDescription *answer = NULL;
    const GstStructure *reply = gst_promise_get_reply(promise);
    gst_structure_get(reply, "answer",
                     GST_TYPE_WEBRTC_SESSION_DESCRIPTION,
                     &answer, NULL);
    gst_promise_unref(promise);

    gchar *sdp = gst_sdp_message_as_text(answer->sdp);
    g_print("[WebRTC] Created SDP answer:\n%s\n", sdp);

    state_update_answer(true, sdp);
    g_signal_emit_by_name(webrtcbin, "set-local-description", answer, NULL);

    char *msg = create_json_message("sdp_answer", sdp);
    send_data_to_client(ws_client, msg);
    free(msg);

    g_free(sdp);
    gst_webrtc_session_description_free(answer);
}

/* ─── Build & start pipeline once role is known ──────────────────────────── */
void setup_pipeline_for_role() {
    const char *file = is_sender ? PIPELINES_SENDER : PIPELINES_RECEIVER;
    g_print("[ROLE] I am %s. Loading '%s'\n",
            is_sender ? "SENDER" : "RECEIVER", file);

    int n = 0;
    char pipelines[MAX_PIPELINES][MAX_LINE_LENGTH];
    if (read_pipelines_from_file(
            file,
            (char * (*)[MAX_LINE_LENGTH])pipelines,
            &n) < 0) {
        g_printerr("[ROLE] Failed to read %s\n", file);
        return;
    }

    /* signal callbacks for both roles */
    SignalCallback callbacks[] = {
        { "on-negotiation-needed", G_CALLBACK(on_negotiation_needed), NULL },
        { "on-ice-candidate",      G_CALLBACK(on_ice_candidate),      NULL },
        { NULL, NULL, NULL }
      };
  
      if (initialize_gstreamer_state(
              &pipeline,
              &webrtcbin,
              callbacks,
              pipelines[0]) < 0)
      {
        g_printerr("[ROLE] Failed to init pipeline via helper\n");
        return;
      }
      g_print("[ROLE] Pipeline initialized via helper\n");

}

/* ─── Signaling callback ─────────────────────────────────────────────────── */
int ws_client_callback(struct lws *wsi,
                       enum lws_callback_reasons reason,
                       void *user, void *in, size_t len)
{
    switch (reason) {
      case LWS_CALLBACK_CLIENT_ESTABLISHED:
        g_print("[WS] Connected to signaling server\n");
        ws_client = wsi;
        break;

      case LWS_CALLBACK_CLIENT_RECEIVE: {
        char *raw = strndup((char*)in, len);
        char *data = NULL;
        MessageType type = parse_json_message(raw, &data);
        free(raw);

        if (type == MSG_TYPE_ROLE && data) {
          is_sender     = (strcmp(data, "sender") == 0);
          role_received = true;
          g_print("[WS] Received role: %s\n", data);
          setup_pipeline_for_role();
          free(data);
          break;
        }
        if (!role_received) {
          free(data);
          break;
        }

        if (!is_sender && type == MSG_TYPE_SDP_OFFER && data) {
          GstSDPMessage *sdp = NULL;
          gst_sdp_message_new(&sdp);
          gst_sdp_message_parse_buffer((guint8*)data, strlen(data), sdp);
          GstWebRTCSessionDescription *offer =
            gst_webrtc_session_description_new(
              GST_WEBRTC_SDP_TYPE_OFFER, sdp);
          state_update_offer(false, data);
          g_signal_emit_by_name(webrtcbin,
                                "set-remote-description",
                                offer, NULL);
          gst_webrtc_session_description_free(offer);

          GstPromise *p = gst_promise_new_with_change_func(
              (GstPromiseChangeFunc)on_answer_created,
              NULL, NULL);
          g_signal_emit_by_name(webrtcbin,
                                "create-answer",
                                NULL, p);
        }
        else if (is_sender && type == MSG_TYPE_SDP_ANSWER && data) {
          GstSDPMessage *sdp = NULL;
          gst_sdp_message_new(&sdp);
          gst_sdp_message_parse_buffer((guint8*)data, strlen(data), sdp);
          GstWebRTCSessionDescription *answer =
            gst_webrtc_session_description_new(
              GST_WEBRTC_SDP_TYPE_ANSWER, sdp);
          state_update_answer(false, data);
          g_signal_emit_by_name(webrtcbin,
                                "set-remote-description",
                                answer, NULL);
          gst_webrtc_session_description_free(answer);
        }
        else if (type == MSG_TYPE_ICE_CANDIDATE && data) {
          state_add_ice(data);
          g_signal_emit_by_name(webrtcbin,
                                "add-ice-candidate",
                                0, data);
        }

        free(data);
        break;
      }

      default:
        break;
    }
    return 0;
}

/* ─── Protocols & main loop ──────────────────────────────────────────────── */
static const struct lws_protocols ws_protocols[] = {
    { "webrtc-protocol", ws_client_callback, 0, 4096 },
    { NULL, NULL, 0, 0 }
};

gboolean lws_service_callback(gpointer user_data) {
    struct lws_context *ctx = user_data;
    if (ctx)
        lws_service(ctx, 0);
    return G_SOURCE_CONTINUE;
}

int main(int argc, char **argv) {
    lws_set_log_level(0xFFFFFFFF, NULL);  // Enable all logging

    AppResources res = {0};

    struct arg_lit *help    = arg_lit0(NULL, "help", "Show help message");
    struct arg_int *port    = arg_int0("p", "port", "<port>", "Signaling server port (default 9000)");
    struct arg_str *address = arg_str0("a", "address", "<address>", "Server address (default localhost)");
    struct arg_str *path    = arg_str0("pa", "path", "<path>", "Server path (default /)");
    struct arg_int *ssl     = arg_int0("ssl", "ssl", "<ssl>", "Use SSL? 0 or 1 (default 0)");
    struct arg_str *pipeline_file = arg_str0("pf", "pipeline-file", "<pipeline-file>", "Pipeline file (default pipelines.txt)");
    struct arg_int *pipeline_index = arg_int0("pi", "pipeline-index", "<pipeline-index>", "Pipeline index (default 0)");
    struct arg_end *end     = arg_end(20);

    void *argtable[] = { help, port, address, path, ssl, pipeline_file, pipeline_index, end };
    int nerrors = arg_parse(argc, argv, argtable);

    struct arg_int *port_arg           = (struct arg_int *)argtable[1];
    struct arg_str *address_arg        = (struct arg_str *)argtable[2];
    struct arg_str *path_arg           = (struct arg_str *)argtable[3];
    struct arg_int *ssl_arg            = (struct arg_int *)argtable[4];
    struct arg_str *pipeline_file_arg  = (struct arg_str *)argtable[5];
    struct arg_int *pipeline_index_arg = (struct arg_int *)argtable[6];

    if (port_arg->count    == 0) port_arg->ival[0]      = 9000;
    if (address_arg->count == 0) address_arg->sval[0]   = "localhost";
    if (path_arg->count    == 0) path_arg->sval[0]      = "/";
    if (ssl_arg->count     == 0) ssl_arg->ival[0]       = 0;
    if (pipeline_file_arg->count == 0) pipeline_file_arg->sval[0] = "pipelines1.txt";
    if (pipeline_index_arg->count == 0) pipeline_index_arg->ival[0] = 0;

    print_argtable(argtable);

    if (help->count) {
        arg_print_syntax(stdout, argtable, "\n");
        arg_print_glossary(stdout, argtable,  "  %-25s %s\n");
        return 0;
    }
    if (nerrors) {
        arg_print_errors(stderr, end, argv[0]);
        return 1;
    }

    int   p   = port->count    ? port->ival[0]    : 9000;
    char *a   = address->count ? address->sval[0] : "localhost";
    char *pa  = path->count    ? path->sval[0]    : "/";
    bool  use_ssl = ssl->count != 0;

    arg_print_syntax(stderr, argtable, "\n");
    fprintf(stderr, "-> connecting to %s:%d%s over %s\n",
            a, p, pa, use_ssl ? "SSL" : "cleartext");

    char **gst_argv = parse_for_gstreamer_args(argtable);
    int    gst_argc = 0;
    while (gst_argv[gst_argc])
    gst_argc++;

    gst_init(&gst_argc, &gst_argv);

    client_state = calloc(1, sizeof(*client_state));
    client_state->ice_candidates = g_ptr_array_new();

    struct lws_context               *ws_context = NULL;
    struct lws_context_creation_info  ctx_info;
    struct lws_client_connect_info    cc_info;

    if (initialize_values_of_libwebsocket_state(
            argtable,
            &ctx_info,
            &cc_info,
            &ws_context,
            ws_protocols,
            NULL) < 0)
    {
        fprintf(stderr, "Error: failed to init libwebsockets\n");
        return 1;
    }

    ws_client = lws_client_connect_via_info(&cc_info);
    if (!ws_client) {
        fprintf(stderr, "Error: WebSocket connect failed\n");
        return 1;
    }

    g_timeout_add(100, lws_service_callback, ws_context);
   
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    
    g_print("[Main] Starting main loop...\n");

    g_main_loop_run(loop);

    gst_element_set_state(pipeline, GST_STATE_NULL);
    if (webrtcbin) gst_object_unref(webrtcbin);
    if (pipeline)   gst_object_unref(pipeline);
    lws_context_destroy(ws_context);
    g_main_loop_unref(loop);

    free(client_state->sdp_offer);
    free(client_state->sdp_answer);
    for (guint i = 0; i < client_state->ice_candidates->len; i++)
        free(g_ptr_array_index(client_state->ice_candidates, i));
    g_ptr_array_free(client_state->ice_candidates, TRUE);
    free(client_state);

    arg_freetable(argtable, sizeof(argtable)/sizeof(void*));
    return 0;
}
