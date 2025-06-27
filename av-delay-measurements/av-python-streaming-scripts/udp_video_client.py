#python3 udp_audio_client.py --server-ip 195.251.234.16 --client-number 1
#python3 udp_audio_client.py --server-ip 195.251.234.16 --client-number 2
import socket
import time
import subprocess
import argparse
import signal

EMPTY_PACKET_COUNT = 10  # Number of empty packets to maintain NAT traversal

def start_client(server_ip, client_number):
    """ Client Mode: Sends RTP audio to the server and receives forwarded RTP from the other client. """
    
    # Set different ports for Client1 and Client2
    if client_number == 1:
        PORT_SEND = 10000  # Client1 → Server
        PORT_RECEIVE = 20000  # Server → Client1
    else:
        PORT_SEND = 10001  # Client2 → Server
        PORT_RECEIVE = 20001  # Server → Client2

    client_send = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    client_receive = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)


    # Bind to a local port for receiving audio
    #client_receive.bind(("0.0.0.0", PORT_RECEIVE))

    #print(f"🔗 [BIND] Client {client_number} bound to local receive port {PORT_RECEIVE}")

    # Send initial "Hello Server" to punch NAT hole
    client_send.sendto(b"Hello Server", (server_ip, PORT_SEND))
    print(f"📤 [SENT] Hello packet to {server_ip} on port {PORT_SEND}")


    # Send initial "Hello Server" to punch NAT hole
    client_receive.sendto(b"Hello Server", (server_ip, PORT_RECEIVE))
    print(f"📤 [SENT] Hello packet to {server_ip} on port {PORT_RECEIVE}")


    # Receive public mapping from the server
    send_data, _ = client_send.recvfrom(1024)
    send_response = send_data.decode().strip()
    print(f"🌍 [PUBLIC INFO] Send Port Mapping: {send_response}")

    public_send_ip, public_send_port = send_response.split(":")
    public_send_port = int(public_send_port)

    print(f"📩 [CONFIRMED] NAT punched for sending on {public_send_port}")


    # Receive public mapping from the server
    recv_data, _ = client_receive.recvfrom(1024)
    recv_response = recv_data.decode().strip()
    print(f"🌍 [PUBLIC INFO] Send Port Mapping: {recv_response}")

    public_rcv_ip, public_rcv_port = recv_response.split(":")
    public_rcv_port = int(public_rcv_port)

    print(f"📩 [CONFIRMED] NAT punched for sending on {public_rcv_port}")

    # Print the local address and port assigned by the OS
    local_rcv_ip, local_rcv_port = client_receive.getsockname()
    local_send_ip, local_send_port = client_send.getsockname()
    # Wait to receive empty packets from the server to confirm NAT hole
    print("🚀 [WAITING] Receiving empty packets from the server...")
    empty_packet_count = 0

    '''while empty_packet_count < EMPTY_PACKET_COUNT:
        try:
            data_send, addr_send = client_send.recvfrom(200000)  # Read UDP packet
            data_rcv, addr_rcv = client_receive.recvfrom(200000)  # Read UDP packet
            if data_rcv == b"":  # Check if the packet is truly empty
                empty_packet_count += 1
                print(f"📩 [RECEIVED] Empty UDP packet #{empty_packet_count} from {addr_rcv}")
            else:
                print(f"📩 [RECEIVED] Non-empty UDP packet from {addr_rcv}, size: {len(data_rcv)} bytes")
            if data_send == b"":  # Check if the packet is truly empty
                print(f"📩 [RECEIVED] Empty UDP packet #{empty_packet_count} from {addr_send}")
            else:
                print(f"📩 [RECEIVED] Non-empty UDP packet from {addr_send}, size: {len(data_send)} bytes")
            
        except socket.timeout:
            print("⚠️ [TIMEOUT] No packets received yet, retrying...")
    '''
    if client_number == 1:
        for _ in range(EMPTY_PACKET_COUNT):
            client_send.sendto(b"", (server_ip, 10000))
            print(f"📤 [SENT] Empty UDP packet to {10000}")
            client_receive.sendto(b"", (server_ip, 20000))
            print(f"📤 [SENT] Empty UDP packet to {20000}")
            time.sleep(1)
               
    else:           
        for _ in range(EMPTY_PACKET_COUNT):
            client_send.sendto(b"", (server_ip, 10001))
            print(f"📤 [SENT] Empty UDP packet to {10001}")
            client_receive.sendto(b"", (server_ip, 20001))
            print(f"📤 [SENT] Empty UDP packet to {20001}")
            time.sleep(1)


    client_send.close()
    client_receive.close()
    print("✅ [NAT CONFIRMED] Empty packets received. NAT hole is open!")
   
    #send_gst_command = [
    #    "gst-launch-1.0", "v4l2src", "!", "videoconvert",  "!", "video/x-raw, width=320, height=240, framerate=30/1", "!",
    #    "vp8enc", "deadline=1", "!",  # fast encoding
    #    "rtpvp8pay", "!",
    #    "application/x-rtp,media=video,encoding-name=VP8,payload=96", "!",
    #    "udpsink", f"host={server_ip}", f"port={PORT_SEND}"
    #]
    #gst-launch-1.0 v4l2src ! videoconvert ! video/x-raw,format=I420 
    #! x264enc tune=zerolatency bitrate=500 speed-preset=ultrafast ! 
    #rtph264pay config-interval=1 pt=96 ! udpsink host=127.0.0.1 port=5000
 
    send_gst_command = [
        "gst-launch-1.0", "v4l2src", "device=/dev/video0", 
        "!", "videoconvert", "!", "video/x-raw,width=320,height=240,format=I420",  # x264enc needs I420
        "!", "x264enc", #tune=zerolatency bitrate=500 speed-preset=ultrafast
        "!", "rtph264pay mtu=300",
        "!", "application/x-rtp,media=video,encoding-name=H264,payload=96",
        "!", "udpsink", f"host={server_ip}", f"port={PORT_SEND}"
    ]

    print(f"🎵 [STREAMING] Sending RTP audio to server from Client {client_number} and local port {local_send_port}")

    #subprocess.Popen(send_gst_command)
    subprocess.Popen(" ".join(send_gst_command), shell=True)

    print(f"🎧 [LISTENING] Receiving relayed RTP audio on {local_rcv_port}...")

    #receive_gst_command = [
    #    "gst-launch-1.0", "udpsrc", f"port={local_rcv_port}",
    #    "caps=application/x-rtp,media=video,encoding-name=VP8,payload=96", "!",
    #    "rtpvp8depay", "!", "vp8dec", "!", "videoconvert", "!",
    #    "queue", "!", "autovideosink", "sync=false"
    #]
    
    #gst-launch-1.0 udpsrc port=5000 caps="application/x-rtp,media=video,encoding-name=H264,payload=96" 
    #! rtph264depay ! avdec_h264 ! videoconvert ! autovideosink sync=false

    receive_gst_command = [
        "gst-launch-1.0", "udpsrc", f"port={local_rcv_port}",
        "caps=application/x-rtp,media=video,encoding-name=H264,payload=96",
        "!", "rtph264depay",
        "!", "avdec_h264",
        "!", "videoconvert",
        "!", "queue",
        "!", "autovideosink", "sync=false"
    ]

    #subprocess.Popen(receive_gst_command)
    subprocess.Popen(" ".join(receive_gst_command), shell=True)

    while True:
        time.sleep(1)
    


# **Command-Line Arguments**
parser = argparse.ArgumentParser(description="UDP Audio Client for Server Relay")
parser.add_argument("--server-ip", type=str, required=True, help="Server IP")
parser.add_argument("--client-number", type=int, choices=[1, 2], required=True, help="Client number (1 or 2)")

args = parser.parse_args()

start_client(args.server_ip, args.client_number)
