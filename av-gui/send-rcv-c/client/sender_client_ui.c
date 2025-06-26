//gcc sender_client_pipeline_callb_cnct_srvr_clb_ui_dnmc_pl_lowdelay.c -o sender_client_pipeline_callb_cnct_srvr_clb_ui_dnmc_pl_lowdelay $(pkg-config --cflags --libs gtk+-3.0 gstreamer-1.0 gstreamer-webrtc-1.0 gstreamer-sdp-1.0) -lwebsockets -lpthread -DGST_USE_UNSTABLE_API
#include <libwebsockets.h>
#include <gst/gst.h>
#include <gst/webrtc/webrtc.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
/*--------------------*/
#include <string.h>
#include <gtk/gtk.h>
#include <gst/gst.h>
#include <gdk/gdk.h>
#include <pthread.h>

typedef struct _CustomData {
    GstElement *pipeline;
    GstElement *videosrc, *tee;
    GstElement *queue1, *queue2;
    GstElement *videoconvert1, *videoconvert2,  *videoconvert3;
    GstElement *gtksink1, *gtksink2, *autovideosink2, *glimagesink2, *gtkglsink1, *gtkglsink2;
    GstElement *vp8enc,*rtpvp8pay, *webrtcbin;
    GstElement *decodebin, *vp8dec, *parsebin, *rtpvp8depay;
    GtkWidget *sink_widget1, *sink_widget2;
    GMainLoop *main_loop;
} CustomData;

CustomData data;


static GstElement *webrtc = NULL;

// Arrays to store ICE candidates
static GPtrArray *local_candidates = NULL;
static GPtrArray *remote_candidates = NULL;


struct lws *wsi;
struct lws_context *context;
static GMainLoop *main_loop = NULL;


/* Play callback */
static void play_cb(GtkButton *button, CustomData *data) {
    g_print("Play pressed\n");
    gst_element_set_state(data->pipeline, GST_STATE_PLAYING);
}

/* Pause callback */
static void pause_cb(GtkButton *button, CustomData *data) {
    g_print("Pause pressed\n");
    gst_element_set_state(data->pipeline, GST_STATE_PAUSED);
}

/* Stop callback */
static void stop_cb(GtkButton *button, CustomData *data) {
    g_print("Stop pressed\n");
    gst_element_set_state(data->pipeline, GST_STATE_READY);
}

/* Window close callback */
static void delete_event_cb(GtkWidget *widget, GdkEvent *event, CustomData *data) {
    stop_cb(NULL, data);
    gtk_main_quit();
}

/* Create GTK UI */
static void create_ui(CustomData *data) {
    GtkWidget *main_window, *main_box, *video_box, *controls;
    GtkWidget *play_button, *pause_button, *stop_button;

    main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    g_signal_connect(G_OBJECT(main_window), "delete-event", G_CALLBACK(delete_event_cb), data);

    /* Create buttons */
    play_button = gtk_button_new_with_label("Play");
    g_signal_connect(G_OBJECT(play_button), "clicked", G_CALLBACK(play_cb), data);

    pause_button = gtk_button_new_with_label("Pause");
    g_signal_connect(G_OBJECT(pause_button), "clicked", G_CALLBACK(pause_cb), data);

    stop_button = gtk_button_new_with_label("Stop");
    g_signal_connect(G_OBJECT(stop_button), "clicked", G_CALLBACK(stop_cb), data);

    /* Create control box */
    controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(controls), play_button, FALSE, FALSE, 5);
    //gtk_box_pack_start(GTK_BOX(controls), pause_button, FALSE, FALSE, 5);
    //gtk_box_pack_start(GTK_BOX(controls), stop_button, FALSE, FALSE, 5);

    /* Get GTK widgets from gtksink*/ 
    g_object_get(data->gtksink1, "widget", &data->sink_widget1, NULL);
    g_object_get(data->gtksink2, "widget", &data->sink_widget2, NULL);

    if (!GTK_IS_WIDGET(data->sink_widget1) ) {  //|| !GTK_IS_WIDGET(data->sink_widget2)
        g_printerr("Error: gtksink1 did not return a valid GTK widget\n");
        return;
    }

    if (!GTK_IS_WIDGET(data->sink_widget2) ) {  //|| !GTK_IS_WIDGET(data->sink_widget2)
        g_printerr("Error: gtksink2 did not return a valid GTK widget\n");
        return;
    }
    /* Create video box */
    video_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(video_box), data->sink_widget1, TRUE, TRUE, 5);
    gtk_box_pack_start(GTK_BOX(video_box), data->sink_widget2, TRUE, TRUE, 5);

    /* Create main box */
    main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_box_pack_start(GTK_BOX(main_box), video_box, TRUE, TRUE, 5);
    gtk_box_pack_start(GTK_BOX(main_box), controls, FALSE, FALSE, 5);

    gtk_container_add(GTK_CONTAINER(main_window), main_box);
    gtk_window_set_default_size(GTK_WINDOW(main_window), 1200, 400);

    gtk_widget_show_all(main_window);
}






