import socket
import time
import math
import random
import threading
import json
import uuid
import sys
import curses
from protocol import *

HOST      = "0.0.0.0"
PORT      = 5555
TICK_RATE = 20          # Hz
TICK_S    = 1.0 / TICK_RATE
ARENA_W   = 60
ARENA_H   = 25
PLAYER_R  = 1.0
COIN_R    = 0.8
SPEED     = 1.5
MAX_COINS = 10
TIMEOUT   = 10.0        # seconds before player removed

COLORS_LIST = ["R","G","B","Y","M","C","W"]

# ── State ──────────────────────────────────────────────────────────────────────

players  = {}   # pid -> dict
coins    = {}   # cid -> dict
clients  = {}   # addr -> pid
last_seen= {}   # pid -> timestamp
tick     = 0
stats    = {
    "pkts_recv": 0,
    "pkts_sent": 0,
    "clients":   0,
}
lock = threading.Lock()

def spawn_coin():
    cid = uuid.uuid4().hex[:6]
    coins[cid] = {
        "id": cid,
        "x":  2 + random.random() * (ARENA_W - 4),
        "y":  2 + random.random() * (ARENA_H - 4),
    }

def add_player(pid, name, addr):
    color = COLORS_LIST[len(players) % len(COLORS_LIST)]
    players[pid] = {
        "id":    pid,
        "name":  name[:10],
        "color": color,
        "x":     5 + random.random() * (ARENA_W - 10),
        "y":     3 + random.random() * (ARENA_H - 6),
        "score": 0,
        "dx":    0,
        "dy":    0,
        "addr":  addr,
    }
    last_seen[pid] = time.time()

def remove_player(pid):
    players.pop(pid, None)
    last_seen.pop(pid, None)
    # remove from clients map
    to_del = [a for a, p in clients.items() if p == pid]
    for a in to_del:
        clients.pop(a, None)

def get_state_packet():
    ps = {pid: {k: v for k, v in p.items() if k != "addr"}
          for pid, p in players.items()}
    return encode({
        "t":       TYPE_STATE,
        "tick":    tick,
        "players": ps,
        "coins":   coins,
    })

# ── Game loop ──────────────────────────────────────────────────────────────────

def game_loop(sock):
    global tick
    while True:
        t0 = time.time()
        with lock:
            # Remove timed-out players
            now = time.time()
            timed_out = [pid for pid, ts in last_seen.items() if now - ts > TIMEOUT]
            for pid in timed_out:
                remove_player(pid)

            # Tick each player
            for pid, p in list(players.items()):
                dx, dy = p["dx"], p["dy"]
                length = math.sqrt(dx*dx + dy*dy)
                if length > 0:
                    dx /= length; dy /= length
                p["x"] = max(PLAYER_R, min(ARENA_W - PLAYER_R, p["x"] + dx * SPEED))
                p["y"] = max(PLAYER_R, min(ARENA_H - PLAYER_R, p["y"] + dy * SPEED))
                p["dx"] = 0; p["dy"] = 0

                # Coin collection
                for cid in list(coins.keys()):
                    c = coins[cid]
                    if math.hypot(p["x"] - c["x"], p["y"] - c["y"]) < PLAYER_R + COIN_R:
                        p["score"] += 1
                        del coins[cid]
                        spawn_coin()

            # Refill coins
            while len(coins) < MAX_COINS:
                spawn_coin()

            tick += 1

            # Broadcast state to all clients
            if players:
                pkt = get_state_packet()
                for pid, p in players.items():
                    try:
                        sock.sendto(pkt, p["addr"])
                        stats["pkts_sent"] += 1
                    except Exception:
                        pass

        elapsed = time.time() - t0
        time.sleep(max(0, TICK_S - elapsed))

# ── Receive loop ───────────────────────────────────────────────────────────────

def recv_loop(sock):
    while True:
        try:
            data, addr = sock.recvfrom(MAX_PACKET)
            stats["pkts_recv"] += 1
            msg = decode(data)
            t   = msg.get("t")

            with lock:
                if t == TYPE_JOIN:
                    name = msg.get("name", "Player")
                    if addr not in clients:
                        pid = uuid.uuid4().hex[:8]
                        clients[addr] = pid
                        add_player(pid, name, addr)
                        welcome = encode({
                            "t":    TYPE_WELCOME,
                            "pid":  pid,
                            "w":    ARENA_W,
                            "h":    ARENA_H,
                        })
                        sock.sendto(welcome, addr)
                        stats["pkts_sent"] += 1

                elif t == TYPE_INPUT:
                    pid = clients.get(addr)
                    if pid and pid in players:
                        players[pid]["dx"] = msg.get("dx", 0)
                        players[pid]["dy"] = msg.get("dy", 0)
                        last_seen[pid]     = time.time()

                elif t == TYPE_PING:
                    pong = make_pong(msg.get("seq", 0), msg.get("ts", 0))
                    sock.sendto(pong, addr)
                    stats["pkts_sent"] += 1
                    pid = clients.get(addr)
                    if pid:
                        last_seen[pid] = time.time()

                elif t == TYPE_LEAVE:
                    pid = clients.get(addr)
                    if pid:
                        remove_player(pid)

        except Exception as e:
            pass

