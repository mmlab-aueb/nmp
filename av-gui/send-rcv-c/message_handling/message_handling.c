#include <cJSON.h>
#include <message_protocol.h>
#include <glib.h>
#include <gst/gst.h> 
#include <signaling_context.h>
#include "message_handling.h"


/*------------------------------------------------------------------
  send_data_to_client
  -------------------
  Sends a JSON message over a WebSocket connection.
  
  Parameters:
    - wsi: the client's WebSocket instance.
    - json_message: the JSON message string to be sent.
  
  Returns:
    - 0 on success, or -1 on error.
-------------------------------------------------------------------*/
int send_data_to_client(struct lws* wsi, const char *json_message) {
    if (!json_message)
        return -1;

    size_t msg_len = strlen(json_message);
    // Allocate a buffer with LWS_PRE bytes reserved.
    unsigned char buffer[LWS_PRE + msg_len + 1];
    memset(buffer, 0, sizeof(buffer));
    memcpy(&buffer[LWS_PRE], json_message, msg_len);

    ssize_t n = lws_write(wsi, &buffer[LWS_PRE], msg_len, LWS_WRITE_TEXT);
    if (n < 0 || (size_t)n != msg_len) {
        lwsl_err("[Signaling] lws_write failed: wrote %zd of %zu bytes\n", n, msg_len);
        return -1;
    }
    return 0;
}

/*------------------------------------------------------------------
  broadcast_ice_candidate
  -----------------------
  Broadcasts an ICE candidate to all connected clients except the sender.
  It creates a JSON message using create_json_message.
  
  Parameters:
    - sender_wsi: the WebSocket instance of the sender (this client will be skipped).
    - candidate: the ICE candidate string to broadcast.
    - ctx: pointer to the global signaling context (holds the clients list and mutex).
  
  Returns:
    - 0 on success, or -1 if an error occurred.
-------------------------------------------------------------------*/
int broadcast_ice_candidate(
    struct lws *sender_wsi, 
    const char *candidate, 
    struct signaling_context *ctx
) {
    // Check that the context and the clients array are valid.
    if (!ctx || !ctx->clients) {
        lwsl_err("[Signaling] Invalid signaling context or clients list.\n");
        return -1;
    }

    char *json_message = create_json_message("ice_candidate", candidate);
    if (!json_message) {
        lwsl_err("[Signaling] Failed to create ICE candidate JSON message.\n");
        return -1;
    }

    int overall_status = 0;
    // Lock the embedded mutex by passing its address.
    g_mutex_lock(&ctx->clients_mutex);
    for (guint i = 0; i < ctx->clients->len; i++) {
        struct lws *client_wsi = g_ptr_array_index(ctx->clients, i);
        // Do not send the message back to the sender.
        if (client_wsi == sender_wsi)
            continue;

        struct per_session_data *psd = (struct per_session_data *)lws_wsi_user(client_wsi);
        if (!psd) {
            lwsl_err("[Signaling] No per-session data for client %p\n", (void*)client_wsi);
            overall_status = -1;
            continue;
        }

        if (send_data_to_client(client_wsi, json_message) < 0) {
            lwsl_err("[Signaling] Failed to send ICE candidate to client %p\n", (void*)client_wsi);
            overall_status = -1;
        }
    }
    // Unlock the mutex by passing its address.
    g_mutex_unlock(&ctx->clients_mutex);
    free(json_message);
    return overall_status;
}

/*------------------------------------------------------------------
  handle_sdp_offer_from_string
  ----------------------------
  Processes an incoming SDP offer. If the new offer differs from the one
  stored in the state, it updates the state and triggers a callback for
  broadcasting the updated offer to all clients.
  
  Parameters:
    - offer_str: the SDP offer string.
    - wsi: the WebSocket instance of the sender.
    - state: pointer to the signaling state.
-------------------------------------------------------------------*/
static void handle_sdp_offer_from_string(const char *offer_str, struct lws *wsi, struct signaling_state *state) {
    lwsl_user("[Signaling] Processing SDP Offer:\n%s\n", offer_str);
    if (!state->sdp_offer || strcmp(state->sdp_offer, offer_str) != 0) {
        free(state->sdp_offer);
        state->sdp_offer = strdup(offer_str);
        state->offer_owner_wsi = wsi;
        state->offer_version = ++state->global_version_counter;
        // Notify all clients that they can now write (e.g. to send the updated offer).
        lws_callback_on_writable_all_protocol(lws_get_context(wsi), lws_get_protocol(wsi));
    } else {
        lwsl_user("[Signaling] Received identical SDP Offer, ignoring.\n");
    }
}

