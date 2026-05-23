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