// Define the structure to hold WebSocket message data
struct client_session_data {
    char message[2048];
    size_t len;
};

static int counter=0;

// Forward declarations
static void on_offer_created(GstPromise *promise, gpointer user_data);
static void on_answer_created(GstPromise *promise, gpointer user_data);

static void store_local_candidate(const char *candidate) {
    g_ptr_array_add(local_candidates, g_strdup(candidate));
}
static void store_remote_candidate(const char *candidate) {
    g_ptr_array_add(remote_candidates, g_strdup(candidate));
}
static void on_ice_candidate(GstElement *webrtcbin, guint mlineindex,
                             gchar *candidate, gpointer user_data);



// Callback when a new pad (stream) is added to webrtcbin
static void on_pad_added(GstElement *rtpvp8depay, GstPad *pad, gpointer user_data) {
     CustomData *data = (CustomData *)user_data;
    g_print("[Sender] New pad from parsebin!\n");
   
    //GstPad *sinkpad = gst_element_get_static_pad(data->autovideosink2, "sink");

    //GstPad *sinkpad = gst_element_get_static_pad(data->gtkglsink2, "sink");
    GstPad *sinkpad = gst_element_get_static_pad(data->gtksink2, "sink");

    if (!sinkpad) {
        g_printerr("[ERROR] Could not get sink pad from vp8dec!\n");
        return;
    }

    if (gst_pad_link(pad, sinkpad) != GST_PAD_LINK_OK) {
        g_printerr("[ERROR] Failed to link WebRTC incoming pad to decodebin!\n");
    } else {
        g_print("[Sender] Successfully linked WebRTC incoming pad to vp8dec.\n");
    }

    gst_object_unref(sinkpad);

   
}


