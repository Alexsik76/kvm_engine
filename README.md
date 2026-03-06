# Hardware IP-KVM Engine

Low-latency hardware KVM over IP solution based on Raspberry Pi 4, TC358743 HDMI-CSI2 bridge, and V4L2 M2M hardware H.264 encoding.

## System Architecture

* **Capture:** Toshiba TC358743 (720p60 UYVY via CSI-2)
* **Encoding:** BCM2711 Hardware H.264 Encoder (`/dev/video11`) via DMA-BUF (Zero-Copy)
* **Streaming:** MediaMTX (WebRTC) + FFmpeg (TCP transport)

## Installation & Setup

These instructions assume a fresh Raspberry Pi OS (Debian) installation.

### 1. System Dependencies

Install required packages for V4L2 control, compilation, and stream routing:

```bash
sudo apt update
sudo apt install -y v4l-utils ffmpeg build-essential git wget tar
```

### 2. TC358743 EDID Driver

The hardware initialization script expects the EDID driver repository to be located at `~/TC358743-Driver`.

```bash
cd ~
git clone [https://github.com/SomeUser/TC358743-Driver.git](https://github.com/SomeUser/TC358743-Driver.git) TC358743-Driver
```
*(Note: Ensure the `720p60edid` file is present in that directory).*

### 3. MediaMTX Server

Download and extract the MediaMTX binary to `~/mediamtx`.

```bash
cd ~
mkdir -p mediamtx
wget [https://github.com/bluenviron/mediamtx/releases/download/v1.9.3/mediamtx_v1.9.3_linux_arm64.tar.gz](https://github.com/bluenviron/mediamtx/releases/download/v1.9.3/mediamtx_v1.9.3_linux_arm64.tar.gz) -O /tmp/mediamtx.tar.gz
tar -xzf /tmp/mediamtx.tar.gz -C ~/mediamtx
rm /tmp/mediamtx.tar.gz
```

### 4. Clone and Configure This Repository

Clone this KVM engine repository and link the minimal MediaMTX configuration file.

```bash
cd ~
git clone [https://github.com/YourUsername/kvm_engine.git](https://github.com/YourUsername/kvm_engine.git) kvm_engine
cd kvm_engine

ln -sf $(pwd)/config/mediamtx.yml ~/mediamtx/mediamtx.yml
```

## Running the KVM

For the first run (or after modifying C++ source code), use the `--build` flag to compile the engine:

```bash
~/kvm_engine/scripts/start_kvm.sh --build
```

For normal startup, simply run:

```bash
~/kvm_engine/scripts/start_kvm.sh
```

The WebRTC stream will be available at `http://<RPI_IP>:8889/kvm`.
