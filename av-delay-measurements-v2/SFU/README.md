### A rendezvous server and a simple custom SFU implementation for simple UDP packet relay

- server_for_p2p_connection.c:  It operates as a rendezvous server and must run in a Ubuntu machine with public IP. Two client applications behind NATs can exchange public IPs and ports so to establish bidirectional audio or video streaming.
- Build
```bash
gcc server_for_p2p_connection.c -o server_for_p2p_connection
```
- and udp_relay_server.c :  It operates both as a rendezvous server and as a UDP relay server. It must run in a Ubuntu machine with public IP. Two client applications behind NATs can establish bidirectional audio or video streaming via the server.
- Build
```bash
gcc udp_relay_server.c -o udp_relay_server
```



