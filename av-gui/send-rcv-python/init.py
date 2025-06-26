#!/usr/bin/env python3
#ST_DEBUG=webrtcbin:5 ./init.py --server=ws://195.251.234.16:10000
import logging
import random
import ssl
import websockets
import asyncio
import sys
import json
import argparse

import gi
gi.require_version('Gst', '1.0')
from gi.repository import Gst
gi.require_version('GstWebRTC', '1.0')
from gi.repository import GstWebRTC
gi.require_version('GstSdp', '1.0')
from gi.repository import GstSdp
from gi.repository import GObject
from gi.repository import Gst, GstSdp, GstWebRTC


logging.basicConfig(
    format='%(filename)s - %(asctime)s - %(levelname)s - %(message)s'
)

logging.getLogger('matplotlib').setLevel(logging.WARNING)


_logger = logging.getLogger()
_logger.setLevel("DEBUG")

PIPELINE_DESC = '''
webrtcbin name=sendrecv bundle-policy=max-bundle stun-server=stun://stun.l.google.com:19302
 videotestsrc is-live=true pattern=snow ! videoconvert ! queue ! vp8enc deadline=1 ! rtpvp8pay !
 queue ! application/x-rtp,media=video,encoding-name=VP8,payload=97 ! sendrecv.
 audiotestsrc is-live=true wave=red-noise ! audioconvert ! audioresample ! queue ! opusenc ! rtpopuspay !
 queue ! application/x-rtp,media=audio,encoding-name=OPUS,payload=96 ! sendrecv.
'''

