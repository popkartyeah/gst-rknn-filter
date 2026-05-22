#!/bin/bash
set -e

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" &>/dev/null && pwd)
BUILD_DIR="${SCRIPT_DIR}/build"
DEPLOY_DIR="${SCRIPT_DIR}/deploy_package"

echo "=== GST-RKNN-Filter 部署打包脚本 ==="

# 检查是否已编译
if [ ! -d "${BUILD_DIR}" ]; then
    echo "错误: 未找到 build 目录，请先运行 build.sh 编译项目"
    exit 1
fi

# 创建部署包目录
rm -rf "${DEPLOY_DIR}"
mkdir -p "${DEPLOY_DIR}"
mkdir -p "${DEPLOY_DIR}/lib"
mkdir -p "${DEPLOY_DIR}/model"
mkdir -p "${DEPLOY_DIR}/scripts"

# 1. 复制编译的 GStreamer 插件
PLUGIN_FILE="${BUILD_DIR}/src/libgstrknn.so"
if [ -f "${PLUGIN_FILE}" ]; then
    echo "复制 GStreamer 插件: ${PLUGIN_FILE}"
    cp "${PLUGIN_FILE}" "${DEPLOY_DIR}/lib/"
else
    echo "错误: 未找到编译的插件 ${PLUGIN_FILE}"
    exit 1
fi

# 2. 复制模型文件（可选，用户可自行选择）
echo "复制模型文件..."
cp -r "${SCRIPT_DIR}/model/" "${DEPLOY_DIR}/"

# 3. 创建目标设备安装脚本
cat > "${DEPLOY_DIR}/scripts/install.sh" << 'EOF'
#!/bin/bash
set -e

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" &>/dev/null && pwd)
DEPLOY_DIR=$(dirname "${SCRIPT_DIR}")

echo "=== 安装 GST-RKNN-Filter 到目标设备 ==="

# 检查系统依赖
echo "检查系统依赖..."
if ! ldconfig -p | grep -q librga; then
    echo "警告: 未找到 librga，请确保已安装 librga-dev"
fi
if ! ldconfig -p | grep -q librknnrt; then
    echo "警告: 未找到 librknnrt，请确保已安装 rknn-toolkit2"
fi

# 1. 安装 GStreamer 插件
GST_PLUGIN_DIR=$(pkg-config --variable=pluginsdir gstreamer-1.0)
if [ -z "${GST_PLUGIN_DIR}" ]; then
    GST_PLUGIN_DIR="/usr/lib/gstreamer-1.0"
fi
echo "GStreamer 插件目录: ${GST_PLUGIN_DIR}"
cp "${DEPLOY_DIR}/lib/libgstrknn.so" "${GST_PLUGIN_DIR}/"

# 2. 复制模型文件到系统目录
echo "安装模型文件..."
MODEL_DIR="/opt/gst-rknn-model"
mkdir -p "${MODEL_DIR}"
cp -r "${DEPLOY_DIR}/model/"* "${MODEL_DIR}/"

# 3. 更新 GStreamer 插件缓存
echo "更新 GStreamer 插件缓存..."
gst-inspect-1.0 rknnfilter

echo ""
echo "=== 安装完成 ==="
echo "插件路径: ${GST_PLUGIN_DIR}/libgstrknn.so"
echo "模型路径: ${MODEL_DIR}"
echo ""
echo "使用示例:"
echo "  gst-launch-1.0 videotestsrc ! rknnfilter model-path=${MODEL_DIR}/yolov5s.rknn ! autovideosink"
EOF

chmod +x "${DEPLOY_DIR}/scripts/install.sh"

# 4. 创建快速使用脚本示例
cat > "${DEPLOY_DIR}/scripts/run_demo.sh" << 'EOF'
#!/bin/bash
MODEL_DIR="/opt/gst-rknn-model"

echo "=== GST-RKNN-Filter 演示脚本 ==="
echo "可用模型:"
ls -la "${MODEL_DIR}/"*.rknn 2>/dev/null || echo "  未找到模型文件"

echo ""
echo "示例命令:"
echo "  # YOLOv5 目标检测"
echo "  gst-launch-1.0 videotestsrc ! rknnfilter model-path=${MODEL_DIR}/yolov5s.rknn ! autovideosink"
echo ""
echo "  # 从摄像头实时检测"
echo "  gst-launch-1.0 v4l2src device=/dev/video0 ! video/x-raw,width=640,height=480 ! rknnfilter model-path=${MODEL_DIR}/yolov5n.rknn ! autovideosink"
echo ""
echo "  # 视频文件处理"
echo "  gst-launch-1.0 filesrc location=input.mp4 ! qtdemux ! h264parse ! avdec_h264 ! rknnfilter model-path=${MODEL_DIR}/yolov8n.rknn ! autovideosink"
EOF

chmod +x "${DEPLOY_DIR}/scripts/run_demo.sh"

# 5. 创建 README
cat > "${DEPLOY_DIR}/README.md" << 'EOF'
# GST-RKNN-Filter 部署包

这是 gst-rknn-filter GStreamer 插件的部署包，用于在 RK3576 设备上进行 AI 推理加速。

## 安装方法

1. 将整个 deploy_package 目录复制到目标 RK3576 设备上
2. 运行安装脚本：
   ```bash
   cd deploy_package/scripts
   sudo ./install.sh
   ```

## 使用方法

安装完成后，可以使用以下命令测试：

```bash
# 检查插件是否正确安装
gst-inspect-1.0 rknnfilter

# 使用示例
gst-launch-1.0 videotestsrc ! rknnfilter model-path=/opt/gst-rknn-model/yolov5s.rknn ! autovideosink
```

## 目录结构

- lib/: GStreamer 插件
- model/: RKNN 模型文件
- scripts/: 安装和演示脚本

## 系统依赖要求

目标设备需要预先安装以下系统库：
- librga (RGA 硬件加速库)
- librknnrt (RKNN 推理库)

> **注意**: 插件已移除 OpenCV 依赖，使用纯 C/C++ 实现绘制功能

## 支持的模型

- YOLOv5/v6/v7/v8/v10/YOLO11 (目标检测)
- YOLOv8-seg (实例分割)
- YOLOv8-pose (姿态估计)
- YOLOv8-obb (旋转目标检测)
- YOLOX (目标检测)
- PP-YOLOE (目标检测)
- RetinaFace (人脸检测)
- MobileNet (分类)
- DeepLab-v3+ (语义分割)
- PP-OCR (文字检测/识别)
- MobileSAM (分割)
- LPRNet (车牌识别)
- MMS-TTS (语音合成)
EOF

echo ""
echo "=== 部署包创建完成 ==="
echo "部署包路径: ${DEPLOY_DIR}"
echo ""
echo "部署步骤:"
echo "1. 将 ${DEPLOY_DIR} 目录复制到目标 RK3576 设备"
echo "2. 在目标设备上运行:"
echo "   sudo ./scripts/install.sh"
echo ""
echo "部署包内容:"
ls -la "${DEPLOY_DIR}/"
echo ""
echo "库文件:"
ls -la "${DEPLOY_DIR}/lib/"
echo ""
echo "模型文件数量: $(ls -1 "${DEPLOY_DIR}/model/"*.rknn 2>/dev/null | wc -l) 个"
