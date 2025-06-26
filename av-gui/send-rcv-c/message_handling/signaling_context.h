// signaling_context.h
#ifndef SIGNALING_CONTEXT_H
#define SIGNALING_CONTEXT_H

#include <glib.h>
#include <libwebsockets.h>
#include <glib/gprintf.h>

/* Global context for signaling, storing the clients list and a mutex */
struct signaling_context {
    GPtrArray *clients;     // Array of (struct lws *) for all clients.
    GMutex      clients_mutex;  // Mutex to protect the clients array.
};

/* Allocate and initialize a new signaling context */
struct signaling_context* create_signaling_context(void);

/* Free the signaling context */
void destroy_signaling_context(struct signaling_context *ctx);

void add_client(struct lws *wsi, struct signaling_context *ctx);

void remove_client(struct lws *wsi, struct signaling_context *ctx);

#endif // SIGNALING_CONTEXT_H
