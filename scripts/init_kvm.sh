#!/bin/bash
# IP-KVM Hardware Initialization Script

echo "Configuring TC358743 for 720p60 UYVY..."

# Navigate to the working directory to ensure the EDID file is found
cd /home/alex/TC358743-Driver || exit 1

# 1. Load the 720p EDID profile
v4l2-ctl -d /dev/video0 --set-edid=file=720p60edid

# Wait for the source PC to negotiate the new virtual monitor
sleep 2

# 2. Apply detected timings
v4l2-ctl -d /dev/video0 --set-dv-bt-timings query || true

# 3. Set the pixel format and resolution for the capture pipeline
v4l2-ctl -d /dev/video0 --set-fmt-video=width=1280,height=720,pixelformat=UYVY

echo "Hardware configuration complete. Current status:"
v4l2-ctl -d /dev/video0 --query-dv-timings || true
