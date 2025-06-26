#ifndef SIGNALING_SERVER_H
#define SIGNALING_SERVER_H

#include <libwebsockets.h>
#include <gst/gst.h>
#include <glib.h>

#include "message_handling.h"
#include "signaling_context.h"
#include "message_protocol.h"

/* ─── Shared Globals ──────────────────────────────────────────────────────── */
extern volatile int              interrupted;
extern struct signaling_context *global_signaling_context;
extern struct signaling_state   *global_signaling_state;
extern int                       connected_clients;

/* ─── Prototypes ─────────────────────────────────────────────────────────── */
/// SIGINT handler to break the service loop
void sigint_handler(int sig);

/// libwebsockets protocol callback
int callback_signaling(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len);

/**
 * Starts a WebSockets-based signaling server on the given port.
 *
 * @param port The TCP port to listen on.
 * @return 0 on clean shutdown, non-zero on failure.
 */
int run_signaling_server(int port);

#endif