// Callback when incomig stream is detected
static void on_incoming_stream(GstElement *webrtc, GstPad *pad, gpointer user_data) {

   g_print("[Sender] Incoming stream detected!\n");
    CustomData *data = (CustomData *)user_data;
    //GstElement *pipeline = (GstElement *)user_data;
    //GstElement *decodebin, *videosink;

    if (!data->pipeline) {
        g_printerr("[ERROR] Pipeline is NULL!\n");
        return;
    }

    // Ensure it's a media pad
    if (!gst_pad_has_current_caps(pad)) {
        g_print("[Sender] Pad has no caps, waiting for negotiation...\n");
        return;
    }

    // Create decodebin
    data->decodebin = gst_element_factory_make("decodebin", NULL);
    if (!data->decodebin) {
        g_printerr("[ERROR] Failed to create decodebin!\n");
        return;
    }

    // Create a new video sink
    data->autovideosink2 = gst_element_factory_make("autovideosink", NULL);
    if (!data->autovideosink2) {
        g_printerr("[ERROR] Failed to create autovideosink!\n");
        gst_object_unref(data->decodebin);
        return;
    }
    // Add decodebin and videosink to pipeline
    //gst_bin_add_many(GST_BIN(data->pipeline), data->decodebin , data->autovideosink2, NULL);
    gst_bin_add_many(GST_BIN(data->pipeline), data->rtpvp8depay, data->vp8dec,data->videoconvert3 ,data->gtksink2, NULL);
    if (!gst_element_link_many(data->rtpvp8depay, data->vp8dec, data->videoconvert3, data->gtksink2, NULL)) {
        g_printerr("[ERROR] Failed to link rtpvp8depay → vp8dec → videoconvert → gtksink2!\n");
        return;
    }


    gst_element_sync_state_with_parent(data->vp8dec);
    gst_element_sync_state_with_parent(data->rtpvp8depay);
    //gst_element_sync_state_with_parent(data->autovideosink2);
    gst_element_sync_state_with_parent(data->videoconvert3);
    gst_element_sync_state_with_parent(data->gtksink2);

    // Connect decodebin to handle the decoded stream
    g_print("[Sender] Incoming stream detected!\n");

    GstPad *sinkpad = gst_element_get_static_pad(data->rtpvp8depay, "sink");
    if (!sinkpad) {
        g_printerr("[ERROR] Decodebin has no sink pad!\n");
    } else {
        if (gst_pad_link(pad, sinkpad) != GST_PAD_LINK_OK) {
            g_printerr("[ERROR] Could not link incoming pad to decodebin sink!\n");
        } else {
            g_print("[Sender] Linked WebRTC incoming pad to decodebin.\n");
        }
            gst_object_unref(sinkpad);
    }
    g_signal_connect(data->rtpvp8depay, "pad-added", G_CALLBACK(on_pad_added), data);
    gst_element_set_state(data->rtpvp8depay, GST_STATE_PLAYING);

    gst_element_set_state(data->vp8dec, GST_STATE_PLAYING);
    gst_element_set_state(data->videoconvert3, GST_STATE_PLAYING);

    //gst_element_set_state(data->autovideosink2, GST_STATE_PLAYING);
    gst_element_set_state(data->gtksink2, GST_STATE_PLAYING);
    //gst_element_set_state(data->gtkglsink2, GST_STATE_PLAYING);

    g_print("[Sender] Incoming stream detected!\n");
    

}







/* ICE candidate from the local (sender) side */
static void on_ice_candidate(GstElement *webrtcbin, guint mlineindex,
                             gchar *candidate, gpointer user_data) {

    //struct lws *wsi = (struct lws *)user_data;
    wsi = (struct lws *)user_data;
    if (!wsi) {
        g_printerr("[ERROR] WebSocket is NULL. Cannot send ICE candidate!\n");
        return;
    } else {
        g_print("[INFO] WebSocket is valid. Sending ICE candidate.\n");
    }

    if (!candidate || !candidate[0]) {
        return;
    }

    // Print local ICE candidate in green
    printf("\033[1;32m[Sender] Produced Local ICE Candidate:\033[0m %s\n", candidate);

    store_local_candidate(candidate);

    gchar *msg = g_strdup_printf("candidate:%s", candidate);
    unsigned char *buf = malloc(LWS_PRE + strlen(msg) + 1);
    memset(buf, 0, LWS_PRE + strlen(msg) + 1);
    memcpy(&buf[LWS_PRE], msg, strlen(msg));

    if (lws_write(wsi, &buf[LWS_PRE], strlen(msg), LWS_WRITE_TEXT) < 0) {
        lwsl_err("Sender: Failed to send ICE candidate\n");
    }
    free(buf);
    g_free(msg);
}

static void handle_remote_candidate(const char *candidate_sdp) {
    if (!candidate_sdp || !candidate_sdp[0]) {
        return;
    }

    // Print in blue
    lwsl_user("\033[1;34m[Sender] Adding Remote ICE Candidate:\033[0m %s\n", candidate_sdp);

    store_remote_candidate(candidate_sdp);
    g_signal_emit_by_name(webrtc, "add-ice-candidate", 0, candidate_sdp);
}

