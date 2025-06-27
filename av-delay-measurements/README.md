### AV Delay Measurements

This directory includes Python scripts for **ultra low delay audio and video streaming** using **GStreamer pipelines**  for both *peer-to-peer* and *client-SFU* modes.

#### P2P architecture
##### Audio
The *p2p_audio_client_hole_punch.py* script can run at both peers to establish a bidirectional audio communication.

First run the signaling server script at a machine with public IP.
> python p2p_signaling_server_hole_punch_bidirectional.py

Then at the first peer, behind NAT run
> python p2p_audio_client_hole_punch.py --server-ip SERVER_IP --local-port 5000 --peer-number 1

And at the second peer behind NAT run
> python p2p_audio_client_hole_punch.py --server-ip SERVER_IP --local-port 5000 --peer-number 2

Where SERVER_IP is the actual public IP of your server.

##### Video
The *p2p_video_client_hole_punch.py* script can run at both peers to establish a bidirectional audio communication.

First run the signaling server script at a machine with public IP.
> python p2p_signaling_server_hole_punch_bidirectional.py

Then at the first peer, behind NAT run
> python p2p_video_client_hole_punch.py --server-ip SERVER_IP --local-port 5000 --peer-number 1

And at the second peer behind NAT run
> python p2p_video_client_hole_punch.py --server-ip SERVER_IP --local-port 5000 --peer-number 2

Where SERVER_IP is the actual public IP of your server.


#### Client-SFU architecture
##### Audio

In this mode, the *udp_audio_relay_server.py* acts as a udp AUDIO relay server and must run on a machine with a public IP and open UDP ports 10000, 10001, 20000, 20001

First run the relay server:

> python udp_audio_relay_server.py

Then run the first client of your choise:

> python udp_audio_client.py --server-ip SERVER_IP --client-number 1

and then run the second client.

> python udp_audio_client.py --server-ip SERVER_IP --client-number 2

After that UDP hole punching will take place and bidirectional audio communication will be established.

##### Video

The *udp_audio_relay_server.py* acts as a udp VIDEO relay server and must run on a machine with a public IP and open UDP ports 10000, 10001, 20000, 20001

First run the relay server:

> python udp_video_relay_server.py

Then run the first client of your choise:

> python udp_video_client.py --server-ip SERVER_IP --client-number 1

and then run the second client.

> python udp_video_client.py --server-ip SERVER_IP --client-number 2

After that UDP hole punching will take place and bidirectional VIDEO communication will be established.




