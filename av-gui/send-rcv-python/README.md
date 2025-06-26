#TENEMP


# Janus video room commands 
Run the server:

```
/usr/local/bin$ sudo ./janus -L 7
```

Run the publisher: 

```
python janusvideoroom-publish.py --server=wss://195.251.234.16:10000 1234
```

Run the subscriber: 

```
python janusvideoroom-subscribe-2.py --server=wss://195.251.234.16:10000 1234
```

Find these messages: 

```
Ack(transaction='1EEOEIGP')
PluginData(sender=5961134235841508, plugin='janus.plugin.videoroom', data={'videoroom': 'joined', 'room': 1234, 'description': 'Demo Room', 'id': 8069138869961132, 'private_id': 2838851466, 'publishers': []}, jsep=None)
Ack(transaction='WCIE6ABN')
PluginData(sender=5961134235841508, plugin='janus.plugin.videoroom', data={'videoroom': 'event', 'error_code': 425, 'error': 'Already in as a publisher on this handle'}, jsep=None)
```

Copy paste the `id` field and while the application runs replace the `feed` attribute with the `id`:


```python
loop = asyncio.get_event_loop()
loop.create_task(signaling.keepalive())
#asyncio.create_task(self.keepalive())

joinmessage = { "request": "join", "ptype": "publisher", "room": 1234, "display": self.peer_id }
#participants = { "request" : "listparticipants", "room" : 1234}
joinmessage2 = { "request": "join", "ptype": "subscriber", "room": 1234, "streams": [{"feed": 3816632741076747}] }
```


Then you need to rerun the subscriber: 

```
python janusvideoroom-subscribe-2.py --server=wss://195.251.234.16:10000 1234
```