/* create SDP Offer */
static void on_negotiation_needed(GstElement *webrtcbin, gpointer wsi) {
    lwsl_user("\nSender: on_negotiation_needed\n");
    GstPromise *promise = gst_promise_new_with_change_func(on_offer_created, wsi, NULL);
    g_signal_emit_by_name(webrtcbin, "create-offer", NULL, promise);
}

/* Called after create-offer finishes */
static void on_offer_created(GstPromise *promise, gpointer user_data) {
    struct lws *wsi = (struct lws *)user_data;
    gst_promise_wait(promise);

    const GstStructure *reply = gst_promise_get_reply(promise);
    GstWebRTCSessionDescription *offer = NULL;
    gst_structure_get(reply, "offer", GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &offer, NULL);
    if (!offer) {
        lwsl_err("Sender: Failed to create Offer\n");
        gst_promise_unref(promise);
        return;
    }

    gchar *sdp_text = gst_sdp_message_as_text(offer->sdp);
    lwsl_user("\n[Sender] Created SDP Offer:\n%s\n", sdp_text);

    g_signal_emit_by_name(webrtc, "set-local-description", offer, NULL);
    gst_webrtc_session_description_free(offer);
    gst_promise_unref(promise);

    unsigned char *buf = malloc(LWS_PRE + strlen(sdp_text) + 1);
    memset(buf, 0, LWS_PRE + strlen(sdp_text) + 1);
    memcpy(&buf[LWS_PRE], sdp_text, strlen(sdp_text));

    if (lws_write(wsi, &buf[LWS_PRE], strlen(sdp_text), LWS_WRITE_TEXT) < 0) {
        lwsl_err("Sender: Failed to send SDP Offer\n");
    } else {
        lwsl_user("Sender: Sent SDP Offer to server\n");
    }

    free(buf);
    g_free(sdp_text);
}

/* Called after we create an Answer in GStreamer */
static void on_answer_created(GstPromise *promise, gpointer user_data) {
    struct lws *wsi = (struct lws *)user_data;
    gst_promise_wait(promise);
    const GstStructure *reply = gst_promise_get_reply(promise);
    GstWebRTCSessionDescription *answer = NULL;
    gst_structure_get(reply, "answer", GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &answer, NULL);
    if (!answer) {
        lwsl_err("Sender: Failed to create Answer\n");
        gst_promise_unref(promise);
        return;
    }

    g_signal_emit_by_name(webrtc, "set-local-description", answer, NULL);

    gchar *sdp_text = gst_sdp_message_as_text(answer->sdp);
    gst_webrtc_session_description_free(answer);
    gst_promise_unref(promise);

    if (!sdp_text) {
        lwsl_err("Sender: Could not convert Answer to text\n");
        return;
    }

    lwsl_user("\n[Sender] Created SDP Answer:\n%s\n", sdp_text);

    unsigned char *buf = malloc(LWS_PRE + strlen(sdp_text) + 14);
    memset(buf, 0, LWS_PRE + strlen(sdp_text) + 14);
    snprintf((char*)&buf[LWS_PRE], strlen(sdp_text) + 14, "SERVER_ANSWER:%s", sdp_text);

    if (lws_write(wsi, &buf[LWS_PRE], strlen((char*)&buf[LWS_PRE]), LWS_WRITE_TEXT) < 0) {
        lwsl_err("Sender: Failed to send SDP Answer\n");
    } else {
        lwsl_user("Sender: Sent SDP Answer to server\n");
    }

    free(buf);
    g_free(sdp_text);

    // (Optional) Let's print local ICE so we see them after we set remote desc
    lwsl_user("\n[Sender after set-remote-description (Answer)] Current Local ICE candidates:\n");
    if (local_candidates) {
        for (guint i = 0; i < local_candidates->len; i++) {
            lwsl_user("  %s\n", (char *)g_ptr_array_index(local_candidates, i));
        }
    }
}



