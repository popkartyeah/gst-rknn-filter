#!/bin/bash

LOG_DIR="/home/cat/memoark/gst-rknn-filter/debug_logs"
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
MEM_LOG="$LOG_DIR/mem_analysis_$TIMESTAMP.log"

mkdir -p "$LOG_DIR"

echo "=== Memory Analysis Started at $(date) ===" | tee -a "$MEM_LOG"
echo "Log file: $MEM_LOG" | tee -a "$MEM_LOG"
echo "" | tee -a "$MEM_LOG"

echo "=== System Memory Before Test ===" | tee -a "$MEM_LOG"
cat /proc/meminfo | head -10 >> "$MEM_LOG"
echo "" >> "$MEM_LOG"

echo "=== DMA Buffer Info Before Test ===" | tee -a "$MEM_LOG"
if [ -e /sys/kernel/debug/dma_buf/bufinfo ]; then
    cat /sys/kernel/debug/dma_buf/bufinfo >> "$MEM_LOG" 2>/dev/null || echo "N/A" >> "$MEM_LOG"
else
    echo "DMA buf info not available" >> "$MEM_LOG"
fi
echo "" >> "$MEM_LOG"

echo "=== Process File Descriptors Before ===" | tee -a "$MEM_LOG"
ps aux | grep gst-launch | grep -v grep | awk '{print $2}' | while read pid; do
    echo "Process $pid:" >> "$MEM_LOG"
    ls -la /proc/$pid/fd 2>/dev/null | wc -l >> "$MEM_LOG"
done
echo "" >> "$MEM_LOG"

START_TIME=$(date +%s)
DURATION=120

echo "=== Running test for $DURATION seconds ===" | tee -a "$MEM_LOG"

cd /home/cat/memoark/gst-rknn-filter

export ROCKCHIP_RGA_LOG=1
export ROCKCHIP_RGA_LOG_LEVEL=0
export GST_DEBUG=3
export GST_DEBUG_FILE="$LOG_DIR/gst_mem_$TIMESTAMP.log"
export GST_DEBUG_NO_COLOR=1

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
    2>&1 | head -1000 &
    
GST_PID=$!

sleep $DURATION

kill $GST_PID 2>/dev/null
sleep 2
kill -9 $GST_PID 2>/dev/null

END_TIME=$(date +%s)
ELAPSED=$((END_TIME - START_TIME))

echo "" | tee -a "$MEM_LOG"
echo "=== System Memory After Test ($ELAPSED seconds) ===" | tee -a "$MEM_LOG"
cat /proc/meminfo | head -10 >> "$MEM_LOG"
echo "" >> "$MEM_LOG"

echo "=== DMA Buffer Info After Test ===" | tee -a "$MEM_LOG"
if [ -e /sys/kernel/debug/dma_buf/bufinfo ]; then
    cat /sys/kernel/debug/dma_buf/bufinfo >> "$MEM_LOG" 2>/dev/null || echo "N/A" >> "$MEM_LOG"
else
    echo "DMA buf info not available" >> "$MEM_LOG"
fi
echo "" >> "$MEM_LOG"

echo "=== File Descriptors After ===" | tee -a "$MEM_LOG"
ps aux | grep gst-launch | grep -v grep | awk '{print $2}' | while read pid; do
    echo "Process $pid:" >> "$MEM_LOG"
    ls -la /proc/$pid/fd 2>/dev/null | wc -l >> "$MEM_LOG"
done
echo "" >> "$MEM_LOG"

echo "=== Memory Analysis Complete ===" | tee -a "$MEM_LOG"
echo "Full log saved to: $MEM_LOG" | tee -a "$MEM_LOG"