#python3 p2p_signaling_client_hole_punch_v2.py --server-ip 195.251.234.16 --local-port 5000 --peer-number 1
#python3 p2p_signaling_client_hole_punch_v2.py --server-ip 195.251.234.16 --local-port 5001 --peer-number 2

import socket
import time
import argparse
import subprocess
import threading

EMPTY_PACKET_COUNT = 10


def start_p2p_client(server_ip, local_port, peer_number):
    """Client that uses signaling server to establish direct P2P message exchange and streams audio from client 1."""
    server_port = 10000 if peer_number == 1 else 10001

    # Open socket for signaling and communication
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("0.0.0.0", local_port))

    # Contact signaling server
    sock.sendto(b"Hello", (server_ip, server_port))
    print(f"📨 [SIGNALING] Sent hello to server {server_ip}:{server_port}")

    # Receive peer's public endpoint
    peer_data, _ = sock.recvfrom(1024)
    peer_ip, peer_port = peer_data.decode().strip().split(":")
    peer_port = int(peer_port)
    print(f"🌍 [PEER INFO] Peer at {peer_ip}:{peer_port}")

    # NAT hole punching
    print("🔓 [PUNCHING] Sending empty packets to peer to open NAT")
    for i in range(EMPTY_PACKET_COUNT):
        sock.sendto(b"", (peer_ip, peer_port))
        print(f"📤 [PUNCH] Sent empty packet #{i+1} to {peer_ip}:{peer_port}")
        time.sleep(1)

    print("✅ [READY] NAT hole punching complete.")
    sock.close()
    if peer_number == 1:
        print("🎵 [STREAMING] Client 1 sending audio to Client 2 via multiudpsink from port 5000")
        send_gst_command = [
            "gst-launch-1.0", "-v",
            "pulsesrc", "latency-time=1000", "buffer-time=2000", "!",
            "audioconvert", "!", "audioresample", "!", "rtpL16pay", "!",
            "application/x-rtp,media=audio,clock-rate=48000,encoding-name=L16,channels=1", "!",
            "multiudpsink", f"clients={peer_ip}:{peer_port}", "bind-port=5000"

        ]
        subprocess.Popen(" ".join(send_gst_command), shell=True)
    else:
        print(f"🎧 [LISTENING] Client 2 receiving audio on port {local_port}")
        recv_command = [
            "gst-launch-1.0", "-v", "udpsrc", f"port={local_port}",
            "caps=application/x-rtp,media=audio,clock-rate=48000,encoding-name=L16,channels=1", "!",
            "rtpL16depay", "!", "audioconvert", "!", "audioresample", "!",
            "queue", "max-size-time=1000000", "!", "pulsesink", "latency-time=1000", "buffer-time=2000", "sync=false"
        ]
        subprocess.Popen(" ".join(recv_command), shell=True)

    # Also start receiving text messages (optional)
    def receive_messages():
        while True:
            data, addr = sock.recvfrom(4096)
            print(f"📥 [RECEIVED from {addr}] {data.decode(errors='ignore')}")

    threading.Thread(target=receive_messages, daemon=True).start()
    while True:
        time.sleep(1)


parser = argparse.ArgumentParser(description="P2P UDP Audio Client")
parser.add_argument("--server-ip", required=True, help="Signaling server IP")
parser.add_argument("--local-port", type=int, required=True, help="Local port to bind for receiving")
parser.add_argument("--peer-number", type=int, choices=[1, 2], required=True, help="Are you client 1 or 2")
args = parser.parse_args()

start_p2p_client(args.server_ip, args.local_port, args.peer_number)
