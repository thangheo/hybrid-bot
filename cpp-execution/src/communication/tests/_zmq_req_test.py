#!/usr/bin/env python3
import zmq
import json
import time
import uuid

ctx = zmq.Context()
sock = ctx.socket(zmq.REQ)
sock.connect('tcp://127.0.0.1:5555')
# Build request matching server's expected shape: {"data": <string>, "metadata": {...}}
request_payload = {
    "strategy_id": "integrated_strategy_test_v1",
    "action": "get"
}

message = {
    "data": json.dumps(request_payload),
    "metadata": {
        "type": "strategy_parameters",
        "request_id": "{}".format(uuid.uuid4()),
        "timestamp": str(int(time.time()*1000))
    }
}

msg_str = json.dumps(message)
print('Sending request to tcp://127.0.0.1:5556 ->', msg_str)
try:
    sock.send(msg_str.encode('utf-8'))
    # Wait up to 5s
    sock.RCVTIMEO = 5000
    resp = sock.recv()
    try:
        print('Raw response:', resp[:200])
        print('Parsed response:', json.loads(resp.decode('utf-8')))
    except Exception:
        print('Response not JSON:', resp)
except Exception as e:
    print('Request failed:', e)
finally:
    sock.close()
    ctx.term()