/*------------------------------------------------------------------
  handle_sdp_answer_from_string
  -----------------------------
  Processes an incoming SDP answer. If the answer differs from the current one,
  updates the state and notifies all clients.
  
  Parameters:
    - answer_str: the SDP answer string.
    - wsi: the WebSocket instance of the sender.
    - state: pointer to the signaling state.
-------------------------------------------------------------------*/
static void handle_sdp_answer_from_string(const char *answer_str, struct lws *wsi, struct signaling_state *state) {
    lwsl_user("[Signaling] Processing SDP Answer:\n%s\n", answer_str);
    if (!state->sdp_answer || strcmp(state->sdp_answer, answer_str) != 0) {
        free(state->sdp_answer);
        state->sdp_answer = strdup(answer_str);
        state->answer_owner_wsi = wsi;
        state->answer_version = ++state->global_version_counter;
        lws_callback_on_writable_all_protocol(lws_get_context(wsi), lws_get_protocol(wsi));
    } else {
        lwsl_user("[Signaling] Received identical SDP Answer, ignoring.\n");
    }
}

/*------------------------------------------------------------------
  handle_multiple_ice_candidates_from_string
  -------------------------------------------
  Processes a string that may contain multiple ICE candidate lines.
  Each candidate (assumed to start with "candidate:") is extracted and
  then broadcast individually.
  
  Parameters:
    - multi_line: a mutable string containing one or more ICE candidate lines.
    - wsi: the WebSocket instance of the sender.
    - state: pointer to the signaling state.
    - ctx: the context of the server
-------------------------------------------------------------------*/
static void handle_multiple_ice_candidates_from_string(char* multi_line, struct lws* wsi, struct signaling_state* state, struct signaling_context *ctx) {
    char *start = multi_line;
    while (start && *start) {
        char *candidate_line = strstr(start, "candidate:");
        if (!candidate_line)
            break;
        // Find the end of this candidate (up to the newline).
        char *line_end = strchr(candidate_line, '\n');
        if (line_end)
            *line_end = '\0';  // Terminate the candidate string

        lwsl_user("[Signaling] Received ICE candidate:\n%s\n", candidate_line);
        broadcast_ice_candidate(wsi, candidate_line, ctx);

        if (line_end)
            start = line_end + 1;
        else
            break;
    }
}

/*------------------------------------------------------------------
  handle_ice_candidate_from_string
  --------------------------------
  Processes an incoming ICE candidate message.
  If the string contains multiple candidate lines (separated by newlines),
  it delegates to handle_multiple_ice_candidates_from_string.
  Otherwise, it broadcasts the candidate.
  
  Parameters:
    - candidate_str: the ICE candidate string.
    - wsi: the WebSocket instance of the sender.
    - state: pointer to the signaling state.
    - ctx: the context of the server
-------------------------------------------------------------------*/
static void handle_ice_candidate_from_string(const char *candidate_str, struct lws *wsi, struct signaling_state *state, struct signaling_context *ctx) {
    lwsl_user("[Signaling] Processing ICE Candidate:\n%s\n", candidate_str);
    // If the candidate string contains multiple lines, process them separately.
    if (strstr(candidate_str, "candidate:") && strchr(candidate_str, '\n')) {
        char *multi_line = strdup(candidate_str);
        if (multi_line) {
            handle_multiple_ice_candidates_from_string(multi_line, wsi, state, ctx);
            free(multi_line);
        }
    } else {
        // Single ICE candidate message.
        broadcast_ice_candidate(wsi, candidate_str, ctx);
    }
}

/*------------------------------------------------------------------
  process_incoming_message
  ------------------------
  Main entry point to process an incoming message from a client.
  It parses the JSON message and dispatches it to the appropriate handler
  based on its type (SDP offer, SDP answer, or ICE candidate). This
  function now accepts a signaling context that holds global data (like
  the clients list and mutex) which can be used by both sender and receiver
  handlers.
  
  Parameters:
    - per_session_data: per-session data for the client (contains the raw message).
    - wsi: the client's WebSocket instance.
    - state: pointer to the per-connection signaling state.
    - ctx: pointer to the global signaling context containing global resources,
           such as the clients list and its mutex.
-------------------------------------------------------------------*/
void process_incoming_message(struct per_session_data* per_session_data,
                              struct lws *wsi,
                              struct signaling_state *state,
                              struct signaling_context *ctx)
{
    char *data = NULL;
    const char *message = per_session_data->message;
    
    // Parse the incoming JSON message into a MessageType and data string.
    MessageType message_type = parse_json_message(message, &data);

    // Dispatch to the appropriate handler based on the message type.
    switch (message_type) {
        case MSG_TYPE_SDP_OFFER:
            // Process an SDP offer. The handler is responsible for updating
            // state and, if needed, notifying other clients.
            handle_sdp_offer_from_string(data, wsi, state);
            break;
        case MSG_TYPE_SDP_ANSWER:
            // Process an SDP answer.
            handle_sdp_answer_from_string(data, wsi, state);
            break;
        case MSG_TYPE_ICE_CANDIDATE:
            // Process ICE candidate(s). This may broadcast the candidate(s)
            // to other clients via the provided signaling context.
            handle_ice_candidate_from_string(data, wsi, state, ctx);
            break;
        default:
            lwsl_err("[Signaling] Unknown message type. Data: %s\n", data ? data : "NULL");
            break;
    }
    
    // Free the allocated data string.
    free(data);
}