static void create_pipeline(CustomData *data){
    //struct lws *wsi = (struct lws *)user_data;
    /* Create a manual pipeline */
    data->pipeline = gst_pipeline_new("test-pipeline");
    data->videosrc = gst_element_factory_make("v4l2src", "videosrc"); //gia camera na ginei uncommented
    //data->videosrc = gst_element_factory_make("videotestsrc", "videosrc");
    data->tee = gst_element_factory_make("tee", "tee");
    data->queue1 = gst_element_factory_make("queue", "queue1");
    data->queue2 = gst_element_factory_make("queue", "queue2");
    data->videoconvert1 = gst_element_factory_make("videoconvert", "videoconvert1");
    data->videoconvert2 = gst_element_factory_make("videoconvert", "videoconvert2");
    data->videoconvert3 = gst_element_factory_make("videoconvert", "videoconvert3");
    data->gtksink1 = gst_element_factory_make("gtksink", "gtksink1");
    data->gtksink2 = gst_element_factory_make("gtksink", "gtksink2");
    data->gtkglsink2 = gst_element_factory_make("gtkglsink", "gtkglsink2");
    data->autovideosink2 = gst_element_factory_make("autovideosink", NULL);
    data->vp8dec = gst_element_factory_make("vp8dec", "vp8dec");

    data->vp8enc = gst_element_factory_make("vp8enc", "vp8enc");
    data->rtpvp8pay = gst_element_factory_make("rtpvp8pay", "rtpvp8pay");
    data->rtpvp8depay = gst_element_factory_make("rtpvp8depay", "rtpvp8depay");
    data->webrtcbin = gst_element_factory_make("webrtcbin", "sendrcv");

        /* Ensure elements were created */
    if (!data->pipeline || !data->videosrc || !data->tee || !data->queue1 || !data->queue2 ||
        !data->videoconvert1 || !data->videoconvert2 || !data->gtksink1 || !data->gtksink2 ||
        !data->vp8enc || !data->rtpvp8pay || !data->webrtcbin ) {
        g_printerr("Error: Failed to create GStreamer elements\n");
        return;
    }
    
    g_object_set(G_OBJECT(data->gtksink2), "sync", FALSE, NULL);

    g_object_set(G_OBJECT(data->queue1), "max-size-buffers", 1, NULL);
    g_object_set(G_OBJECT(data->queue2), "max-size-buffers", 1, NULL);

    g_object_set(G_OBJECT(data->vp8enc), "deadline", 1, NULL);  // Low-latency encoding
    g_object_set(G_OBJECT(data->webrtcbin), "latency", 0, NULL);
    g_object_set(G_OBJECT(data->webrtcbin), "bundle-policy", 2, NULL);
    g_object_set(G_OBJECT(data->rtpvp8pay), "config-interval", -1, NULL);

        /* Add elements to pipeline */
    gst_bin_add_many(GST_BIN(data->pipeline), data->videosrc, data->tee, data->queue1, data->queue2,
                     data->videoconvert1, data->videoconvert2, data->gtksink1, 
                     data->vp8enc, data->rtpvp8pay,data->webrtcbin, NULL);
        /* Request pads from tee */
    GstPad *tee_srcpad1 = gst_element_request_pad_simple(data->tee, "src_%u");
    GstPad *tee_srcpad2 = gst_element_request_pad_simple(data->tee, "src_%u");

    if (!tee_srcpad1 || !tee_srcpad2) {
        g_printerr("Error: Failed to request tee pads\n");
        return;
    }

        /* Link elements */
    gst_element_link_many(data->videosrc, data->tee, NULL);
    gst_element_link_pads(data->tee, GST_PAD_NAME(tee_srcpad1), data->queue1, "sink");
    gst_element_link_pads(data->tee, GST_PAD_NAME(tee_srcpad2), data->queue2, "sink");
    gst_element_link_many(data->queue1, data->videoconvert1, data->gtksink1, NULL);
    gst_element_link_many(data->queue2, data->videoconvert2, data->vp8enc, data->rtpvp8pay,data->webrtcbin, NULL);


    //gst_element_set_state(data->pipeline, GST_STATE_PLAYING);

    webrtc = gst_bin_get_by_name(GST_BIN(data->pipeline), "sendrcv");
    if (!webrtc) {
        lwsl_err("Sender: Failed to get WebRTCbin from pipeline\n");
        return;
    }
    g_object_set(webrtc, "latency", 50, NULL); // Default is 200ms
    g_object_set(webrtc, "bundle-policy", 2, NULL);
    g_object_set(webrtc, "stun-server", "stun://stun.l.google.com:19302", NULL);
    g_signal_connect(webrtc, "on-negotiation-needed", G_CALLBACK(on_negotiation_needed), wsi);
    g_signal_connect(webrtc, "on-ice-candidate", G_CALLBACK(on_ice_candidate), wsi);
    g_signal_connect(webrtc, "pad-added", G_CALLBACK(on_incoming_stream), data);


}



