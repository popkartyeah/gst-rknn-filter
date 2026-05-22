# GStreamer RKNN 插件项目文档

## 1. 项目概述

GStreamer RKNN 插件是一个在 Rockchip NPU（如 RK3576、RK3588、RK3568）上运行视觉 AI 模型的 GStreamer 插件。它提供了硬件加速的视频推理功能，支持实时视频流的人脸检测、目标检测、图像分割等任务。

### 1.1 主要功能

- 支持多种视觉模型：YOLO 系列、RetinaFace、PPOCR、LPRNet 等
- 硬件加速：利用 RK3576/RK3588 的 NPU 和 RGA 加速器
- 实时推理：支持 `videotestsrc`、`v4l2src`、`rtmpsrc`、`uridecodebin` 等视频源
- 人脸检测与裁剪：从视频流中检测人脸并保存为 JPEG 图片
- 多语言支持：提供 C 和 Go 版本的示例代码

### 1.2 项目结构

```
gst-rknn-filter/
├── README.md              # 快速安装与使用指南
├── build.sh               # 编译脚本
├── meson.build            # Meson 构建配置
├── src/                   # 插件核心源码
│   ├── gstrknn.c          # GStreamer 插件主入口
│   ├── gstrknn.h          # 插件头文件
│   ├── gst_face_meta.c    # 人脸元数据处理
│   ├── gst_face_meta.h    # 人脸元数据结构
│   ├── rknnprocess.cc     # RKNN 推理处理
│   ├── rknnprocess.h      # RKNN 推理头文件
│   ├── rgaprocess.cc      # RGA 图像处理
│   ├── rgaprocess.h       # RGA 图像处理头文件
│   ├── retinaface.h       # RetinaFace 检测模型
│   ├── retinaface.cc      # RetinaFace 实现
│   ├── postprocess.cc     # 通用后处理
│   ├── postprocess_dispatcher.cc  # 后处理分发器
│   └── postprocess/       # 各模型后处理实现
│       ├── postprocess_retinaface.cc
│       ├── postprocess_yolov5.cc
│       ├── postprocess_yolov8.cc
│       └── ...
├── face_capture_tee.c     # C 版本人脸捕获示例
├── face_capture_tee.go    # Go 版本人脸捕获示例
├── model/                 # RKNN 模型文件
│   ├── RetinaFace_mobile320.rknn
│   ├── yolov5.rknn
│   └── ...
├── thirdparty/            # 第三方库
│   ├── librga/            # RGA 加速库
│   └── librknn_api/       # RKNN 运行时库
└── deploy_package/        # 部署包
```

---

## 2. 系统架构

### 2.1 整体架构图

