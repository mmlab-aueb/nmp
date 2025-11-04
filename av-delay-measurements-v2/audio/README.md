### C++ code for audio streaming using ALSA library in Ubuntu Linux

#### Compile
''g++ -std=c++17 udp_audio_sender_threaded.cpp -o udp_audio_sender_threaded -lasound -lpthread''


# UDP Audio Streaming with ALSA (C++ / Linux)

This folder contains minimal C++ examples for **sending** and **receiving** raw PCM audio over **UDP** using the **ALSA** library on Ubuntu/Linux.  
Each app uses threads for audio I/O (ALSA) and networking.

> These examples stream **unencrypted raw audio** for experimentation on trusted networks.

---

## Directory


