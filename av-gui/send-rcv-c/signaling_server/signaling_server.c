#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <glib.h>
#include <gst/gst.h>
#include <libwebsockets.h>

#include "signaling_server.h"    
#include "message_handling.h"
#include "signaling_context.h"
#include "message_protocol.h"

/* Global variables for graceful shutdown and global state */
volatile int interrupted = 0;
struct signaling_context *global_signaling_context = NULL;
struct signaling_state   *global_signaling_state = NULL;
int connected_clients = 0;

/* Signal handler for SIGINT */
void sigint_handler(int sig)
{
    interrupted = 1;
}

/*
 * The libwebsockets protocol callback.
 *
 * This callback handles connection events, incoming messages, and writeable events.
 */
int callback_signaling(struct lws *wsi,
                              enum lws_callback_reasons reason,
                              void *user,
                              void *in,
                              size_t len)
{
    struct per_session_data *psd = (struct per_session_data *)user;

    switch (reason) {
    case LWS_CALLBACK_ESTABLISHED:
        lwsl_user("[Signaling] Connection established: %p\n", wsi);
        add_client(wsi, global_signaling_context);
        connected_clients++;

        // Immediately tell the client its role
        {
          const char *role_str = (connected_clients == 1) ? "sender" : "receiver";
          char *role_msg = create_json_message("role", role_str);
          if (role_msg) {
            send_data_to_client(wsi, role_msg);
            free(role_msg);
            lwsl_user("[Signaling] Assigned role '%s' to %p\n", role_str, wsi);
          }
        }

        psd->len = 0;
        psd->seen_offer_version = 0;
        psd->seen_answer_version = 0;
        psd->seen_ice_candidate_count = 0;
        /* Immediately ask for writeable callback so that state is replayed */
        lws_callback_on_writable(wsi);
        break;

    case LWS_CALLBACK_RECEIVE:

        if (len < sizeof(psd->message)) {
            memcpy(psd->message, in, len);
            psd->message[len] = '\0';
            lwsl_user("[Signaling] Received message: %s\n", psd->message);

            /* Process the JSON message (dispatching to SDP/ICE handlers) */
            process_incoming_message(psd, wsi, global_signaling_state, global_signaling_context);
        } else {
            lwsl_err("[Signaling] Received message too long (%zu bytes)\n", len);
        }
        break;

    case LWS_CALLBACK_SERVER_WRITEABLE:
        /*
         * When a connection becomes writeable, check if there is a new SDP offer/answer
         * or any ICE candidates that the client has not seen yet.
         */
        if (global_signaling_state) {
            if (global_signaling_state->sdp_offer &&
                psd->seen_offer_version < global_signaling_state->offer_version &&
                wsi != global_signaling_state->offer_owner_wsi)
            {
                char *json = create_json_message("sdp_offer", global_signaling_state->sdp_offer);
                if (json) {
                    if (send_data_to_client(wsi, json) == 0)
                        psd->seen_offer_version = global_signaling_state->offer_version;
                    free(json);
                }
            }
            if (global_signaling_state->sdp_answer &&
                psd->seen_answer_version < global_signaling_state->answer_version &&
                wsi != global_signaling_state->answer_owner_wsi)
            {
                char *json = create_json_message("sdp_answer", global_signaling_state->sdp_answer);
                if (json) {
                    if (send_data_to_client(wsi, json) == 0)
                        psd->seen_answer_version = global_signaling_state->answer_version;
                    free(json);
                }
            }
            
            if (global_signaling_state->ice_candidates) {
                while (psd->seen_ice_candidate_count < global_signaling_state->ice_candidates->len) {
                    char *candidate = g_ptr_array_index(global_signaling_state->ice_candidates,
                                                        psd->seen_ice_candidate_count);
                    char *json = create_json_message("ice_candidate", candidate);
                    if (json) {
                        if (send_data_to_client(wsi, json) == 0) {
                            psd->seen_ice_candidate_count++;
                        } else {
                            free(json);
                            break;  // Try sending again later if this candidate couldn’t be sent.
                        }
                        free(json);
                    } else {
                        lwsl_err("[Signaling] Failed to create JSON for ICE candidate replay.\n");
                        break;
                    }
                }
            }
        }
        break;

    case LWS_CALLBACK_CLOSED:
        lwsl_user("[Signaling] Connection closed: %p\n", wsi);
        remove_client(wsi, global_signaling_context);
        connected_clients--;
        break;

    default:
        break;
    }
    return 0;
}

