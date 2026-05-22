#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include "gst_face_meta.h"

#define OUTPUT_DIR "/tmp/faces"

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
                // Extract face from RGB frame
                guint8 *frame_data = map_info.data;
                gint stride = width * 3;
                
                // Allocate buffer for face image (BGR format for JPEG)
                guint8 *face_bgr = (guint8 *)malloc(face_width * face_height * 3);
                
                for (gint y = 0; y < face_height; y++) {
                    for (gint x = 0; x < face_width; x++) {
                        gint src_idx = (y1 + y) * stride + (x1 + x) * 3;
                        gint dst_idx = y * face_width * 3 + x * 3;
                        // RGB to BGR
                        face_bgr[dst_idx] = frame_data[src_idx + 2];
                        face_bgr[dst_idx + 1] = frame_data[src_idx + 1];
                        face_bgr[dst_idx + 2] = frame_data[src_idx];
                    }
                }
                
                // Save as BMP (simpler than JPEG)
                time_t t = time(NULL);
                gchar filename[256];
                snprintf(filename, sizeof(filename), "%s/face_%ld.bmp", OUTPUT_DIR, t);
                
                FILE *f = fopen(filename, "wb");
                if (f) {
                    // BMP header
                    unsigned char header[54] = {
                        0x42, 0x4D,           // Magic
                        0, 0, 0, 0,           // Size
                        0, 0, 0, 0,           // Reserved
                        54, 0, 0, 0,          // Offset to pixel data
                        40, 0, 0, 0,          // Info header size
                        0, 0, 0, 0,           // Width
                        0, 0, 0, 0,           // Height
                        1, 0,                 // Planes
                        24, 0,                // Bits per pixel
                        0, 0, 0, 0,           // Compression
                        0, 0, 0, 0,           // Size of pixel data
                        0, 0, 0, 0,           // X pixels per meter
                        0, 0, 0, 0,           // Y pixels per meter
                        0, 0, 0, 0,           // Used colors
                        0, 0, 0, 0            // Important colors
                    };
                    
                    gint bmp_size = 54 + face_width * face_height * 3;
                    header[2] = (bmp_size >> 0) & 0xFF;
                    header[3] = (bmp_size >> 8) & 0xFF;
                    header[4] = (bmp_size >> 16) & 0xFF;
                    header[5] = (bmp_size >> 24) & 0xFF;
                    header[18] = (face_width >> 0) & 0xFF;
                    header[19] = (face_width >> 8) & 0xFF;
                    header[20] = (face_width >> 16) & 0xFF;
                    header[21] = (face_width >> 24) & 0xFF;
                    header[22] = (face_height >> 0) & 0xFF;
                    header[23] = (face_height >> 8) & 0xFF;
                    header[24] = (face_height >> 16) & 0xFF;
                    header[25] = (face_height >> 24) & 0xFF;
                    
                    fwrite(header, 1, 54, f);
                    
                    // Write pixel data (bottom-up)
                    for (gint y = face_height - 1; y >= 0; y--) {
                        fwrite(face_bgr + y * face_width * 3, 1, face_width * 3, f);
                    }
                    
                    fclose(f);
                    data->save_count++;
                    g_print("[%d] Saved: %s (face at %d,%d-%d,%d)\n", 
                            data->save_count, filename, x1, y1, x2, y2);
                }
                
                free(face_bgr);
            }
        }
    } else {
        g_print("Frame %d: No face meta found\n", data->frame_count);
    }
    
    gst_buffer_unmap(buffer, &map_info);
    gst_sample_unref(sample);
    
    return GST_FLOW_OK;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        g_print("Usage: %s <rtmp_url> <model_path>\n", argv[0]);
        return 1;
    }
    
    gst_init(&argc, &argv);
    
    FaceCaptureData data = {
        .rtmp_url = argv[1],
        .model_path = argv[2],
        .frame_skip = 3,
        .frame_count = 0,
        .save_count = 0
    };
    
    // Create output directory
    mkdir(OUTPUT_DIR, 0755);
    
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
        "appsink name=appsink sync=false",
        data.rtmp_url, data.model_path
    );
    
    GstElement *pipeline = gst_parse_launch(pipeline_str, NULL);
    g_free(pipeline_str);
    
    GstElement *appsink = gst_bin_get_by_name(GST_BIN(pipeline), "appsink");
    gst_app_sink_set_emit_signals(GST_APP_SINK(appsink), TRUE);
    g_signal_connect(appsink, "new-sample", G_CALLBACK(new_sample), &data);
    
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    
    GstBus *bus = gst_element_get_bus(pipeline);
    GstMessage *msg;
    
    while ((msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE, 
            GST_MESSAGE_ERROR | GST_MESSAGE_EOS))) {
        if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
            GError *err;
            gchar *debug_info;
            gst_message_parse_error(msg, &err, &debug_info);
            g_printerr("Error received from element %s: %s\n", 
                       GST_OBJECT_NAME(msg->src), err->message);
            g_printerr("Debugging information: %s\n", debug_info ? debug_info : "none");
            g_clear_error(&err);
            g_free(debug_info);
        } else if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_EOS) {
            g_print("End-Of-Stream reached.\n");
        }
        gst_message_unref(msg);
    }
    
    gst_object_unref(bus);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    
    return 0;
}