class WebRTCClient:
    def __init__(self, id_, server):
        self.id_ = id_
        self.conn = None
        self.pipe = None
        self.webrtc = None
        self.server = server
        self.pipeline_started = False
        self.ice_candidate_queue = []
        self.remote_sdp_set = False
        #self.loop = asyncio.new_event_loop()
        #asyncio.set_event_loop(self.loop)
        self.ssl_context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        self.ssl_context.check_hostname = False
        self.ssl_context.verify_mode = ssl.CERT_NONE

    async def connect(self):
        #self.conn = await websockets.connect(self.server, ssl=self.ssl_context)
        self.conn = await websockets.connect(self.server)
        await self.conn.send(f'HELLO {self.id_}')
        _logger.info(f"Connected to server: {self.server}, ID: {self.id_}")

    def add_ice_candidate(self, message_data):
        # Check if pipeline is started and webrtc element is ready
        if not self.pipeline_started or not self.webrtc:
            _logger.info("Pipeline not started yet. Queueing ICE candidate.")
            self.ice_candidate_queue.append(message_data)
            return
        
        candidate = message_data['ice']['candidate']
        sdp_mline_index = message_data['ice']['sdpMLineIndex']
        self.webrtc.emit("add-ice-candidate", sdp_mline_index, candidate)
        _logger.info(f"Added ICE candidate: {candidate}")


    def on_ice_candidate(_, mlineindex, candidate):
        _logger.debug(f"Debug: ICE candidate generated - {candidate}, mlineindex: {mlineindex}")
        asyncio.run_coroutine_threadsafe(self.send_ice_candidate_message(mlineindex, candidate), loop)
        #await self.send_ice_candidate_message(mlineindex, candidate)
        #loop = asyncio.get_event_loop()  # Get the current event loop explicitly
        #self.webrtc.connect("on-ice-candidate", lambda *args: loop.create_task(on_ice_candidate(*args)))
        #if self.webrtc:
        self.webrtc.emit("add-ice-candidate", mlineindex, candidate)
        #    self.webrtc.connect("on-ice-candidate", lambda *args: asyncio.create_task(on_ice_candidate(*args)))
        #    print("ICE callbacks set up.")
        #else:
        #    print("Warning: WebRTC element not initialized for ICE callbacks.")
        
        

    def send_ice_candidate_message(self, _, mlineindex, candidate):
        icemsg = json.dumps({'ice': {'candidate': candidate, 'sdpMLineIndex': mlineindex}})
        _logger.info(f"Sending ICE candidate: {candidate}, mlineindex: {mlineindex}")
        loop = asyncio.new_event_loop()
        loop.run_until_complete(self.conn.send(icemsg))
        loop.close()
    

    def start_pipeline(self):
        turn_servers = [
            {
                "urls": "turn:195.251.234.16:3478",
            }
        ]
        _logger.info("start_pipeline(self)")
        self.pipe = Gst.parse_launch(PIPELINE_DESC)
        self.webrtc = self.pipe.get_by_name('sendrecv')
        if not self.webrtc:
            _logger.error("Error: WebRTC element was not created.")
        else:
            _logger.debug("IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIII")
            _logger.info("WebRTC element created successfully, setting properties...")
        # If an SDP offer was received earlier, process it now
        #if self.pending_sdp_offer:
        #    self.handle_sdp(self.pending_sdp_offer)
        #    self.pending_sdp_offer = None  # Clear the stored SDP offer
        self.webrtc.set_property("stun-server", "stun://stun.l.google.com:19302")
        #self.webrtc.set_property("turn-server", json.dumps(turn_servers))
        self.webrtc.connect('on-ice-candidate', self.send_ice_candidate_message)
        _logger.info("Connected on-ice-candidate signal.")
        self.webrtc.connect('pad-added', self.on_incoming_stream)
        _logger.info("webrtc.connect('pad-added', self.on_incoming_stream)")
        # Start the pipeline
        self.pipe.set_state(Gst.State.PLAYING)
        _logger.info("Pipeline started and waiting for incoming streams.")

        # Process any queued ICE candidates
        #self.process_queued_ice_candidates()
        #GObject.MainLoop().run()
        
        
        
    def process_queued_ice_candidates(self):
        """Process all ICE candidates that were queued before the pipeline started."""
        while self.ice_candidate_queue:
            candidate_data = self.ice_candidate_queue.pop(0)
            self.add_ice_candidate(candidate_data)
        _logger.info("All queued ICE candidates have been processed.")
        
        
        
            

    def handle_sdp(self, offer_sdp):
        _logger.info("Received SDP offer, preparing to set as remote description.")
        _logger.debug("0000000000000000000000000000000000000000000000000")
        _logger.info("Original Offer SDP:") #, offer_sdp
        # Extract the actual SDP string from the dictionary
        #if isinstance(offer_sdp, dict) and "sdp" in offer_sdp:
        #    offer_sdp_str = offer_sdp["sdp"]
        #else:
        #    print("Error: SDP offer format is invalid.")
        #    return
        if not self.webrtc:  # Check if `self.webrtc` is ready
            _logger.info("Pipeline not ready, storing SDP offer.")
            self.pending_sdp_offer = offer_sdp  # Store the SDP offer until ready
            return

        # Check if `offer_sdp` is a dictionary and attempt to extract the SDP string
        if isinstance(offer_sdp, dict):
            if "sdp" in offer_sdp:
                _logger.debug("SDP content structure:") #, offer_sdp["sdp"]
                _logger.debug("Type of offer_sdp['sdp']:", type(offer_sdp["sdp"]))
                
                # Further drill down if it’s still a dictionary
                if isinstance(offer_sdp["sdp"], dict):
                    _logger.debug("Nested structure within 'sdp':") #, offer_sdp["sdp"]

                    # Check if there's an actual SDP string inside this nested dictionary
                    if "sdp" in offer_sdp["sdp"] and isinstance(offer_sdp["sdp"]["sdp"], str):
                        offer_sdp_str = offer_sdp["sdp"]["sdp"]
                        _logger.debug("Extracted SDP stringgggg:") #, offer_sdp_str
                        _logger.debug("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA")
                    else:
                        _logger.error("Error: 'sdp' key in nested dictionary is not a string.")
                        return
                elif isinstance(offer_sdp["sdp"], str):
                    offer_sdp_str = offer_sdp["sdp"]
                    _logger.debug("Extracted SDP string:") #, offer_sdp_str
                    _logger.debug("BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB")
                else:
                    _logger.error("Error: 'sdp' key not found as expected.")
                    return
            else:
                _logger.error("Error: 'sdp' key not found in offer_sdp.")
                return
        else:
            _logger.error("Error: offer_sdp is not a dictionary.")
            return

        # Encode the SDP string if extracted successfully
        try:
            encoded_offer_sdp = offer_sdp_str.encode('utf-8')
            _logger.info("Length of encoded SDP offer:", len(encoded_offer_sdp))
        except Exception as e:
            _logger.error("Exception during encoding:", e)               
        #test_sdp = "v=0\r\no=- 12345 0 IN IP4 127.0.0.1\r\ns=Test SDP\r\nt=0 0\r\nm=audio 9 UDP/TLS/RTP/SAVPF 96\r\na=rtpmap:96 OPUS/48000/2\r\n"      
        
        # Parse the SDP offer and create a WebRTC session description
        result, sdp_message = GstSdp.SDPMessage.new()
        _logger.debug("1111111111111111111111111111111111111)")
        #print(result)
        if result != GstSdp.SDPResult.OK:
            _logger.error("Error: Failed to initialize SDP message.")
            return
        _logger.debug("2222222222222222222222222222222222222222")

            # Parsing the SDP offer
        try:
            _logger.debug("33333333333333333333333333333333333333333")
            parse_result = GstSdp.sdp_message_parse_buffer(encoded_offer_sdp, sdp_message)         
            _logger.debug("444444444444444444444444444444444444444444")
            if parse_result != GstSdp.SDPResult.OK:
                _logger.error("Error: Failed to parse SDP offer.")
                return
        except Exception as e:
            _logger.error("Exception during SDP parsing:", e)
            return
        _logger.debug("55555555555555555555555555555555555555555")
        
    

       
        _logger.debug("66666666666666666666666666666666666666666")
        self.pipe.set_state(Gst.State.PLAYING)

        try:
            offer = GstWebRTC.WebRTCSessionDescription.new(GstWebRTC.WebRTCSDPType.OFFER, sdp_message)
            promise = Gst.Promise.new_with_change_func(self.on_remote_description_set)
            self.webrtc.emit('set-remote-description', offer, promise)
            #promise.interrupt()
            _logger.info("Remote description set successfully")
            _logger.debug("77777777777777777777777777777777777777777")
        except Exception as e:
            _logger.error(f"Error emitting set-remote-description: {e}")




    def on_remote_description_set(self, promise):
        promise.wait()
        _logger.debug("88888888888888888888888888888888888888888888888")
        #print("Remote description set successfully.")
        # Now create and send the SDP answer
        self.create_answer()


        
    def create_answer(self):

        _logger.debug("9999999999999999999999999999999999999999999999")
        _logger.info("Creating SDP answer...")
        # Use a promise to handle the SDP answer creation asynchronously
        promise = Gst.Promise.new_with_change_func(self.on_sdp_answer_created)
        self.webrtc.emit("create-answer", None, promise)

    def on_sdp_answer_created(self, promise):
        promise.wait()
        reply = promise.get_reply()
        answer = reply.get_value('answer')
        _logger.debug("EEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEE")
        
        sdp_text = answer.sdp.as_text()
        #print(sdp_text)
        promise = Gst.Promise.new()
        self.webrtc.emit('set-local-description', answer, promise)
        promise.interrupt()
        asyncio.run(self.send_sdp_message("answer", sdp_text))
        self.pipe.set_state(Gst.State.PLAYING)
        
    async def send_sdp_message(self, sdp_type, sdp_text):
        _logger.debug("send_sdp_message")
        """Send the SDP message back to the sender."""
        message = {"sdp": {"type": sdp_type, "sdp": sdp_text}}
        await self.conn.send(json.dumps(message))
        _logger.info(f"Sent SDP message:\n") #{message}



        
    #def close_pipeline(self):
    #    if self.pipe:
    #        self.pipe.set_state(Gst.State.NULL)
    #        self.pipe = None
    #        self.webrtc = None
    #        print("Pipeline closed")

    async def loop(self):
        assert self.conn
        async for message in self.conn:
            #executor = concurrent.futures.ThreadPoolExecutor()
            _logger.debug(f"Message received: {message}") #{message}

            if 'HELLO' in message:
                _logger.info("Received HELLO message from server.")
            #    # The initiator does not need to set up a call here
            #elif message == 'SESSION_OK':
            #    print(f"Error received: {message}")
            #    self.close_pipeline()
            #    return 1
            else:
                # Parse the message as JSON
                try:
                    message_data = json.loads(message)
                    #print(f"Parsed JSON message: {message_data}")

                    # Handle SDP or ICE based on the message content
                    
                    if "sdp" in message_data:
                        _logger.info("SDP messageeee received. Starting pipeline to handle SDP.")
                        self.start_pipeline()  # Start the pipeline
                        #asyncio.run(self.start_pipeline())
                        #asyncio.create_task(self.start_pipeline())
                        #executor.submit(self.start_pipeline)
                        self.handle_sdp(message_data)  # Handle the SDP message
                        #self.handle_sdp(message_data)
                        #asyncio.run(self.handle_sdp(message_data))
                        #asyncio.create_task(self.handle_sdp(message_data))
                        #executor.submit(self.handle_sdp, message_data)
                    elif "ice" in message_data and "candidate" in message_data["ice"]:
                        #print("ICE candidate received.")
                        #self.add_ice_candidate(message_data)  # Add the ICE candidate to webrtcbin
                        ice = message_data['ice']
                        candidate = ice['candidate']
                        sdpmlineindex = ice['sdpMLineIndex']
                        _logger.info(f"Received ICE candidate: {candidate}, sdpMLineIndex: {sdpmlineindex}")
                        self.webrtc.emit('add-ice-candidate', sdpmlineindex, candidate)

                except json.JSONDecodeError:
                    _logger.error("Failed toooo decode message as JSON.")
                    continue


        # Once done, close the pipeline
        #self.close_pipeline()
        return 0



    def on_incoming_stream(self, webrtc, pad):
        _logger.info("on_incoming_stream")
        # Triggered when WebRTC receives a new stream
        if pad.get_direction() == Gst.PadDirection.SRC:
            decodebin = Gst.ElementFactory.make("decodebin")
            decodebin.connect("pad-added", self.on_incoming_decodebin_stream)
            self.pipe.add(decodebin)
            pad.link(decodebin.get_static_pad("sink"))
            decodebin.sync_state_with_parent()
            
    def on_incoming_decodebin_stream(self, decodebin, pad):
        caps = pad.get_current_caps()
        structure = caps.get_structure(0)
        media_type = structure.get_name()
        
        if media_type.startswith("video"):
            sink = Gst.ElementFactory.make("autovideosink")
            self.pipe.add(sink)
            sink.sync_state_with_parent()
            pad.link(sink.get_static_pad("sink"))
            _logger.info("Linked video stream to autovideosink")

        elif media_type.startswith("audio"):
            sink = Gst.ElementFactory.make("autoaudiosink")
            self.pipe.add(sink)
            sink.sync_state_with_parent()
            pad.link(sink.get_static_pad("sink"))
            _logger.info("Linked audio stream to autoaudiosink")

    

    #async def stop(self):
    #    if self.conn:
    #        await self.conn.close()
    #    self.conn = None

if __name__ == '__main__':
    Gst.init(None)
    parser = argparse.ArgumentParser()
    parser.add_argument('--server', help='Signaling server to connect to, e.g., "wss://127.0.0.1:10000"')
    args = parser.parse_args()

    _logger.info(f"Connecting to signalling server on: {args.server}")
    client_id = random.randint(10, 10000)
    client = WebRTCClient(client_id, args.server)
    loop = asyncio.get_event_loop()
    loop.run_until_complete(client.connect())
    result = loop.run_until_complete(client.loop())
    sys.exit(result)