# ── Server TUI ─────────────────────────────────────────────────────────────────

def server_tui(stdscr):
    curses.curs_set(0)
    stdscr.nodelay(True)

    # Colors
    curses.start_color()
    curses.init_pair(1, curses.COLOR_YELLOW,  curses.COLOR_BLACK)
    curses.init_pair(2, curses.COLOR_GREEN,   curses.COLOR_BLACK)
    curses.init_pair(3, curses.COLOR_CYAN,    curses.COLOR_BLACK)
    curses.init_pair(4, curses.COLOR_RED,     curses.COLOR_BLACK)
    curses.init_pair(5, curses.COLOR_MAGENTA, curses.COLOR_BLACK)

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((HOST, PORT))
    sock.setblocking(False)

    # Seed coins
    with lock:
        for _ in range(MAX_COINS):
            spawn_coin()

    # Start threads
    threading.Thread(target=recv_loop,  args=(sock,), daemon=True).start()
    threading.Thread(target=game_loop,  args=(sock,), daemon=True).start()

    while True:
        key = stdscr.getch()
        if key == ord('q'):
            break

        stdscr.erase()
        h, w = stdscr.getmaxyx()

        # Title
        title = " 🪙 COIN COLLECTOR — UDP SERVER "
        stdscr.addstr(0, max(0, w//2 - len(title)//2), title, curses.color_pair(1) | curses.A_BOLD)

        # Server info
        import socket as _s
        try:
            local_ip = _s.gethostbyname(_s.gethostname())
        except Exception:
            local_ip = "127.0.0.1"

        stdscr.addstr(1, 2, f"Listening on  {local_ip}:{PORT}  (share this IP with clients)", curses.color_pair(3))
        stdscr.addstr(2, 2, f"Tick: {tick}   Players: {len(players)}   Coins: {len(coins)}   "
                            f"Pkts Recv: {stats['pkts_recv']}   Pkts Sent: {stats['pkts_sent']}",
                      curses.color_pair(2))
        stdscr.addstr(3, 2, "Press Q to quit", curses.color_pair(5))

        # Divider
        stdscr.addstr(4, 0, "─" * min(w, 80), curses.color_pair(5))

        # Arena
        arena_top = 5
        arena_left = 2

        with lock:
            # Draw border
            for x in range(ARENA_W + 2):
                if arena_top < h and arena_left + x < w:
                    stdscr.addch(arena_top, arena_left + x, '#', curses.color_pair(5))
                if arena_top + ARENA_H + 1 < h and arena_left + x < w:
                    stdscr.addch(arena_top + ARENA_H + 1, arena_left + x, '#', curses.color_pair(5))
            for y in range(ARENA_H + 2):
                if arena_top + y < h and arena_left < w:
                    stdscr.addch(arena_top + y, arena_left, '#', curses.color_pair(5))
                if arena_top + y < h and arena_left + ARENA_W + 1 < w:
                    stdscr.addch(arena_top + y, arena_left + ARENA_W + 1, '#', curses.color_pair(5))

            # Draw coins
            for c in coins.values():
                cx = arena_left + 1 + int(c["x"])
                cy = arena_top  + 1 + int(c["y"])
                if 0 < cy < h and 0 < cx < w:
                    stdscr.addch(cy, cx, '$', curses.color_pair(1))

            # Draw players
            color_map = {"R": 4, "G": 2, "B": 3, "Y": 1, "M": 5, "C": 3, "W": 0}
            for p in players.values():
                px = arena_left + 1 + int(p["x"])
                py = arena_top  + 1 + int(p["y"])
                cp = color_map.get(p["color"], 2)
                if 0 < py < h and 0 < px < w:
                    stdscr.addch(py, px, '@', curses.color_pair(cp) | curses.A_BOLD)

        # Leaderboard (right of arena)
        lb_x = arena_left + ARENA_W + 5
        stdscr.addstr(arena_top, lb_x, "LEADERBOARD", curses.color_pair(1) | curses.A_BOLD)
        with lock:
            sorted_p = sorted(players.values(), key=lambda p: p["score"], reverse=True)
            for i, p in enumerate(sorted_p[:10]):
                if arena_top + 1 + i < h and lb_x < w - 20:
                    stdscr.addstr(arena_top + 1 + i, lb_x,
                                  f"{i+1}. {p['name'][:8]:<8} {p['score']:>4}",
                                  curses.color_pair(2))

        stdscr.refresh()
        time.sleep(0.1)

if __name__ == "__main__":
    curses.wrapper(server_tui)
