## AV Delay Measurements

This directory includes Python scripts for **ultra low delay audio and video streaming** using **GStreamer pipelines**  for both *peer-to-peer* and *client-SFU* modes.

### P2P architecture
#### Audio
The *p2p_audio_client_hole_punch.py* script can run at both peers to establish a bidirectional audio communication.
First run the signaling server script at a machine with public IP.
> python p2p_signaling_server_hole_punch_bidirectional.py

