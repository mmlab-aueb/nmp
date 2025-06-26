// signaling_context.c
#include "signaling_context.h"
#include <stdlib.h>

struct signaling_context* create_signaling_context(void) {
    struct signaling_context *ctx = malloc(sizeof(struct signaling_context));
    if (!ctx)
        return NULL;

    ctx->clients = g_ptr_array_new_with_free_func(NULL);
    g_mutex_init(&ctx->clients_mutex); // Initialize the embedded mutex.
    return ctx;
}


/* 
 * Add a client (WebSocket instance) to the global signaling context.
 */
void add_client(struct lws *wsi, struct signaling_context *ctx) {
    g_mutex_lock(&ctx->clients_mutex);
    g_ptr_array_add(ctx->clients, wsi);
    g_mutex_unlock(&ctx->clients_mutex);
};

/* 
 * Remove a client from the global signaling context.
 */
void remove_client(struct lws *wsi, struct signaling_context *ctx) {
    g_mutex_lock(&ctx->clients_mutex);
    for (guint i = 0; i < ctx->clients->len; i++) {
        if (g_ptr_array_index(ctx->clients, i) == wsi) {
            g_ptr_array_remove_index(ctx->clients, i);
            break;
        }
    }
    g_mutex_unlock(&ctx->clients_mutex);
};

void destroy_signaling_context(struct signaling_context *ctx) {
    if (ctx) {
        if (ctx->clients)
            g_ptr_array_free(ctx->clients, TRUE);
        g_mutex_clear(&ctx->clients_mutex); // Clear the embedded mutex.
        free(ctx);
    }
}