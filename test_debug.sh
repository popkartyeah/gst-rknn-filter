#!/bin/bash

LOG_DIR="/home/cat/memoark/gst-rknn-filter/debug_logs"
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
GST_LOG_FILE="$LOG_DIR/gst_debug_$TIMESTAMP.log"

mkdir -p "$LOG_DIR"

echo "=== Starting Debug Session at $(date) ===" | tee -a "$GST_LOG_FILE"
echo "GStreamer Log: $GST_LOG_FILE" | tee -a "$GST_LOG_FILE"
echo "" | tee -a "$GST_LOG_FILE"

export ROCKCHIP_RGA_LOG=1
export ROCKCHIP_RGA_LOG_LEVEL=3
echo "RGA debug logs enabled: ROCKCHIP_RGA_LOG=1, ROCKCHIP_RGA_LOG_LEVEL=3" | tee -a "$GST_LOG_FILE"

if [ -e /sys/kernel/debug/rkrga/debug ]; then
    echo "Enabling RGA debug nodes..." | tee -a "$GST_LOG_FILE"
    echo reg > /sys/kernel/debug/rkrga/debug 2>/dev/null || echo "Could not enable reg log"
    echo msg > /sys/kernel/debug/rkrga/debug 2>/dev/null || echo "Could not enable msg log"
    echo time > /sys/kernel/debug/rkrga/debug 2>/dev/null || echo "Could not enable time log"
    echo mm > /sys/kernel/debug/rkrga/debug 2>/dev/null || echo "Could not enable mm log"
fi

if [ -e /sys/module/rk_vcodec/parameters/mpp_dev_debug ]; then
    echo "Enabling VPU debug..." | tee -a "$GST_LOG_FILE"
    echo 0x100 > /sys/module/rk_vcodec/parameters/mpp_dev_debug 2>/dev/null || echo "Could not enable VPU debug"
fi

export GST_DEBUG=3
export GST_DEBUG_FILE="$GST_LOG_FILE"
export GST_DEBUG_NO_COLOR=1

echo "" | tee -a "$GST_LOG_FILE"
echo "=== Launching GStreamer Pipeline ===" | tee -a "$GST_LOG_FILE"
echo "" | tee -a "$GST_LOG_FILE"

cd /home/cat/memoark/gst-rknn-filter

gst-launch-1.0 -e \
    rtmpsrc location=rtmp://192.168.1.62/live/qingting ! \
    flvdemux name=demux \
    demux.video ! queue ! h264parse ! mppvideodec ! video/x-raw,format=NV12 ! \
    videocrop top=0 bottom=270 left=480 right=480 ! \
    video/x-raw,format=NV12,width=960,height=810 ! \
    rknnfilter model-path=/home/cat/memoark/gst-rknn-filter/model/RetinaFace_mobile320.rknn model-type=retinaface show-fps=true frame_skip=3 ! \
    mpph264enc bps=4000000 gop=60 profile=high rc-mode=cbr ! \
    h264parse ! queue ! \
    flvmux name=mux streamable=true ! \
    rtmpsink location=rtmp://192.168.1.62/live/infer \
    2>&1 | tee -a "$GST_LOG_FILE"

EXIT_CODE=${PIPESTATUS[0]}
echo "" | tee -a "$GST_LOG_FILE"
echo "=== GStreamer exited with code: $EXIT_CODE at $(date) ===" | tee -a "$GST_LOG_FILE"
