#!/bin/bash

LOG_DIR="/home/cat/memoark/gst-rknn-filter/debug_logs"
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
LOG_FILE="$LOG_DIR/hw_monitor_$TIMESTAMP.log"

mkdir -p "$LOG_DIR"

echo "=== Hardware Monitor Started at $(date) ===" | tee -a "$LOG_FILE"
echo "Log file: $LOG_FILE" | tee -a "$LOG_FILE"
echo "" | tee -a "$LOG_FILE"

collect_info() {
    local current_time=$(date +"%Y-%m-%d %H:%M:%S")
    echo "--- [$current_time] ---" >> "$LOG_FILE"
    echo "" >> "$LOG_FILE"
    
    echo "=== NPU Information ===" >> "$LOG_FILE"
    if [ -e /sys/kernel/debug/rknpu/load ]; then
        echo "NPU Load:" >> "$LOG_FILE"
        cat /sys/kernel/debug/rknpu/load >> "$LOG_FILE" 2>/dev/null || echo "N/A" >> "$LOG_FILE"
    fi
    if [ -e /sys/kernel/debug/rknpu/power ]; then
        echo "NPU Power:" >> "$LOG_FILE"
        cat /sys/kernel/debug/rknpu/power >> "$LOG_FILE" 2>/dev/null || echo "N/A" >> "$LOG_FILE"
    fi
    if [ -e /sys/kernel/debug/rknpu/volt ]; then
        echo "NPU Voltage:" >> "$LOG_FILE"
        cat /sys/kernel/debug/rknpu/volt >> "$LOG_FILE" 2>/dev/null || echo "N/A" >> "$LOG_FILE"
    fi
    if [ -e /sys/class/devfreq/fdab0000.npu/cur_freq ]; then
        echo "NPU Frequency:" >> "$LOG_FILE"
        cat /sys/class/devfreq/fdab0000.npu/cur_freq >> "$LOG_FILE" 2>/dev/null || echo "N/A" >> "$LOG_FILE"
    fi
    echo "" >> "$LOG_FILE"
    
    echo "=== RGA Information ===" >> "$LOG_FILE"
    if [ -e /sys/kernel/debug/rkrga/load ]; then
        echo "RGA Load:" >> "$LOG_FILE"
        cat /sys/kernel/debug/rkrga/load >> "$LOG_FILE" 2>/dev/null || echo "N/A" >> "$LOG_FILE"
    fi
    if [ -e /sys/kernel/debug/rkrga/mm_session ]; then
        echo "RGA Memory Sessions:" >> "$LOG_FILE"
        cat /sys/kernel/debug/rkrga/mm_session >> "$LOG_FILE" 2>/dev/null || echo "N/A" >> "$LOG_FILE"
    fi
    if [ -e /sys/kernel/debug/rkrga/request_manager ]; then
        echo "RGA Request Manager:" >> "$LOG_FILE"
        cat /sys/kernel/debug/rkrga/request_manager >> "$LOG_FILE" 2>/dev/null || echo "N/A" >> "$LOG_FILE"
    fi
    if [ -e /sys/kernel/debug/rkrga/hardware ]; then
        echo "RGA Hardware:" >> "$LOG_FILE"
        cat /sys/kernel/debug/rkrga/hardware >> "$LOG_FILE" 2>/dev/null || echo "N/A" >> "$LOG_FILE"
    fi
    echo "" >> "$LOG_FILE"
    
    echo "=== Memory Information ===" >> "$LOG_FILE"
    cat /proc/meminfo | head -20 >> "$LOG_FILE"
    if [ -e /sys/kernel/debug/dma_buf/bufinfo ]; then
        echo "DMA Buffer Info:" >> "$LOG_FILE"
        cat /sys/kernel/debug/dma_buf/bufinfo >> "$LOG_FILE" 2>/dev/null || echo "N/A" >> "$LOG_FILE"
    fi
    echo "" >> "$LOG_FILE"
    
    echo "=== CPU Temperature ===" >> "$LOG_FILE"
    if [ -e /sys/class/thermal/thermal_zone0/temp ]; then
        cat /sys/class/thermal/thermal_zone0/temp >> "$LOG_FILE" 2>/dev/null || echo "N/A" >> "$LOG_FILE"
    fi
    echo "" >> "$LOG_FILE"
    
    echo "=== Process List (GStreamer related) ===" >> "$LOG_FILE"
    ps aux | grep -E "gst-launch|rknn|rga" | grep -v grep >> "$LOG_FILE" 2>/dev/null || echo "No matching processes" >> "$LOG_FILE"
    echo "" >> "$LOG_FILE"
    
    echo "========================================" >> "$LOG_FILE"
    echo "" >> "$LOG_FILE"
}

trap 'echo "=== Monitor stopped at $(date) ===" | tee -a "$LOG_FILE"; exit 0' SIGINT SIGTERM

echo "Collecting initial info..."
collect_info

echo "Monitoring started. Collecting info every 10 seconds..."
while true; do
    sleep 10
    collect_info
done
