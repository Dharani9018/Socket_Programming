import json
import time

# Packet types
TYPE_JOIN      = "join"
TYPE_WELCOME   = "welcome"
TYPE_INPUT     = "input"
TYPE_STATE     = "state"
TYPE_PING      = "ping"
TYPE_PONG      = "pong"
TYPE_LEAVE     = "leave"

MAX_PACKET     = 65535

def encode(obj: dict) -> bytes:
    return json.dumps(obj).encode("utf-8")

def decode(data: bytes) -> dict:
    return json.loads(data.decode("utf-8"))

def make_input(seq, dx, dy):
    return encode({"t": TYPE_INPUT, "seq": seq, "dx": dx, "dy": dy})

def make_ping(seq):
    return encode({"t": TYPE_PING, "seq": seq, "ts": time.time()})

def make_pong(seq, ts):
    return encode({"t": TYPE_PONG, "seq": seq, "ts": ts})

def make_join(name):
    return encode({"t": TYPE_JOIN, "name": name})

def make_leave(player_id):
    return encode({"t": TYPE_LEAVE, "pid": player_id})
