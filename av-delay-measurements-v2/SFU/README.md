### A simple custom SFU implementation for simple UDP packet relay

- server_for_p2p_connection.c
--  It operates as a rendezvous server and must run in a Ubuntu machine with public IP. Two client applications behind NATs can exchange public IPs and ports so to establish bidirectional audio or video streaming.

- and udp_relay_server.c
It operates both as a rendezvous server and as a UDP relay server. It must run in a Ubuntu machine with public IP. Two client applications behind NATs can establish bidirectional audio or video streaming via the server.



### Build

```bash
g++ -O2 -std=c++17 3-to-3-udp-relay-server.cpp -o 3-to-3-udp-relay-server
```


