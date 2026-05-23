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
