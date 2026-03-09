#!/bin/bash

# Industrial Startup Script for IP-KVM
# Responsibilities: Hardware Init, USB Gadget Setup, HID Server, and Video Streaming.
# Adheres to SRP: Orchestrates system components without internal logic.

set -e

PROJECT_ROOT="/home/alex/kvm_engine"
HID_SERVER_BIN="$PROJECT_ROOT/hid_server"
MEDIAMTX_DIR="/home/alex/mediamtx"

echo "=== IP-KVM Integrated System Startup ==="

# 1. Hardware Video Init
echo "[1/4] Initializing Video Bridge (TC358743)..."
sudo "$PROJECT_ROOT/scripts/init_kvm.sh" || echo "Warning: Video init returned non-zero. Proceeding..."

# 2. USB Gadget Init
echo "[2/4] Initializing USB HID Gadget..."
sudo "$PROJECT_ROOT/scripts/setup_usb_gadget.sh"

# 3. Compile/Build (Optional)
if [ "$1" == "--build" ]; then
    echo "[3/4] Rebuilding components..."
    
    # Build C++ Video Engine
    cd "$PROJECT_ROOT"
    g++ -O3 -mcpu=cortex-a72 -mtune=cortex-a72 -flto -Wall -Wextra \
        src/main.cpp src/CaptureDevice.cpp src/EncoderDevice.cpp -o kvm_engine
    
    # Build Go HID Server (Explicit output path to project root)
    cd "$PROJECT_ROOT/src/hid_server"
    go build -o "$HID_SERVER_BIN" main.go
    
    echo "Build complete."
else
    echo "[3/4] Skipping build."
fi

# 4. Starting Services
echo "[4/4] Launching Services..."

# Verify HID server exists before execution
if [ ! -f "$HID_SERVER_BIN" ]; then
    echo "Error: HID server binary not found at $HID_SERVER_BIN"
    echo "Please run with --build flag first."
    exit 1
fi

# Start HID Server in background
# Writing to /dev/hidg* requires root privileges
sudo "$HID_SERVER_BIN" &
HID_PID=$!

# Cleanup function to prevent double execution
cleanup() {
    echo "Shutting down HID server..."
    sudo kill $HID_PID 2>/dev/null || true
    trap - EXIT INT TERM
}

trap cleanup EXIT INT TERM

# Start MediaMTX
cd "$MEDIAMTX_DIR"
./mediamtx