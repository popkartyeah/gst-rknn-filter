#ifndef _RKNN_PROCESS_H_
#define _RKNN_PROCESS_H_
#include "rknn_api.h"

/* Model type for postprocess dispatch (根据现有模型) */
typedef enum {
    RKNN_MODEL_YOLOV5,
    RKNN_MODEL_YOLOV6,
    RKNN_MODEL_YOLOV7,
    RKNN_MODEL_YOLOV8,
    RKNN_MODEL_YOLOV8_OBB,
    RKNN_MODEL_YOLOV10,
    RKNN_MODEL_YOLO11,
    RKNN_MODEL_PPYOLOE,
    RKNN_MODEL_YOLOV8_POSE,
    RKNN_MODEL_DEEPLABV3,
    RKNN_MODEL_YOLOV5_SEG,
    RKNN_MODEL_YOLOV8_SEG,
    RKNN_MODEL_PPSEG,
    RKNN_MODEL_RETINAFACE,
    RKNN_MODEL_LPRNET,
    RKNN_MODEL_PPOCR_DET,
    RKNN_MODEL_PPOCR_REC,
    RKNN_MODEL_PASSTHROUGH,
    RKNN_MODEL_MAX
} RknnModelType;

typedef struct __BOX_RECT
{
    int left;
    int right;
    int top;
    int bottom;
} BOX_RECT;

#define OBJ_NAME_MAX_SIZE 16
#define OBJ_NUMB_MAX_SIZE 128

typedef struct __detect_result_t
{
    char name[OBJ_NAME_MAX_SIZE];
    BOX_RECT box;
    float prop;
} detect_result_t;

typedef struct _detect_result_group_t
{
    int id;
    int count;
    detect_result_t results[OBJ_NUMB_MAX_SIZE];
} detect_result_group_t;

struct _RknnProcess {
    rknn_context ctx;
    rknn_input* inputs;
    rknn_output* outputs;
    rknn_input_output_num io_num;
    rknn_tensor_attr *input_attrs;
    rknn_tensor_attr *output_attrs;
    int model_width;
    int model_height;
    int model_channel;
    BOX_RECT pads;
    float scale_w;
    float scale_h;
    int original_width;
    int original_height;
    unsigned char* model_data;
    char* label_path;
    char* model_path;
    RknnModelType model_type;
    detect_result_group_t last_detect_result;
    int outputs_acquired;
};

#ifdef __cplusplus
extern "C" {
#endif

int rknn_prepare(struct _RknnProcess* rknn_process);
int rknn_inference_and_postprocess(
    struct _RknnProcess* rknn_process,
    void* orig_img,
    float box_conf_threshold,
    float nms_threshold,
    int show_fps,      
    double current_fps,
    int do_inference,   // 是否执行推理，0=不推理，1=推理
    int64_t unused      // 未使用的参数，为了与调用代码兼容
);
void rknn_release(struct _RknnProcess* rknn_process);
#ifdef __cplusplus
}
#endif

#endif