```
┌─────────────────────────────────────────────────────────────┐
│                      GStreamer Pipeline                      │
├─────────────────────────────────────────────────────────────┤
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐  │
│  │ rtmpsrc /    │    │ mppvideodec  │    │ videocrop    │  │
│  │ v4l2src      │───▶│ (NPU解码)    │───▶│ (裁剪)      │  │
│  └──────────────┘    └──────────────┘    └──────────────┘  │
│                              │                               │
│                              ▼                               │
│  ┌────────────────────────────────────────────────────────┐  │
│  │                rknnfilter (本插件)                       │  │
│  │  ┌───────────────────────────────────────────────────┐  │  │
│  │  │ 1. 格式转换: RGB ↔ NV12 (RGA 加速)                │  │  │
│  │  │ 2. NPU推理: RetinaFace / YOLO                      │  │  │
│  │  │ 3. 人脸检测: 输出人脸坐标                          │  │  │
│  │  └───────────────────────────────────────────────────┘  │  │
│  └────────────────────────────────────────────────────────┘  │
│                              │                               │
│                              ▼                               │
│  ┌────────────────────────────────────────────────────────┐  │
│  │                   tee (分流)                             │  │
│  └────────────────────────────────────────────────────────┘  │
│                              │                               │
│               ┌──────────────┴──────────────┐               │
│               ▼                             ▼               │
│  ┌──────────────────────┐      ┌──────────────────────┐    │
│  │ meta_sink (appsink) │      │ jpeg_sink (appsink) │    │
│  │ (接收人脸坐标)       │      │ (接收JPEG图片)      │    │
│  └──────────────────────┘      └──────────────────────┘    │
│               │                             │               │
│               └──────────────┬──────────────┘               │
│                              ▼                               │
│  ┌────────────────────────────────────────────────────────┐  │
│  │  应用层: Go / C 程序处理人脸坐标 + JPEG 图片            │  │
│  │  - 使用 ffmpeg 裁剪人脸区域                             │  │
│  │  - 保存为 face_xxx.jpg                                  │  │
│  └────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

### 2.2 关键组件说明

#### 2.2.1 核心插件: `rknnfilter`

`rknnfilter` 是插件的核心，负责：
- **格式转换**: 使用 RGA 硬件加速器进行 NV12 ↔ RGB 格式转换
- **模型推理**: 通过 `librknnrt` 在 NPU 上运行 RKNN 模型
- **后处理**: 解析模型输出，得到人脸/目标坐标
- **元数据附加**: 将检测结果作为元数据附加到 GStreamer Buffer

#### 2.2.2 人脸元数据: `gst_face_meta`

自定义 GStreamer 元数据结构，用于传递人脸检测结果：

```c
typedef struct _GstFaceResult {
    float left;    // 人脸左上角 X
    float top;     // 人脸左上角 Y
    float right;   // 人脸右下角 X
    float bottom;  // 人脸右下角 Y
} GstFaceResult;

