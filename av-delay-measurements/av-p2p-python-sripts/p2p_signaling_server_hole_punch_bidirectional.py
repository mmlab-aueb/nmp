# Correcting syntax and re-saving files for bidirectional server and client

# Corrected Signaling Server Script
#signaling_server_code = """
import socket
import time

PORTS = {
    "client1_send": 10000,
    "client1_recv": 10001,
    "client2_send": 10002,
    "client2_recv": 10003
}

def start_signaling_server():
    # Signaling Server for Bidirectional Audio
    socks = {}
    addrs = {}

    for label, port in PORTS.items():
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.bind(("0.0.0.0", port))
        socks[label] = sock
        print(f"📡 [WAITING] {label} on port {port}")

    # Receive all 4 "hello" messages
    for label, sock in socks.items():
        data, addr = sock.recvfrom(1024)
        addrs[label] = addr
        print(f"✅ [CONNECTED] {label} from {addr}")

    # Send each client the peer’s public send/receive addresses
    client1_peer_info = f"{addrs['client2_recv'][0]}:{addrs['client2_recv'][1]}:{addrs['client2_send'][0]}:{addrs['client2_send'][1]}"
    client2_peer_info = f"{addrs['client1_recv'][0]}:{addrs['client1_recv'][1]}:{addrs['client1_send'][0]}:{addrs['client1_send'][1]}"

    socks['client1_send'].sendto(client1_peer_info.encode(), addrs['client1_send'])
    socks['client1_recv'].sendto(client1_peer_info.encode(), addrs['client1_recv'])

    socks['client2_send'].sendto(client2_peer_info.encode(), addrs['client2_send'])
    socks['client2_recv'].sendto(client2_peer_info.encode(), addrs['client2_recv'])

    print("🔁 [EXCHANGED] Sent peer info to both clients.")

    while True:
        for label in PORTS:
            socks[label].sendto(b"", addrs[label])
        print("📤 [KEEPALIVE] Sent to all sockets.")
        time.sleep(1)

start_signaling_server()

