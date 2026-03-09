#!/bin/bash

# Video Pipeline Orchestrator (State-based)
# Responsibilities: Determines signal state, launches appropriate engine, and exits on state change.

readonly ENGINE_BIN="/home/alex/kvm_engine/kvm_engine"
readonly VIDEO_DEV="/dev/video0"

log_message() {
    echo "[$(date +'%Y-%m-%dT%H:%M:%S%z')] $1" >&2
}

check_signal() {
    v4l2-ctl -d "$VIDEO_DEV" --set-dv-bt-timings query > /dev/null 2>&1
    return $?
}

log_message "Starting stream orchestrator..."

if check_signal; then
    log_message "Signal detected. Starting kvm_engine."
    v4l2-ctl -d "$VIDEO_DEV" --set-fmt-video=width=1280,height=720,pixelformat=UYVY > /dev/null 2>&1
    
    # Use 'exec' to replace the bash process with kvm_engine.
    # When signal is lost, kvm_engine exits, closing the pipe and triggering a MediaMTX restart.
    exec "$ENGINE_BIN"
else
    log_message "No signal. Starting dummy black stream."
    
    # Start continuous dummy stream in the background
    ffmpeg -hide_banner -loglevel error \
        -f lavfi -i color=c=black:s=1280x720:r=1 \
        -c:v libx264 -preset ultrafast -tune zerolatency \
        -f h264 pipe:1 &
    DUMMY_PID=$!
    
    # Monitor for signal return
    while true; do
        sleep 2
        if check_signal; then
            log_message "Signal restored! Stopping dummy stream to trigger pipeline restart."
            kill -9 $DUMMY_PID 2>/dev/null
            exit 0
        fi
        
        # Watchdog: if ffmpeg dies unexpectedly, exit to trigger restart
        if ! kill -0 $DUMMY_PID 2>/dev/null; then
            log_message "Dummy stream died. Exiting to restart."
            exit 1
        fi
    done
fi