#!/bin/bash

cd /home/cat/memoark/gst-rknn-filter

chmod +x monitor_hw.sh test_debug.sh

echo "========================================"
echo "RK3576 Debug Session Starting..."
echo "========================================"
echo ""

echo "[1/3] Starting hardware monitor..."
./monitor_hw.sh &
MONITOR_PID=$!
echo "Monitor PID: $MONITOR_PID"
echo ""

sleep 2

echo "[2/3] Starting GStreamer debug session..."
echo ""
./test_debug.sh &
GST_PID=$!
echo "GStreamer PID: $GST_PID"
echo ""

echo "[3/3] Monitoring for 15 minutes..."
echo "Press Ctrl+C to stop early"
echo ""

trap 'echo "Stopping debug session..."; kill $GST_PID $MONITOR_PID 2>/dev/null; exit 0' SIGINT SIGTERM

START_TIME=$(date +%s)
DURATION=$((15 * 60))

while true; do
    CURRENT_TIME=$(date +%s)
    ELAPSED=$((CURRENT_TIME - START_TIME))
    REMAINING=$((DURATION - ELAPSED))
    
    if [ $REMAINING -le 0 ]; then
        echo "15 minutes elapsed. Stopping..."
        break
    fi
    
    echo -ne "Elapsed: ${ELAPSED}s / Remaining: ${REMAINING}s\r"
    sleep 1
done

echo ""
echo "Stopping processes..."
kill $GST_PID $MONITOR_PID 2>/dev/null
sleep 2
kill -9 $GST_PID $MONITOR_PID 2>/dev/null

echo ""
echo "========================================"
echo "Debug session complete!"
echo "Logs saved in: /home/cat/memoark/gst-rknn-filter/debug_logs/"
echo "========================================"
