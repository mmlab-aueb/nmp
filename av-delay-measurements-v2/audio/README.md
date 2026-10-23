## UDP Audio Streaming with ALSA (C++ / Linux)

This folder contains minimal C++ examples for **sending** and **receiving** raw PCM audio over **UDP** using the **ALSA** library on Ubuntu/Linux.

Both applications should run on the same LAN with known IP's.

They can run both in peer-to-peer mode and as Client - SFU using the SFU application.

Each app uses threads for audio I/O (ALSA) and networking.

---

### Directory Structure

```
audio/
├── udp_audio_sender_threaded.cpp
├── udp_audio_receiver_threaded.cpp
├── udp_p2p_audio_client_threaded.cpp
└── README.md

```

---

### Requirements

Install build tools & ALSA dev libraries:

```bash
sudo apt-get update
sudo apt-get install -y build-essential libasound2-dev alsa-utils
```

---

### Build

#### Sender

```bash
g++ -std=c++17 -O2 udp_audio_sender_threaded.cpp \
  -o udp_audio_sender_threaded \
  -lasound -lpthread
```

#### Receiver

```bash
g++ -std=c++17 -O2 udp_audio_receiver_threaded.cpp \
  -o udp_audio_receiver_threaded \
  -lasound -lpthread
```

---

### Run in LAN conditions

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
### Run with two endpoints behind NATs 
#### Build the client application
```bash
g++ -std=c++17 -pthread udp_p2p_audio_client_threaded.cpp -o udp_p2p_audio_client_threaded_cpp -lasound
```

### Peer to Peer Mode
####  1. Run the server_for_p2p_connection (in the SFU folder) in a Ubuntu machine with public IP
```bash
./server_for_p2p_connection 
```
####  2. Run the client application on each of the two Ubuntu machines that sit behind NATs.
####  Peer 1
```bash
./udp_p2p_audio_client_threaded_cpp  ServerIP client1 10000 10001 
```
####  Peer 2
```bash
./udp_p2p_audio_client_threaded_cpp  ServerIP client2 20000 10001 
```



### Client SFU Mode
####  1. Run the server_for_p2p_connection (in the SFU folder) in a Ubuntu machine with public IP
```bash
./udp_relay_server 
```
####  2. Run the client application on each of the two Ubuntu machines that sit behind NATs.
####  Client 1
```bash
./udp_p2p_audio_client_threaded_cpp  ServerIP client1 10000 10001 
```
####  Client 2
```bash
./udp_p2p_audio_client_threaded_cpp  ServerIP client2 20000 10001 
```

---

### Troubleshooting

| Issue | Solution |
|------|---------|
Choppy audio | Increase BUFFER_CAPACITY  |
XRUN errors | Larger buffer |
Silence | Ensure same rate/channels on both sides |