typedef struct _GstFaceMeta {
    int face_count;           // 检测到的人脸数量
    GstFaceResult faces[128]; // 人脸坐标列表
} GstFaceMeta;
```

#### 2.2.3 `tee` 分流器

`tee` 元件将视频流分成两路：
- 一路（`meta_sink`）: 仅提取元数据（人脸坐标）
- 另一路（`jpeg_sink`）: 经过 `videoconvert` → `mppjpegenc` 编码为 JPEG

---

## 3. 实现要点

### 3.1 Pipeline 配置

**C 版本** (`face_capture_tee.c`):

```c
char pipeline_str[2048];
snprintf(pipeline_str, sizeof(pipeline_str),
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
```

**Go 版本** (`face_capture_tee.go`):

```go
pipelineStr := fmt.Sprintf(
    "rtmpsrc location=%s ! "+
    "flvdemux name=demux ! "+
    "queue ! "+
    "h264parse ! "+
    "mppvideodec ! "+
    "video/x-raw,format=NV12 ! "+
    "videocrop top=0 bottom=270 left=480 right=480 ! "+
    "video/x-raw,format=NV12,width=960,height=810 ! "+
    "rknnfilter model-path=%s model-type=retinaface ! "+
    "video/x-raw,format=RGB ! "+
    "tee name=t "+
    "t. ! queue ! appsink name=meta_sink sync=false "+
    "t. ! queue ! videoconvert ! video/x-raw,format=NV12 ! mppjpegenc ! appsink name=jpeg_sink sync=false",
    app.rtmpURL, app.modelPath)
```

### 3.2 关键点：颜色空间转换

**问题**：`mppjpegenc` 硬件编码器对 RGB 格式处理异常，导致绿屏/花屏。

**解决方案**：
- 从 `rknnfilter` 输出的 RGB 格式，通过 `videoconvert` 转换为 NV12
- 再输入 `mppjpegenc` 进行硬件 JPEG 编码

**Pipeline 关键部分**:
```
rknnfilter ! video/x-raw,format=RGB ! videoconvert ! video/x-raw,format=NV12 ! mppjpegenc
```

### 3.3 `appsink` 回调处理

#### 3.3.1 C 版本

**元数据回调 (`meta_sample`)**：

```c
static GstFlowReturn meta_sample(GstAppSink *appsink, gpointer user_data) {
    GstSample *sample = gst_app_sink_pull_sample(appsink);
    GstBuffer *buffer = gst_sample_get_buffer(sample);

    // 提取人脸元数据
    GstFaceMeta *face_meta = gst_buffer_get_face_meta(buffer);

    g_mutex_lock(&g_meta_mutex);
    if (face_meta != NULL) {
        // 保存人脸坐标到全局变量
        g_cached_meta.face_count = face_meta->face_count;
        // ...
    }
    g_mutex_unlock(&g_meta_mutex);

    gst_sample_unref(sample);
    return GST_FLOW_OK;
}
```

**JPEG 回调 (`jpeg_sample`)**：

```c
static GstFlowReturn jpeg_sample(GstAppSink *appsink, gpointer user_data) {
    GstSample *sample = gst_app_sink_pull_sample(appsink);
    GstBuffer *buffer = gst_sample_get_buffer(sample);

    g_mutex_lock(&g_meta_mutex);
    if (g_cached_meta.valid && g_cached_meta.face_count > 0) {
        // 1. 查找离中心最近的人脸
        // 2. 将 JPEG Buffer 保存为临时文件
        // 3. 使用 ffmpeg 裁剪人脸区域
        // 4. 保存 face_xxx.jpg
    }
    g_mutex_unlock(&g_meta_mutex);

    gst_sample_unref(sample);
    return GST_FLOW_OK;
}
```

#### 3.3.2 Go 版本

使用 `go-gst` 库，结构与 C 版本类似。

### 3.4 FFMPEG 人脸裁剪

使用 `ffmpeg` 命令从完整帧中裁剪人脸区域：

```bash
ffmpeg -i temp_xxx.jpg -vf crop=W:H:X:Y -y face_xxx.jpg
```

其中：
- `W` = 人脸宽度 (right - left + 40，额外 20 像素边距)
- `H` = 人脸高度 (bottom - top + 40)
- `X` = 左上角 X (left - 20)
- `Y` = 左上角 Y (top - 20)

---

## 4. 数据流程

### 4.1 完整数据流

1. **视频源 → 解码**
   - `rtmpsrc` 接收 RTMP 视频流
   - `flvdemux` 解复用 FLV 容器
   - `mppvideodec` (NPU 硬件加速) 解码 H.264

2. **预处理**
   - `videocrop`: 裁剪视频帧（去除上下/左右边缘）
   - 输出: NV12, 960×810

3. **NPU 推理** (`rknnfilter`)
   - 格式转换 (RGA): NV12 → RGB
   - 预处理: 缩放至 320×320 (RetinaFace 输入尺寸)
   - NPU 推理: RetinaFace 模型
   - 后处理: 解析输出，得到人脸坐标
   - 附加元数据: `GstFaceMeta`

4. **分流与编码**
   - `tee`: 分流为两路
   - 一路 (`meta_sink`): 仅提取元数据
   - 另一路: RGB → NV12 (`videoconvert`) → `mppjpegenc` (JPEG 编码)

5. **应用层处理**
   - 从 `meta_sink` 接收人脸坐标
   - 从 `jpeg_sink` 接收 JPEG 图片
   - 使用 ffmpeg 裁剪人脸区域
   - 保存为 `face_xxx.jpg`

### 4.2 时序图

```
rtmpsrc     mppvideodec  videocrop  rknnfilter   tee     meta_sink  jpeg_sink
   │             │            │          │         │          │           │
   │──帧 1──────▶│            │          │         │          │           │
   │             │──帧 1─────▶│          │         │          │           │
   │             │            │──帧 1───▶│         │          │           │
   │             │            │          │─元数据─▶│          │           │
   │             │            │          │         │──回调──▶│           │
   │             │            │          │─ JPEG ─▶│          │           │
   │             │            │          │         │          │──回调────▶│
   │             │            │          │         │          │           │
   │──帧 2──────▶│            │          │         │          │           │
   │             │──帧 2─────▶│          │         │          │           │
   │             │            │──帧 2───▶│         │          │           │
   │             │            │          │         │          │           │
```

---

## 5. 编译与运行

### 5.1 编译 C 版本

```bash
cd /home/cat/memoark/gst-rknn-filter
gcc -o face_capture_tee_c face_capture_tee.c \
    $(pkg-config --cflags --libs gstreamer-1.0 gstreamer-app-1.0) \
    -I./src -L./build/src -lgstrknn -lm
```

### 5.2 编译 Go 版本

```bash
cd /home/cat/memoark/gst-rknn-filter
go build -o face_capture_tee_go face_capture_tee.go
```

### 5.3 运行示例

```bash
# C 版本
export GST_PLUGIN_PATH="$(pwd)/build/src:$GST_PLUGIN_PATH"
export LD_LIBRARY_PATH="$(pwd)/build/src:$LD_LIBRARY_PATH"
./face_capture_tee_c rtmp://192.168.1.62/live/qingting ./model/RetinaFace_mobile320.rknn

# Go 版本
./face_capture_tee_go rtmp://192.168.1.62/live/qingting ./model/RetinaFace_mobile320.rknn
```

输出文件在 `/tmp/faces_jpeg/` (C 版本) 和 `/tmp/faces_go_jpeg/` (Go 版本) 目录。

---

## 6. 硬件加速

### 6.1 硬件资源使用

| 模块 | 硬件资源 | 用途 |
|------|----------|------|
| `mppvideodec` | VPU (视频处理单元) | 硬件解码 H.264 |
| `videoconvert` (部分) | RGA (2D 图形加速) | NV12 ↔ RGB 转换 |
| `rknnfilter` | NPU (神经网络处理器) | RetinaFace 模型推理 |
| `mppjpegenc` | VPU / JPEG 编码引擎 | 硬件 JPEG 编码 |

### 6.2 性能优化

1. **使用 `tee` 而非多个 filter 实例**: 避免重复推理
2. **`sync=false`**: 禁用 `appsink` 的同步，避免阻塞
3. **队列缓冲**: `queue` 元件缓冲视频流，避免丢帧

---

## 7. 常见问题

### 7.1 问题 1: 绿屏 / 花屏

**原因**: `mppjpegenc` 硬件编码器对 RGB 格式支持异常。

**解决**: 添加 `videoconvert` 转换为 NV12 格式：
```
tee ! queue ! videoconvert ! video/x-raw,format=NV12 ! mppjpegenc
```

### 7.2 问题 2: JPEG 文件过小 (几百字节)

**原因**: ffmpeg 裁剪时，人脸坐标与 JPEG 图片时间戳不匹配，导致裁剪失败。

**解决**: 使用互斥锁 (`g_meta_mutex`) 同步元数据与 JPEG 数据。

### 7.3 问题 3: 找不到 `rknnfilter` 插件

**解决**: 设置环境变量：
```bash
export GST_PLUGIN_PATH="$(pwd)/build/src:$GST_PLUGIN_PATH"
export LD_LIBRARY_PATH="$(pwd)/build/src:$LD_LIBRARY_PATH"
```

---

## 8. 总结

本项目实现了：

1. **实时人脸检测**: 在 RK3576/RK3588 NPU 上运行 RetinaFace
2. **硬件加速**: 利用 RGA、NPU、VPU 全链路硬件加速
3. **人脸捕获**: 自动检测、裁剪、保存人脸为 JPEG
4. **多语言实现**: 提供 C 与 Go 两个版本

关键技术点：
- GStreamer 自定义元数据 (`gst_face_meta`)
- `tee` 分流器实现多路数据
- `appsink` 回调处理
- 颜色空间转换 (RGB ↔ NV12)
- FFMPEG 人脸区域裁剪
