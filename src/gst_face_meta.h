#ifndef _GST_FACE_META_H_
#define _GST_FACE_META_H_

#include <gst/gst.h>
#include "rknnprocess.h"

#define OBJ_NAME_MAX_SIZE 16

#define GST_FACE_META_API_TYPE (gst_face_meta_api_get_type())
#define GST_FACE_META_INFO (gst_face_meta_get_info())

typedef struct _GstFaceResult {
    char name[OBJ_NAME_MAX_SIZE];
    gint left;
    gint right;
    gint top;
    gint bottom;
    gfloat prop;
} GstFaceResult;

typedef struct _GstFaceMeta {
    GstMeta meta;
    gint face_count;
    GstFaceResult faces[128];
} GstFaceMeta;

GType gst_face_meta_api_get_type(void);
const GstMetaInfo* gst_face_meta_get_info(void);
GstFaceMeta* gst_face_meta_add(GstBuffer* buffer, gint face_count, const detect_result_t* results);
GstFaceMeta* gst_face_meta_get(GstBuffer* buffer);

#endif