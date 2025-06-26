## TENEMP WebRTC Video Streaming

A simple WebRTC-based video streaming demo using a libwebsockets signaling server and GStreamer clients. It lets two clients (a **sender** and a **receiver**) exchange video streams locally.

---

### Components

1. **signaling_server**
   - Listens on a specified port for WebSocket connections.
   - Tracks connected clients and assigns the first as **sender** and the second as **receiver**.
   - Relays SDP offers/answers and ICE candidates between clients.

2. **unified_client**
   - Connects to the signaling server over WebSocket.
   - Receives its **role** (`sender`/`receiver`) on connect.
   - Loads the appropriate GStreamer pipeline from:
     - `client/pipelines1.txt` for **sender** (captures real camera)
     - `client/pipelines2.txt` for **receiver** (plays test video locally)
   - Uses GStreamer WebRTC elements to negotiate and render media.
   - Accepts `-a <address>` and `-p <port>` command-line flags:
     ```
     ./unified_client -a 192.168.1.42 -p 9001
     ```

3. **client_utils** (in `client_utils.h/.c`)
   - Helper functions to parse command-line args, initialize libwebsockets, and set up GStreamer.

---

## Build Instructions

From the project root:

```
    mkdir build
    cd build
    cmake ..
    make
```

This produces the binaries:
- `signaling_server`
- `unified_client`

---

## Run Instructions

1. **Start the signaling server** (default port 9000 or specify):

```
    ./signaling_server
    ./signaling_server 9001
```

2. **Open two terminals**, and in each:

```
    ./unified_client -p 9000
    ./unified_client -a <server_ip> -p <port>
```

3. The first terminal window shows your camera (sender).  
4. The second shows a test video (receiver).

> The server keeps a connection count: client #1 is `sender`, #2 is `receiver`.

---

## Custom Usage

- By default, **receiver** uses a test pattern because running two camera sources on the same machine may conflict.
- To stream real camera across machines:
  1. Copy `pipelines1.txt` contents into `pipelines2.txt` on the second machine (or point both to the same file).
  2. Ensure each machine has a camera device (e.g. `/dev/video0`).
  3. Run the client on each machine pointing to the same signaling server address.

---

## File Layout

```
/ # project root
├─ build/     
├─ client/
│  ├─ unified_client.h
│  ├─ unified_client.c
│  ├─ client_utils.h
│  ├─ client_utils.c
│  ├─ pipelines1.txt
│  └─ pipelines2.txt
├─ signaling_server/
│  ├─ signaling_server.h
│  └─ signaling_server.c
└─ README.md
```

---

## Notes

- The app is currently tuned for local testing.
- All command-line flags (port, address, path, SSL, pipeline file/index) are parsed via **argtable3** and printed at startup.
- `client_utils` abstracts initialization of WebSocket/GStreamer, keeping the client code clean.