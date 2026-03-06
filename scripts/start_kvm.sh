#!/bin/bash

set -e

echo "=== KVM System Startup ==="

echo "[1/3] Initializing hardware..."
if [ -x "/home/alex/kvm_engine/scripts/init_kvm.sh" ]; then
    /home/alex/kvm_engine/scripts/init_kvm.sh
else
    echo "Error: init_kvm.sh not found or not executable."
    exit 1
fi

if [ "$1" == "--build" ]; then
    echo "[2/3] Compiling C++ source code..."
    cd /home/alex/kvm_engine || exit 1
    g++ -O3 -mcpu=cortex-a72 -mtune=cortex-a72 -flto -Wall -Wextra \
        src/main.cpp src/CaptureDevice.cpp src/EncoderDevice.cpp -o kvm_engine
    echo "Compilation successful."
else
    echo "[2/3] Skipping compilation (use --build to compile)."
fi

echo "[3/3] Starting MediaMTX server..."
cd /home/alex/mediamtx || exit 1
./mediamtx