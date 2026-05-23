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
