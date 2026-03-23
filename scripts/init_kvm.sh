#!/bin/bash
# IP-KVM Hardware Initialization Script

# Кольори для логів
GREEN='\033[0;32m'
NC='\033[0m' # No Color
DATE_FMT=$(date "+%Y/%m/%d %H:%M:%S")

echo -e "${DATE_FMT} INF Configuring TC358743 for 720p60 UYVY..."

cd /home/alex/TC358743-Driver || exit 1
EDID_FILE="force_720p.edid" 

v4l2-ctl -d /dev/video0 --set-edid pad=0,file=${EDID_FILE},format=raw

echo -e "${DATE_FMT} INF Waiting for stable video signal..."
while true; do
    CURRENT_W=$(v4l2-ctl -d /dev/video0 --query-dv-timings | awk '/Active width/ {print $3}' || echo 0)
    
    if [ "${CURRENT_W:-0}" -gt 0 ]; then
        break
    fi
    sleep 1
done

v4l2-ctl -d /dev/video0 --set-dv-bt-timings query || true

WIDTH=$(v4l2-ctl -d /dev/video0 --query-dv-timings | awk '/Active width/ {print $3}')
HEIGHT=$(v4l2-ctl -d /dev/video0 --query-dv-timings | awk '/Active height/ {print $3}')
v4l2-ctl -d /dev/video0 --set-fmt-video=width=${WIDTH:-1280},height=${HEIGHT:-720},pixelformat=UYVY

# Вивід у стилі MediaMTX з зеленим INF
DATE_FMT=$(date "+%Y/%m/%d %H:%M:%S")
echo -e "${DATE_FMT} ${GREEN}INF Hardware configuration complete. Current status: ${WIDTH}x${HEIGHT}${NC}"