/*
 * Main entry point.
 *
 * Initializes GStreamer and libwebsockets, creates the global signaling context and state,
 * and then enters the libwebsockets service loop.
 */
int main(int argc, char **argv)
{

    /* --- Parse command line: [port] --- */
    int port = 9000;
    if (argc >= 2) {
        port = atoi(argv[1]);
        if (port <= 0) {
            fprintf(stderr, "Invalid port: %s\n", argv[1]);
            return 1;
        }
    }

    /* Initialize GStreamer (clients use GStreamer for media; the server simply depends on it) */
    gst_init(&argc, &argv);

    /* Set the libwebsockets log level */
    lws_set_log_level(LLL_USER | LLL_ERR | LLL_WARN, NULL);

    /* Initialize the global signaling context */
    global_signaling_context = create_signaling_context();
    if (!global_signaling_context) {
        lwsl_err("[Signaling] Failed to create signaling context\n");
        return -1;
    }

    /* Allocate and initialize the global signaling state */
    global_signaling_state = calloc(1, sizeof(struct signaling_state));
    if (!global_signaling_state) {
        lwsl_err("[Signaling] Failed to allocate signaling state\n");
        destroy_signaling_context(global_signaling_context);
        return -1;
    }
    global_signaling_state->ice_candidates = g_ptr_array_new();
    global_signaling_state->global_version_counter = 0;
    global_signaling_state->sdp_offer = NULL;
    global_signaling_state->sdp_answer = NULL;
    global_signaling_state->offer_owner_wsi = NULL;
    global_signaling_state->answer_owner_wsi = NULL;
    global_signaling_state->offer_version = 0;
    global_signaling_state->answer_version = 0;

    /* Set up SIGINT handler for graceful shutdown */
    signal(SIGINT, sigint_handler);

    /*
     * Define the libwebsockets protocols.
     *
     * We define one protocol ("webrtc-protocol") that uses our callback.
     */
    static const struct lws_protocols protocols[] = {
        {
            .name = "webrtc-protocol",
            .callback = callback_signaling,
            .per_session_data_size = sizeof(struct per_session_data),
            .rx_buffer_size = 4096,
        },
        { NULL, NULL, 0, 0 } /* terminator */
    };

    /* Prepare the context creation info */
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = port;
    info.protocols = protocols;
    info.gid = -1;
    info.uid = -1;

    /* Create the libwebsockets context */
    struct lws_context *context = lws_create_context(&info);
    if (!context) {
        lwsl_err("[Signaling] lws_create_context failed\n");
        destroy_signaling_context(global_signaling_context);
        free(global_signaling_state);
        return -1;
    }

    lwsl_user("Server listening on port: %d\n", port);

    /* Main event loop */
    while (!interrupted)
        lws_service(context, 1000);

    lwsl_user("[Signaling] Server shutting down...\n");

    /* Cleanup */
    lws_context_destroy(context);
    destroy_signaling_context(global_signaling_context);

    if (global_signaling_state->sdp_offer)
        free(global_signaling_state->sdp_offer);
    if (global_signaling_state->sdp_answer)
        free(global_signaling_state->sdp_answer);
    if (global_signaling_state->ice_candidates)
        g_ptr_array_free(global_signaling_state->ice_candidates, TRUE);
    free(global_signaling_state);

    return 0;
}