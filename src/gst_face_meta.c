#include "gst_face_meta.h"
#include <string.h>

static const GstMetaInfo* gst_face_meta_info = NULL;

// Structure for easy access from Python
typedef struct {
    int face_count;
    char names[128][16];
    int lefts[128];
    int rights[128];
    int tops[128];
    int bottoms[128];
    float props[128];
} FaceMetaResult;

// Helper function for Python to extract face meta
__attribute__((visibility("default")))
int gst_face_meta_extract(gpointer buffer_ptr, FaceMetaResult* result) {
    if (!buffer_ptr || !result) {
        return -1;
    }
    
    GstBuffer* buffer = (GstBuffer*)buffer_ptr;
    GstFaceMeta* face_meta = gst_face_meta_get(buffer);
    
    if (!face_meta) {
        return 0;
    }
    
    result->face_count = face_meta->face_count;
    for (int i = 0; i < face_meta->face_count && i < 128; i++) {
        strncpy(result->names[i], face_meta->faces[i].name, 15);
        result->names[i][15] = '\0';
        result->lefts[i] = face_meta->faces[i].left;
        result->rights[i] = face_meta->faces[i].right;
        result->tops[i] = face_meta->faces[i].top;
        result->bottoms[i] = face_meta->faces[i].bottom;
        result->props[i] = face_meta->faces[i].prop;
    }
    
    return face_meta->face_count;
}

static gboolean gst_face_meta_init(GstMeta* meta, gpointer params, GstBuffer* buffer) {
    GstFaceMeta* face_meta = (GstFaceMeta*)meta;
    (void)params;
    (void)buffer;
    face_meta->face_count = 0;
    return TRUE;
}

static void gst_face_meta_free(GstMeta* meta, GstBuffer* buffer) {
    (void)meta;
    (void)buffer;
}

static gboolean gst_face_meta_transform(GstBuffer* dest, GstMeta* src_meta, GstBuffer* src_buffer, GQuark type, gpointer data) {
    GstFaceMeta* src_face_meta = (GstFaceMeta*)src_meta;
    
    // Always copy meta - let any plugin decide if it wants it
    GstFaceMeta* dest_face_meta = gst_face_meta_add(dest, src_face_meta->face_count, NULL);
    
    if (!dest_face_meta) {
        return FALSE;
    }
    
    for (int i = 0; i < src_face_meta->face_count && i < 128; i++) {
        dest_face_meta->faces[i] = src_face_meta->faces[i];
    }
    dest_face_meta->face_count = src_face_meta->face_count;
    
    return TRUE;
}

GType gst_face_meta_api_get_type(void) {
    static GType type = 0;
    static const gchar *tags[] = { "face", NULL };
    
    if (g_once_init_enter(&type)) {
        GType tmp = gst_meta_api_type_register("GstFaceMetaAPI", tags);
        g_once_init_leave(&type, tmp);
    }
    return type;
}

const GstMetaInfo* gst_face_meta_get_info(void) {
    if (g_once_init_enter(&gst_face_meta_info)) {
        const GstMetaInfo* info = gst_meta_register(
            GST_FACE_META_API_TYPE,
            "GstFaceMeta",
            sizeof(GstFaceMeta),
            (GstMetaInitFunction)gst_face_meta_init,
            (GstMetaFreeFunction)gst_face_meta_free,
            (GstMetaTransformFunction)gst_face_meta_transform
        );
        g_once_init_leave(&gst_face_meta_info, info);
    }
    return gst_face_meta_info;
}

GstFaceMeta* gst_face_meta_add(GstBuffer* buffer, gint face_count, const detect_result_t* results) {
    GstFaceMeta* meta = (GstFaceMeta*)gst_buffer_add_meta(buffer, gst_face_meta_get_info(), NULL);
    if (!meta) return NULL;

    meta->face_count = face_count;
    if (results != NULL) {
        for (int i = 0; i < face_count && i < 128; i++) {
            strncpy(meta->faces[i].name, results[i].name, OBJ_NAME_MAX_SIZE - 1);
            meta->faces[i].name[OBJ_NAME_MAX_SIZE - 1] = '\0';
            meta->faces[i].left = results[i].box.left;
            meta->faces[i].right = results[i].box.right;
            meta->faces[i].top = results[i].box.top;
            meta->faces[i].bottom = results[i].box.bottom;
            meta->faces[i].prop = results[i].prop;
        }
    }
    return meta;
}

GstFaceMeta* gst_face_meta_get(GstBuffer* buffer) {
    return (GstFaceMeta*)gst_buffer_get_meta(buffer, GST_FACE_META_API_TYPE);
}