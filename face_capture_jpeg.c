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
    gchar *rtmp_url;
    gchar *model_path;
    gint frame_skip;
    gint frame_count;
    gint save_count;
} FaceCaptureData;

static GstFlowReturn new_sample(GstAppSink *appsink, gpointer user_data) {
    FaceCaptureData *data = (FaceCaptureData *)user_data;
    GstSample *sample = gst_app_sink_pull_sample(appsink);
    
    if (!sample) {
        return GST_FLOW_OK;
    }
    
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    GstCaps *caps = gst_sample_get_caps(sample);
    
    GstStructure *structure = gst_caps_get_structure(caps, 0);
    gint width, height;
    gst_structure_get_int(structure, "width", &width);
    gst_structure_get_int(structure, "height", &height);
    
    GstMapInfo map_info;
    if (!gst_buffer_map(buffer, &map_info, GST_MAP_READ)) {
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }
    
    data->frame_count++;
    
    if (data->frame_count % (data->frame_skip + 1) != 0) {
        gst_buffer_unmap(buffer, &map_info);
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }
    
    GstFaceMeta *face_meta = (GstFaceMeta *)gst_buffer_get_meta(buffer, GST_FACE_META_API_TYPE);
    
    if (face_meta && face_meta->face_count > 0) {
        g_print("Frame %d: Found %d faces\n", data->frame_count, face_meta->face_count);
        
        // Find nearest face to center
        gint center_x = width / 2;
        gint center_y = height / 2;
        GstFaceResult *nearest = NULL;
        gfloat min_dist = 1e9;
        
        for (gint i = 0; i < face_meta->face_count; i++) {
            GstFaceResult *face = &face_meta->faces[i];
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
                        data->save_count++;
                        g_print("[%d] Saved: %s (face at %d,%d-%d,%d)\n", 
                                data->save_count, output_file, x1, y1, x2, y2);
                    } else {
                        g_print("ffmpeg failed with return code: %d\n", ret);
                    }
                    
                    // Cleanup temp file
                    unlink(temp_jpeg);
                }
            }
        }
    }
    
    gst_buffer_unmap(buffer, &map_info);
    gst_sample_unref(sample);
    
    return GST_FLOW_OK;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        g_printerr("Usage: %s <rtmp-url> <model-path>\n", argv[0]);
        return -1;
    }
    
    gst_init(&argc, &argv);
    
    // Create output directory
    gchar *mkdir_cmd = g_strdup_printf("mkdir -p %s", OUTPUT_DIR);
    system(mkdir_cmd);
    g_free(mkdir_cmd);
    
    FaceCaptureData data;
    data.rtmp_url = argv[1];
    data.model_path = argv[2];
    data.frame_skip = 3;
    data.frame_count = 0;
    data.save_count = 0;
    
    g_print("Testing mppjpegenc + JPEG + ffmpeg crop mode\n");
    
    // Pipeline with mppjpegenc
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
        "mppjpegenc ! "
        "appsink name=appsink sync=false",
        data.rtmp_url, data.model_path);
    
    g_print("Pipeline: %s\n", pipeline_str);
    
    GstElement *pipeline = gst_parse_launch(pipeline_str, NULL);
    g_free(pipeline_str);
    
    GstElement *appsink = gst_bin_get_by_name(GST_BIN(pipeline), "appsink");
    gst_app_sink_set_emit_signals(GST_APP_SINK(appsink), TRUE);
    g_signal_connect(appsink, "new-sample", G_CALLBACK(new_sample), &data);
    
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    
    GstBus *bus = gst_element_get_bus(pipeline);
    GstMessage *msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);
    
    if (msg) {
        gst_message_unref(msg);
    }
    
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    gst_object_unref(bus);
    
    return 0;
}