/* LWS callback for the sender */
static int websocket_callback(struct lws *wsi, enum lws_callback_reasons reason,
                              void *user, void *in, size_t len) {
    struct client_session_data *csd = (struct client_session_data *)user;

    switch (reason) {
    case LWS_CALLBACK_CLIENT_ESTABLISHED:
        lwsl_user("[Sender] WebSocket connection established\n");
        break;

    case LWS_CALLBACK_CLIENT_RECEIVE: {
        if (csd->len + len >= sizeof(csd->message)) {
            lwsl_err("Sender: Received too-long msg\n");
            return -1;
        }
        memcpy(csd->message + csd->len, in, len);
        csd->len += len;
        csd->message[csd->len] = '\0';

        if (lws_is_final_fragment(wsi)) {
            lwsl_user("\n[Sender] Complete message:\n%s\n", csd->message);

            if (!strncmp(csd->message, "SERVER_OFFER:", 13)) {
                const char *offer_sdp = csd->message + 13;
                lwsl_user("\n[Sender] Got SDP Offer:\n%s\n", offer_sdp);

                GstSDPMessage *sdp = NULL;
                if (gst_sdp_message_new_from_text(offer_sdp, &sdp) != GST_SDP_OK) {
                    lwsl_err("Sender: Failed to parse SDP Offer\n");
                } else {
                    GstWebRTCSessionDescription *offer =
                        gst_webrtc_session_description_new(GST_WEBRTC_SDP_TYPE_OFFER, sdp);
                    g_signal_emit_by_name(webrtc, "set-remote-description", offer, NULL);
                    gst_webrtc_session_description_free(offer);

                    GstPromise *promise =
                        gst_promise_new_with_change_func(on_answer_created, wsi, NULL);
                    g_signal_emit_by_name(webrtc, "create-answer", NULL, promise);
                }

            } else if (!strncmp(csd->message, "SERVER_ANSWER:", 14)) {
                const char *answer_sdp = csd->message + 14;
                lwsl_user("\n[Sender] Got SDP Answer:\n%s\n", answer_sdp);

                GstSDPMessage *sdp = NULL;
                if (gst_sdp_message_new_from_text(answer_sdp, &sdp) != GST_SDP_OK) {
                    lwsl_err("Sender: Failed to parse SDP Answer\n");
                } else {
                    GstWebRTCSessionDescription *answer =
                        gst_webrtc_session_description_new(GST_WEBRTC_SDP_TYPE_ANSWER, sdp);
                    g_signal_emit_by_name(webrtc, "set-remote-description", answer, NULL);
                    gst_webrtc_session_description_free(answer);
                }

            } else {
                // Possibly multiple "candidate:..." lines
                char *start = csd->message;
                while (start && *start) {
                    char *cand_line = strstr(start, "candidate:");
                    if (!cand_line) {
                        break;
                    }
                    char *line_end = strchr(cand_line, '\n');
                    if (line_end) {
                        *line_end = '\0';
                    }

                    lwsl_user("\n[Sender] Found remote candidate line:\n%s\n", cand_line);
                    const char *cand = cand_line + 10; // skip "candidate:"
                    handle_remote_candidate(cand);

                    if (line_end) {
                        start = line_end + 1;
                    } else {
                        break;
                    }
                }
            }

            csd->len = 0;
            csd->message[0] = '\0';
        }
        break;
    }

    default:
        break;
    }
    return 0;
}




