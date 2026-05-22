#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include "gst_face_meta.h"

#define OUTPUT_DIR "/tmp/faces_jpeg"

typedef struct {
    gint face_count;
    GstFaceResult faces[128];
    gint64 pts;
    gboolean valid;
} CachedFaceMeta;

static CachedFaceMeta g_cached_meta = {.valid = FALSE};
static GMutex g_meta_mutex;
static gint frame_count = 0;
static gint save_count = 0;

// Callback for meta (RGB) appsink
static GstFlowReturn meta_sample(GstAppSink *appsink, gpointer user_data) {
    GstSample *sample = gst_app_sink_pull_sample(appsink);
    if (!sample) return GST_FLOW_OK;
    
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    GstFaceMeta *face_meta = gst_face_meta_get(buffer);
    
    g_mutex_lock(&g_meta_mutex);
    if (face_meta && face_meta->face_count > 0) {
        g_cached_meta.face_count = face_meta->face_count;
        for (int i = 0; i < face_meta->face_count && i < 128; i++) {
            g_cached_meta.faces[i] = face_meta->faces[i];
        }
        g_cached_meta.pts = GST_BUFFER_PTS(buffer);
        g_cached_meta.valid = TRUE;
    } else {
        g_cached_meta.valid = FALSE;
    }
    g_mutex_unlock(&g_meta_mutex);
    
    gst_sample_unref(sample);
    return GST_FLOW_OK;
}

// Callback for JPEG appsink
static GstFlowReturn jpeg_sample(GstAppSink *appsink, gpointer user_data) {
    GstSample *sample = gst_app_sink_pull_sample(appsink);
    if (!sample) return GST_FLOW_OK;
    
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    GstCaps *caps = gst_sample_get_caps(sample);
    
    gint width, height;
    gst_structure_get_int(gst_caps_get_structure(caps, 0), "width", &width);
    gst_structure_get_int(gst_caps_get_structure(caps, 0), "height", &height);
    
    g_mutex_lock(&g_meta_mutex);
    if (g_cached_meta.valid && g_cached_meta.face_count > 0) {
        // Find nearest face to center
        gint center_x = width / 2;
        gint center_y = height / 2;
        GstFaceResult *nearest = NULL;
        gfloat min_dist = 1e9;
        
        for (gint i = 0; i < g_cached_meta.face_count; i++) {
            GstFaceResult *face = &g_cached_meta.faces[i];
            gint face_center_x = (face->left + face->right) / 2;
            gint face_center_y = (face->top + face->bottom) / 2;
            gfloat dist = sqrt(pow(face_center_x - center_x, 2) + pow(face_center_y - center_y, 2));
            
            if (dist < min_dist) {
                min_dist = dist;
                nearest = face;
            }
        }
        
        if (nearest) {
            gint x1 = MAX(0, nearest->left - 20);
            gint y1 = MAX(0, nearest->top - 20);
            gint x2 = MIN(width, nearest->right + 20);
            gint y2 = MIN(height, nearest->bottom + 20);
            
            gint face_width = x2 - x1;
            gint face_height = y2 - y1;
            
            if (face_width > 0 && face_height > 0) {
                GstMapInfo map_info;
                if (gst_buffer_map(buffer, &map_info, GST_MAP_READ)) {
                    // Save JPEG buffer to temp file
                    time_t t = time(NULL);
                    gchar temp_jpeg[256];
                    gchar output_file[256];
                    snprintf(temp_jpeg, sizeof(temp_jpeg), "%s/temp_%ld.jpg", OUTPUT_DIR, t);
                    snprintf(output_file, sizeof(output_file), "%s/face_%ld.jpg", OUTPUT_DIR, t);
                    
                    FILE *f = fopen(temp_jpeg, "wb");
                    if (f) {
                        fwrite(map_info.data, 1, map_info.size, f);
                        fclose(f);
                        
                        // Use ffmpeg to crop face from JPEG
                        gchar ffmpeg_cmd[1024];
                        snprintf(ffmpeg_cmd, sizeof(ffmpeg_cmd), 
                                "ffmpeg -i %s -vf 'crop=%d:%d:%d:%d' -y %s 2>/dev/null",
                                temp_jpeg, face_width, face_height, x1, y1, output_file);
                        
                        int ret = system(ffmpeg_cmd);
                        if (ret == 0) {
                            save_count++;
                            g_print("[JPEG] [%d] Saved: %s (face at %d,%d-%d,%d)\n", 
                                    save_count, output_file, x1, y1, x2, y2);
                        }
                        
                        unlink(temp_jpeg);
                    }
                    
                    gst_buffer_unmap(buffer, &map_info);
                }
            }
        }
        
        g_cached_meta.valid = FALSE;
    }
    g_mutex_unlock(&g_meta_mutex);
    
    gst_sample_unref(sample);
    return GST_FLOW_OK;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        g_printerr("Usage: %s <rtmp-url> <model-path>\n", argv[0]);
        return -1;
    }
    
    gst_init(&argc, &argv);
    g_mutex_init(&g_meta_mutex);
    
    // Create output directory
    gchar *mkdir_cmd = g_strdup_printf("mkdir -p %s", OUTPUT_DIR);
    system(mkdir_cmd);
    g_free(mkdir_cmd);
    
    g_print("Testing tee + dual appsink mode (meta + JPEG)\n");
    
    // Pipeline with tee - split into two branches
    gchar *pipeline_str = g_strdup_printf(
        "rtmpsrc location=%s ! "
        "flvdemux name=demux ! "
        "queue ! "
        "h264parse ! "
        "mppvideodec ! "
        "video/x-raw,format=NV12 ! "
        "videocrop top=0 bottom=270 left=480 right=480 ! "
        "video/x-raw,format=NV12,width=960,height=810 ! "
        "rknnfilter model-path=%s model-type=retinaface ! "
        "video/x-raw,format=RGB ! "
        "tee name=t "
        "t. ! queue ! appsink name=meta_sink sync=false "
        "t. ! queue ! videoconvert ! video/x-raw,format=NV12 ! mppjpegenc ! appsink name=jpeg_sink sync=false",
        argv[1], argv[2]);
    
    g_print("Pipeline: %s\n", pipeline_str);
    
    GstElement *pipeline = gst_parse_launch(pipeline_str, NULL);
    g_free(pipeline_str);
    
    // Setup meta appsink (RGB with face meta)
    GstElement *meta_sink = gst_bin_get_by_name(GST_BIN(pipeline), "meta_sink");
    gst_app_sink_set_emit_signals(GST_APP_SINK(meta_sink), TRUE);
    g_signal_connect(meta_sink, "new-sample", G_CALLBACK(meta_sample), NULL);
    
    // Setup JPEG appsink
    GstElement *jpeg_sink = gst_bin_get_by_name(GST_BIN(pipeline), "jpeg_sink");
    gst_app_sink_set_emit_signals(GST_APP_SINK(jpeg_sink), TRUE);
    g_signal_connect(jpeg_sink, "new-sample", G_CALLBACK(jpeg_sample), NULL);
    
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    
    GstBus *bus = gst_element_get_bus(pipeline);
    GstMessage *msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);
    
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
    g_mutex_clear(&g_meta_mutex);
    
    return 0;
}