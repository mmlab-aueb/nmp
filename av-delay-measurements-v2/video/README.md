## UDP Video Streaming with ALSA (C++ / Linux)

This folder contains minimal C++ examples for **sending** and **receiving** jpeg frames over **UDP** using the **openCV** library on Ubuntu/Linux.
Both applications should run on the same LAN with known IP's.
Each app uses threads for capturing frames and networking.

---

### Directory Structure

```
video/
├── open-camera-receive-udp-threaded.cpp
├── open-camera-send-udp-threaded.cpp
└── README.md
```

---

### Requirements

Install build tools & ALSA dev libraries:

```bash
sudo apt-get update
sudo apt-get install -y build-essential pkg-config libopencv-dev
# optional: tools
sudo apt-get install -y v4l-utils ffmpeg
```

---

### Build

#### Sender

```bash
g++ -std=c++11 -pthread -o open-camera-send-udp-threaded open-camera-send-udp-threaded.cpp \
     $(pkg-config --cflags --libs opencv4)
```

#### Receiver

```bash
g++ -std=c++11 -pthread -o open-camera-receive-udp-threaded-new \
        open-camera-receive-udp-threaded-new.cpp $(pkg-config --cflags --libs opencv4)
```

---

#### Run

#### Receiver must run on a Ubuntu machine with known IP ( example: 192.168.1.50 listens on port 5000)

```bash
./udp_audio_receiver_threaded
```

#### Sender must run on a Ubuntu machine with known IP (send to receiver_IP port)

```bash
./udp_audio_sender_threaded  192.168.1.50  5000
```

Replace `192.168.1.50` with the receiver’s IP.

---

### Troubleshooting

| Issue | Solution |
|------|---------|
Choppy audio | Increase BUFFER_CAPACITY  |
XRUN errors | Larger buffer |
Silence | Ensure same rate/channels on both sides |




