#include <stdio.h>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include "gst_face_meta.h"

static GstFlowReturn new_sample(GstAppSink *appsink, gpointer user_data) {
    static int frame_count = 0;
    frame_count++;
    g_print("Frame %d: Got new sample!\n", frame_count);
    GstSample *sample = gst_app_sink_pull_sample(appsink);
    if (sample) {
        GstCaps *caps = gst_sample_get_caps(sample);
        gchar *caps_str = gst_caps_to_string(caps);
        g_print("  Caps: %s\n", caps_str);
        g_free(caps_str);
        
        GstBuffer *buffer = gst_sample_get_buffer(sample);
        
        GstFaceMeta *face_meta = gst_face_meta_get(buffer);
        if (face_meta) {
            g_print("  Found face meta with %d faces!\n", face_meta->face_count);
        } else {
            g_print("  NO face meta found!\n");
        }
        
        g_print("  Buffer size: %lu\n", (unsigned long)gst_buffer_get_size(buffer));
        gst_sample_unref(sample);
    }
    return GST_FLOW_OK;
}

int main(int argc, char **argv) {
    gst_init(&argc, &argv);
    
    const gchar *pipeline_str = 
        "rtmpsrc location=rtmp://192.168.1.62/live/qingting ! "
        "flvdemux name=demux ! "
        "queue ! "
        "h264parse ! "
        "mppvideodec ! "
        "video/x-raw,format=NV12 ! "
        "videocrop top=0 bottom=270 left=480 right=480 ! "
        "video/x-raw,format=NV12,width=960,height=810 ! "
        "rknnfilter model-path=/home/cat/memoark/gst-rknn-filter/model/RetinaFace_mobile320.rknn model-type=retinaface ! "
        "video/x-raw,format=RGB ! "
        "mppjpegenc ! "
        "appsink name=appsink sync=false";
    
    GstElement *pipeline = gst_parse_launch(pipeline_str, NULL);
    g_print("Pipeline: %s\n", pipeline_str);
    
    GstElement *appsink = gst_bin_get_by_name(GST_BIN(pipeline), "appsink");
    gst_app_sink_set_emit_signals(GST_APP_SINK(appsink), TRUE);
    g_signal_connect(appsink, "new-sample", G_CALLBACK(new_sample), NULL);
    
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    
    GstBus *bus = gst_element_get_bus(pipeline);
    GstMessage *msg = gst_bus_timed_pop_filtered(bus, 20 * GST_SECOND, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);
    
    if (msg) {
        GError *err = NULL;
        gchar *debug = NULL;
        
        if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
            gst_message_parse_error(msg, &err, &debug);
            g_print("Error: %s\n", err ? err->message : "unknown");
            if (debug) {
                g_print("Debug: %s\n", debug);
                g_free(debug);
            }
            if (err) {
                g_error_free(err);
            }
        }
        gst_message_unref(msg);
    }
    
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    gst_object_unref(bus);
    
    return 0;
}