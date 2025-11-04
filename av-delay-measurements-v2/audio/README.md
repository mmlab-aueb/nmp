# UDP Audio Streaming with ALSA (C++ / Linux)

This folder contains minimal C++ examples for **sending** and **receiving** raw PCM audio over **UDP** using the **ALSA** library on Ubuntu/Linux.  
Each app uses threads for audio I/O (ALSA) and networking.

> These examples stream **unencrypted raw audio** and are meant for research/experimentation on trusted networks.

---

## Directory Structure

```
audio/
├── udp_audio_sender_threaded.cpp
├── udp_audio_receiver_threaded.cpp
└── README.md
```

---

## Requirements

Install build tools & ALSA dev libraries:

```bash
sudo apt-get update
sudo apt-get install -y build-essential libasound2-dev alsa-utils
```

---

## Build

### Sender

```bash
g++ -std=c++17 -O2 udp_audio_sender_threaded.cpp \
  -o udp_audio_sender_threaded \
  -lasound -lpthread
```

### Receiver

```bash
g++ -std=c++17 -O2 udp_audio_receiver_threaded.cpp \
  -o udp_audio_receiver_threaded \
  -lasound -lpthread
```

---

## Run

### Receiver (listens on port 5000)

```bash
./udp_audio_receiver_threaded --port 5000
```

### Sender (send to receiver IP)

```bash
./udp_audio_sender_threaded --host 192.168.1.50 --port 5000
```

Replace `192.168.1.50` with the receiver’s IP.

---

## Options (if supported)

| Flag | Description |
|------|------------|
| `--device hw:0,0` | ALSA device |
| `--rate 48000` | Sample rate |
| `--channels 1` | Mono (2 = stereo) |
| `--frames 1024` | ALSA buffer frames |
| `--host <IP>` | Destination IP (sender) |
| `--port <N>` | UDP port |

---

## Useful Commands

List ALSA devices:

```bash
arecord -l
aplay -l
```

Allow UDP port:

```bash
sudo ufw allow 5000/udp
```

---

## Troubleshooting

| Issue | Solution |
|------|---------|
Choppy audio | Increase `--frames` |
XRUN errors | Larger buffer or lower sample rate |
Silence | Ensure same rate/channels on both sides |
NAT issues | Port forward or run on LAN |

---

## Security Note

Raw UDP audio — no encryption/auth.  
Use VPN/SSH tunnel for secure use.

---

## License

MIT
