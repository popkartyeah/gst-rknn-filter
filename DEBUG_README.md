# RK3576 调试说明

## 调试脚本说明

### 1. start_debug.sh - 一键启动调试
主启动脚本，同时启动硬件监控和GStreamer调试，运行2分钟。

```bash
cd /home/cat/memoark/gst-rknn-filter
./start_debug.sh
```

### 2. monitor_hw.sh - 硬件监控脚本
定期收集NPU、RGA、内存等硬件状态，每10秒记录一次。

监控内容：
- NPU: 负载、电源状态、电压、频率
- RGA: 负载、内存会话、请求管理器、硬件信息
- 内存: meminfo、DMA buffer信息
- CPU温度、进程列表

### 3. test_debug.sh - GStreamer调试脚本
增强版test.sh，开启：
- RGA调试日志
- VPU调试打印
- GStreamer调试级别3

## 日志文件位置

所有日志保存在: `/home/cat/memoark/gst-rknn-filter/debug_logs/`

- `hw_monitor_YYYYMMDD_HHMMSS.log` - 硬件监控日志
- `gst_debug_YYYYMMDD_HHMMSS.log` - GStreamer调试日志

## 手动调试命令（使用rk3576-debug SKILL）

如果需要单独调试，可以使用以下命令：

### NPU调试
```bash
# 查看NPU负载
cat /sys/kernel/debug/rknpu/load

# 查看NPU电源状态
cat /sys/kernel/debug/rknpu/power

# 查看NPU电压
cat /sys/kernel/debug/rknpu/volt

# 查看NPU频率
cat /sys/class/devfreq/fdab0000.npu/cur_freq
```

### RGA调试
```bash
# 开启RGA日志
export ROCKCHIP_RGA_LOG=1
export ROCKCHIP_RGA_LOG_LEVEL=3

# 查看RGA负载
cat /sys/kernel/debug/rkrga/load

# 查看RGA内存会话
cat /sys/kernel/debug/rkrga/mm_session

# 查看RGA请求管理器
cat /sys/kernel/debug/rkrga/request_manager

# 开启RGA debug节点
echo reg > /sys/kernel/debug/rkrga/debug
echo msg > /sys/kernel/debug/rkrga/debug
echo time > /sys/kernel/debug/rkrga/debug
echo mm > /sys/kernel/debug/rkrga/debug
```

### 内存调试
```bash
# 查看内存信息
cat /proc/meminfo

# 查看DMA buffer
cat /sys/kernel/debug/dma_buf/bufinfo
```