static void print_candidates_once(void) {
    if (local_candidates) {
        lwsl_user("\n[Sender] Final Local ICE candidates:\n");
        for (guint i = 0; i < local_candidates->len; i++) {
            lwsl_user("  %s\n", (char *)g_ptr_array_index(local_candidates, i));
        }
    }
    if (remote_candidates) {
        lwsl_user("\n[Sender] Final Remote ICE candidates:\n");
        for (guint i = 0; i < remote_candidates->len; i++) {
            lwsl_user("  %s\n", (char *)g_ptr_array_index(remote_candidates, i));
        }
    }
}

static void connect_to_server() {
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = CONTEXT_PORT_NO_LISTEN;

    static struct lws_protocols protocols[] = {
        {
            "signaling-protocol",
            websocket_callback,
            sizeof(struct client_session_data),
            2048
        },
        {NULL, NULL, 0, 0}
    };
    info.protocols = protocols;

    context = lws_create_context(&info);
    if (!context) {
        lwsl_err("Sender: Failed to create LWS context\n");
        //return 1;
    }

    struct lws_client_connect_info ccinfo;
    memset(&ccinfo, 0, sizeof(ccinfo));
    ccinfo.context = context;
    ccinfo.address = "195.251.234.16";
    ccinfo.port = 12000;
    //ccinfo.address = "localhost";
    //ccinfo.port = 8080;    
    ccinfo.path = "/";
    ccinfo.protocol = "signaling-protocol";

    wsi = lws_client_connect_via_info(&ccinfo);
    if (!wsi) {
        lwsl_err("Sender: Failed to connect to server\n");
        lws_context_destroy(context);
        //return 1;
    }
}
/* WebSocket Service Function (Runs in a Separate Thread) */
static void *websocket_service_thread(void *arg) {
    while (1) {
        if (context) {
            lws_service(context, 0); // Process WebSocket events without blocking
        }
        g_usleep(50000); // Sleep for 50ms to prevent high CPU usage
    }
    return NULL;
}

static void *gstreamer_pipeline_thread(void *arg) {
    CustomData *data = (CustomData *)arg;
    
    // Start the pipeline
    gst_element_set_state(data->pipeline, GST_STATE_PLAYING);

    g_main_loop_run(data->main_loop); // Blocks, so must be in a thread

    return NULL;
}

int main(int argc, char *argv[]) {
   gtk_init(&argc, &argv);
    gst_init(&argc, &argv);
    lws_set_log_level(LLL_USER | LLL_ERR | LLL_WARN | LLL_NOTICE, NULL);


    lwsl_user("[Sender] Starting up...\n");
    
    local_candidates = g_ptr_array_new_with_free_func(g_free);
    remote_candidates = g_ptr_array_new_with_free_func(g_free);

   
        
    connect_to_server();
    create_pipeline(&data);
    /* Start GTK main loop*/ 
    create_ui(&data);
   // ✅ Initialize GStreamer main loop
    data.main_loop = g_main_loop_new(NULL, FALSE);


    pthread_t ws_thread;
    if (pthread_create(&ws_thread, NULL, websocket_service_thread, NULL) != 0) {
        g_printerr("[ERROR] Failed to create WebSocket thread\n");
        return -1;
    }
    //while (1) {
    //    lws_service(context, 1000);
    //}

    //pthread_t  gst_thread;
        // Start GStreamer pipeline thread
    //if (pthread_create(&gst_thread, NULL, gstreamer_pipeline_thread, (void *)&data) != 0) {
    //    g_printerr("[ERROR] Failed to create GStreamer thread\n");
    //    return -1;
    //}

    gtk_main();
    print_candidates_once();
    g_ptr_array_free(local_candidates, TRUE);
    g_ptr_array_free(remote_candidates, TRUE);

    //gst_element_set_state(pipeline, GST_STATE_NULL);
    //gst_object_unref(data->pipeline);
    lws_context_destroy(context);
    return 0;
}
