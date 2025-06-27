#python3 udp_audio_relay_server.py 

import socket
import time
import subprocess

PORT_CLIENT1 = 10000  # Client1 → Server (Receiving Audio)
PORT_CLIENT2 = 10001  # Client2 → Server (Receiving Audio)
PORT_SEND1 = 20000  # Server → Client1 (Sending Audio)
PORT_SEND2 = 20001  # Server → Client2 (Sending Audio)
EMPTY_PACKET_COUNT = 10  # Number of empty packets for NAT punching

def start_server():
    """ Server Mode: Relays RTP audio between two NAT clients. """
    server_recv1 = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    server_recv2 = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    server_recv3 = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    server_recv4 = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    server_recv1.bind(("0.0.0.0", PORT_CLIENT1))
    server_recv2.bind(("0.0.0.0", PORT_SEND1))

    server_recv3.bind(("0.0.0.0", PORT_CLIENT2))
    server_recv4.bind(("0.0.0.0", PORT_SEND2))

    print(f"🌍 [SERVER] Listening on ports {PORT_CLIENT1} , {PORT_CLIENT2},{PORT_SEND1} , {PORT_SEND2} for incoming RTP audio.")

    # Receive client "Hello" messages to establish NAT mapping
    data1, addr1 = server_recv1.recvfrom(1024)
    data2, addr2 = server_recv2.recvfrom(1024)
    
    data3, addr3 = server_recv3.recvfrom(1024)
    data4, addr4 = server_recv4.recvfrom(1024)
    

    client1_ip, client1_port_1 = addr1
    client1_ip, client1_port_2 = addr2

    client2_ip, client2_port_1 = addr3
    client2_ip, client2_port_2 = addr4

    print(f"✅ [CLIENT1 DETECTED] {client1_ip}:{client1_port_1}")
    print(f"✅ [CLIENT1 DETECTED] {client1_ip}:{client1_port_2}")

    print(f"✅ [CLIENT1 DETECTED] {client2_ip}:{client2_port_1}")
    print(f"✅ [CLIENT2 DETECTED] {client2_ip}:{client2_port_2}")

    # Send public IP & port mappings back to clients
    server_recv1.sendto(f"{client1_ip}:{client1_port_1}".encode(), addr1)
    server_recv2.sendto(f"{client1_ip}:{client1_port_2}".encode(), addr2)

    server_recv3.sendto(f"{client2_ip}:{client2_port_1}".encode(), addr3)
    server_recv4.sendto(f"{client2_ip}:{client2_port_2}".encode(), addr4)

    # Send empty packets to keep NAT traversal open
    print("🚀 [KEEP-ALIVE] Sending empty packets to maintain NAT traversal...")
    for _ in range(EMPTY_PACKET_COUNT):
        server_recv1.sendto(b"", addr1)
        print(f"📤 [SENT] Empty UDP packet to {addr1}")
        server_recv2.sendto(b"", addr2)
        print(f"📤 [SENT] Empty UDP packet to {addr2}")
        server_recv3.sendto(b"", addr3)
        print(f"📤 [SENT] Empty UDP packet to {addr3}")
        server_recv4.sendto(b"", addr4)
        print(f"📤 [SENT] Empty UDP packet to {addr4}")
        time.sleep(1)


    print("🧹 [CLEANUP] Closing NAT punching sockets so GStreamer can bind")
    server_recv2.close()
    server_recv4.close()
    time.sleep(2)
    # Start relaying Client1 → Client2 and Client2 → Client1
    print(f"🔄 [RELAY] Forwarding Client1 ({client1_ip}:{client1_port_1}) → Client2 ({client2_ip}:{client2_port_1})")
    print(f"🔄 [RELAY] Forwarding Client2 ({client2_ip}:{client2_port_1}) → Client1 ({client1_ip}:{client1_port_2})")


    '''
    #Each client listen to its self
    relay_gst_command1 = [
        "gst-launch-1.0", "-v",
        "udpsrc", f"port={PORT_CLIENT1}",
        "caps=application/x-rtp,media=audio,clock-rate=48000,encoding-name=L16,channels=1", "!",
        "queue", "!", "multiudpsink", f"clients={client1_ip}:{client1_port_2}", "bind-port=20000"
    ]

    subprocess.Popen(" ".join(relay_gst_command1), shell=True)

    relay_gst_command2 = [
        "gst-launch-1.0", "-v",
        "udpsrc", f"port={PORT_CLIENT2}",
        "caps=application/x-rtp,media=audio,clock-rate=48000,encoding-name=L16,channels=1", "!",
        "queue", "!", "multiudpsink", f"clients={client2_ip}:{client2_port_2}", "bind-port=20001"
    ]
    subprocess.Popen(" ".join(relay_gst_command2), shell=True)
    print(f"🎵 [STREAMING] Audio relay active between Client1 & Client2 via the server!")
    '''



    #Each client listen to the other client
    relay_gst_command1 = [
        "gst-launch-1.0", "-v",
        "udpsrc", f"port={PORT_CLIENT2}",
        "caps=application/x-rtp,media=audio,clock-rate=48000,encoding-name=L16,channels=1", "!",
        "queue", "!", "multiudpsink", f"clients={client1_ip}:{client1_port_2}", "bind-port=20000"
    ]

    subprocess.Popen(" ".join(relay_gst_command1), shell=True)

    relay_gst_command2 = [
        "gst-launch-1.0", "-v",
        "udpsrc", f"port={PORT_CLIENT1}",
        "caps=application/x-rtp,media=audio,clock-rate=48000,encoding-name=L16,channels=1", "!",
        "queue", "!", "multiudpsink", f"clients={client2_ip}:{client2_port_2}", "bind-port=20001"
    ]
    subprocess.Popen(" ".join(relay_gst_command2), shell=True)
    print(f"🎵 [STREAMING] Audio relay active between Client1 & Client2 via the server!")


    
    '''
    #relay noise to both  clients
    loopback_gst_command1 =[
        "gst-launch-1.0", "audiotestsrc", "wave=white-noise", "!",
        "audioconvert", "!", "audioresample", "!", "rtpL16pay", "!",
        "application/x-rtp,media=audio,encoding-name=L16,payload=96", "!",
        "multiudpsink", f"clients={client1_ip}:{client1_port_2}", "bind-port=20000"
    ]
    loopback_gst_command2 =[
        "gst-launch-1.0", "audiotestsrc", "wave=white-noise", "!",
        "audioconvert", "!", "audioresample", "!", "rtpL16pay", "!",
        "application/x-rtp,media=audio,encoding-name=L16,payload=96", "!",
        "multiudpsink", f"clients={client2_ip}:{client2_port_2}", "bind-port=20001"
    ]
    subprocess.Popen(" ".join(loopback_gst_command1), shell=True)
    subprocess.Popen(" ".join(loopback_gst_command2), shell=True)
    '''  

start_server()
