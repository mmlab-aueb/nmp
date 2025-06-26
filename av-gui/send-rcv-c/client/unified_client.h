#ifndef UNIFIED_CLIENT_H
#define UNIFIED_CLIENT_H

#include <gst/gst.h>
#include <gst/webrtc/webrtc.h>
#include <glib.h>
#include <libwebsockets.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "message_protocol.h"
#include "message_handling.h"    
#include "client_utils.h"    

/* ─── Pipeline definitions ─────────────────────────────────────────────────── */
#define PIPELINES_SENDER   "../client/pipelines1.txt"
#define PIPELINES_RECEIVER "../client/pipelines2.txt"

/* ─── Shared globals, defined in unified_client.c ───────────────────────── */
extern GstElement             *pipeline;
extern GstElement             *webrtcbin;
extern struct lws_context     *ws_context;
extern struct lws             *ws_client;
extern struct signaling_state *client_state;
extern bool                    is_sender;
extern bool                    role_received;

/* ─── Forward declarations ─────────────────────────────────────────────────── */
struct signaling_state;

/* ─── Application resources ───────────────────────────────────────────────── */
typedef struct {
    GstElement             *pipeline;
    GstElement             *webrtcbin;
    struct lws_context     *ws_context;
    struct lws             *ws_client;
    GMainLoop              *loop;
    struct signaling_state *client_state;
    void                  **argtable;
    size_t                  argtable_count;
} AppResources;

/* ─── Resource cleanup ───────────────────────────────────────────────────── */
void free_app_resources(AppResources *res);

/* ─── State‑update helpers ───────────────────────────────────────────────── */
void state_update_offer(bool local, const char *sdp);
void state_update_answer(bool local, const char *sdp);
void state_add_ice(const char *candidate);

/* ─── WebRTC callbacks ───────────────────────────────────────────────────── */
void on_offer_created(GstPromise *promise, gpointer user_data);
void on_negotiation_needed(GstElement *webrtc, gpointer user_data);
void on_ice_candidate(GstElement *webrtc, guint mline, gchar *candidate, gpointer user_data);
void on_answer_created(GstPromise *promise, gpointer user_data);

/* ─── Pipeline setup ──────────────────────────────────────────────────────── */
void setup_pipeline_for_role(void);

/* ─── Signaling callbacks ────────────────────────────────────────────────── */
int  ws_client_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len);

gboolean lws_service_callback(gpointer user_data);

/* ─── Entry point ─────────────────────────────────────────────────────────── */
int main(int argc, char **argv);

#endif
