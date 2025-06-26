#ifndef MESSAGE_HANDLING_H
#define MESSAGE_HANDLING_H

#include <gst/gst.h>
#include <libwebsockets.h>
#include "signaling_context.h"

/*
 * A structure that holds all the SDP-related state.
 * (This replaces the previous global static buffers and counters.)
 */
struct signaling_state {
    char *sdp_offer;
    char *sdp_answer;
    GPtrArray *ice_candidates; // Using GLib's pointer array for multiple candidates
    struct lws *offer_owner_wsi;
    struct lws *answer_owner_wsi;
    unsigned long offer_version;
    unsigned long answer_version;
    unsigned long global_version_counter;
};

// Per-connection data
struct per_session_data {
    char message[4096];
    size_t len;

    unsigned long seen_offer_version;
    unsigned long seen_answer_version;
    unsigned long seen_ice_candidate_count;
};

int send_data_to_client(struct lws* wsi, const char *json_message);
int broadcast_ice_candidate(struct lws *sender_wsi, const char *candidate, struct signaling_context *ctx);
void process_incoming_message(struct per_session_data* per_session_data, struct lws *wsi, struct signaling_state *state, struct signaling_context *ctx);

#endif // MESSAGE_HANDLING_H