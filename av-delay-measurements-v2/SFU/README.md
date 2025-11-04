### A simple custom SFU implementation for simple UDP packet relay

This 3 x 3 relay server must run in a LAN with known IP's. 

It receives UDP packets in 3 UDP ports (given as arguments) and sends these packets to 3 given IP's and corresponding ports. 

### Build

```bash
g++ -O2 -std=c++17 3-to-3-udp-relay-server.cpp -o 3-to-3-udp-relay-server
```

### Usage example:
```bash
   ./3-to-3-udp-relay-server \
     --p1 10000 --p2 20000 --p3 30000 \
     --1to2 10.0.0.2:11000 --1to3 10.0.0.3:12000 \
     --2to1 10.0.0.1:10100 --2to3 10.0.0.3:12001 \
     --3to1 10.0.0.1:10101 --3to2 10.0.0.2:11001
```
