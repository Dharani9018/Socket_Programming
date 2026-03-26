import socket
import threading
import time
import math
import curses
import argparse
import sys
import random
from protocol import *

# ── Args ───────────────────────────────────────────────────────────────────────
parser = argparse.ArgumentParser(description="Coin Collector UDP Client")
parser.add_argument("--host", default="127.0.0.1", help="Server IP")
parser.add_argument("--port", default=5555,        type=int, help="Server port")
parser.add_argument("--name", default="Player",    help="Your name")
args = parser.parse_args()

# ── Network ────────────────────────────────────────────────────────────────────
sock   = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.settimeout(0.05)
SERVER = (args.host, args.port)

# ── State ──────────────────────────────────────────────────────────────────────
my_id       = None
arena_w     = 50
arena_h     = 20
game_state  = {"players": {}, "coins": {}, "tick": 0}
connected   = False
state_lock  = threading.Lock()

SPEED    = 1.5
PLAYER_R = 1.0

# ── Prediction ─────────────────────────────────────────────────────────────────
pred_x    = 0.0
pred_y    = 0.0
pred_lock = threading.Lock()

# ── Stats ──────────────────────────────────────────────────────────────────────
last_seq_recv  = 0
out_of_order   = 0
pkts_recv      = 0
pkts_sent      = 0
rtt_samples    = []
rtt            = 0.0
rtt_min        = float('inf')
rtt_max        = 0.0
jitter         = 0.0
last_rtt       = 0.0
ping_seq       = 0
pending_pings  = {}
pred_err_acc   = 0.0
pred_err_count = 0
tick_count     = 0
last_tick_ts   = time.time()
tick_rate      = 0

# ── Net sim ────────────────────────────────────────────────────────────────────
sim_latency = 0
sim_loss    = 0
sim_jitter  = 0

# ── Input ──────────────────────────────────────────────────────────────────────
current_dir = {"dx": 0, "dy": 0}
input_seq   = 0

# ═══════════════════════════════════════════════════════════════════════════════
# SEND
# ═══════════════════════════════════════════════════════════════════════════════
def send_pkt(data: bytes):
    global pkts_sent
    pkts_sent += 1
    if sim_loss > 0 and (random.random() * 100 < sim_loss):
        return
    delay = sim_latency / 1000.0
    if sim_jitter > 0:
        delay += (random.random() - 0.5) * 2 * sim_jitter / 1000.0
    if delay < 0.001:
        try:    sock.sendto(data, SERVER)
        except: pass
    else:
        def _send():
            time.sleep(max(0, delay))
            try:    sock.sendto(data, SERVER)
            except: pass
        threading.Thread(target=_send, daemon=True).start()

# ═══════════════════════════════════════════════════════════════════════════════
# RECEIVE LOOP
# ═══════════════════════════════════════════════════════════════════════════════
def recv_loop():
    global my_id, connected, arena_w, arena_h
    global game_state, last_seq_recv, out_of_order, pkts_recv
    global rtt, rtt_min, rtt_max, jitter, last_rtt, rtt_samples
    global pred_x, pred_y, pred_err_acc, pred_err_count
    global tick_count, last_tick_ts, tick_rate

    while True:
        try:
            data, _ = sock.recvfrom(MAX_PACKET)
            msg = decode(data)
            t   = msg.get("t")
            pkts_recv += 1

            if t == TYPE_WELCOME:
                my_id    = msg["pid"]
                arena_w  = msg.get("w", 50)
                arena_h  = msg.get("h", 20)
                connected = True
                with pred_lock:
                    pred_x = arena_w / 2.0
                    pred_y = arena_h / 2.0

            elif t == TYPE_STATE:
                incoming_tick = msg.get("tick", 0)
                if incoming_tick <= last_seq_recv and last_seq_recv > 0:
                    out_of_order += 1
                    continue
                last_seq_recv = incoming_tick

                tick_count += 1
                now = time.time()
                if now - last_tick_ts >= 1.0:
                    tick_rate    = tick_count
                    tick_count   = 0
                    last_tick_ts = now

                with state_lock:
                    game_state = msg

                if my_id:
                    me = msg["players"].get(my_id)
                    if me:
                        with pred_lock:
                            err = math.hypot(pred_x - me["x"], pred_y - me["y"])
                            pred_err_acc   += err
                            pred_err_count += 1
                            if err > 1.5:
                                pred_x += (me["x"] - pred_x) * 0.4
                                pred_y += (me["y"] - pred_y) * 0.4

            elif t == TYPE_PONG:
                seq = msg.get("seq")
                if seq in pending_pings:
                    sample = (time.time() - pending_pings.pop(seq)) * 1000
                    rtt_samples.append(sample)
                    if len(rtt_samples) > 30:
                        rtt_samples.pop(0)
                    rtt     = sum(rtt_samples) / len(rtt_samples)
                    rtt_min = min(rtt_min, sample)
                    rtt_max = max(rtt_max, sample)
                    jitter  = abs(sample - last_rtt)
                    last_rtt = sample

        except socket.timeout:
            pass
        except Exception:
            pass

# ═══════════════════════════════════════════════════════════════════════════════
# PING LOOP
# ═══════════════════════════════════════════════════════════════════════════════
def ping_loop():
    global ping_seq
    while True:
        if connected:
            ping_seq += 1
            pending_pings[ping_seq] = time.time()
            send_pkt(make_ping(ping_seq))
        time.sleep(1.0)

# ═══════════════════════════════════════════════════════════════════════════════
# INPUT SEND LOOP
# ═══════════════════════════════════════════════════════════════════════════════
def input_send_loop():
    global input_seq, pred_x, pred_y
    while True:
        if connected:
            dx = current_dir["dx"]
            dy = current_dir["dy"]
            ln = math.sqrt(dx*dx + dy*dy)
            if ln > 0:
                ndx, ndy = dx/ln, dy/ln
                with pred_lock:
                    pred_x = max(PLAYER_R, min(arena_w - PLAYER_R, pred_x + ndx * SPEED))
                    pred_y = max(PLAYER_R, min(arena_h - PLAYER_R, pred_y + ndy * SPEED))
            if dx != 0 or dy != 0:
                input_seq += 1
                send_pkt(make_input(input_seq, dx, dy))
        time.sleep(1.0 / 30)

# ═══════════════════════════════════════════════════════════════════════════════
# DRAW HELPERS
# ═══════════════════════════════════════════════════════════════════════════════
def safe_addch(win, y, x, ch, attr=0):
    try:    win.addch(y, x, ch, attr)
    except: pass

def safe_addstr(win, y, x, s, attr=0):
    try:    win.addstr(y, x, s, attr)
    except: pass

def draw_box(win, y, x, h, w, pair):
    attr = curses.color_pair(pair)
    # corners
    safe_addch(win, y,     x,     curses.ACS_ULCORNER, attr)
    safe_addch(win, y,     x+w-1, curses.ACS_URCORNER, attr)
    safe_addch(win, y+h-1, x,     curses.ACS_LLCORNER, attr)
    safe_addch(win, y+h-1, x+w-1, curses.ACS_LRCORNER, attr)
    # top/bottom
    for i in range(1, w-1):
        safe_addch(win, y,     x+i, curses.ACS_HLINE, attr)
        safe_addch(win, y+h-1, x+i, curses.ACS_HLINE, attr)
    # sides
    for i in range(1, h-1):
        safe_addch(win, y+i, x,     curses.ACS_VLINE, attr)
        safe_addch(win, y+i, x+w-1, curses.ACS_VLINE, attr)

def draw_panel_box(win, y, x, h, w, title, pair):
    draw_box(win, y, x, h, w, pair)
    if title:
        safe_addstr(win, y, x+2, f" {title} ", curses.color_pair(pair) | curses.A_BOLD)

# ═══════════════════════════════════════════════════════════════════════════════
# MAIN TUI
# ═══════════════════════════════════════════════════════════════════════════════
def main(stdscr):
    global sim_latency, sim_loss, sim_jitter
    global current_dir
    global pred_x, pred_y

    curses.curs_set(0)
    stdscr.nodelay(True)
    stdscr.keypad(True)          # ← this makes arrow keys work properly
    curses.start_color()
    curses.use_default_colors()

    # Color pairs
    curses.init_pair(1, curses.COLOR_YELLOW,  -1)   # coins / title
    curses.init_pair(2, curses.COLOR_GREEN,   -1)   # me / good
    curses.init_pair(3, curses.COLOR_CYAN,    -1)   # info / other players
    curses.init_pair(4, curses.COLOR_RED,     -1)   # bad stats
    curses.init_pair(5, curses.COLOR_MAGENTA, -1)   # borders / hints
    curses.init_pair(6, curses.COLOR_WHITE,   -1)   # normal text
    curses.init_pair(7, curses.COLOR_BLUE,    -1)   # arena border

    C_TITLE  = curses.color_pair(1) | curses.A_BOLD
    C_ME     = curses.color_pair(2) | curses.A_BOLD
    C_GOOD   = curses.color_pair(2)
    C_INFO   = curses.color_pair(3)
    C_BAD    = curses.color_pair(4)
    C_BORDER = curses.color_pair(5)
    C_OTHER  = curses.color_pair(3)
    C_COIN   = curses.color_pair(1)
    C_ARENA  = curses.color_pair(7)
    C_NORMAL = curses.color_pair(6)

    def stat_attr(v, warn, bad):
        return C_BAD if v > bad else (curses.color_pair(1) if v > warn else C_GOOD)

    # Join
    send_pkt(make_join(args.name))

    # Wait for welcome
    deadline = time.time() + 5
    while not connected and time.time() < deadline:
        time.sleep(0.05)
    if not connected:
        curses.endwin()
        print(f"✗ Cannot connect to {args.host}:{args.port}")
        sys.exit(1)

    sim_vals = [0, 0, 0]
    sim_idx  = 0

    while True:
        # ── Input ──
        key = stdscr.getch()

        if   key == curses.KEY_UP    or key == ord('w') or key == ord('W'):
            current_dir["dx"] = 0;  current_dir["dy"] = -1
        elif key == curses.KEY_DOWN  or key == ord('s') or key == ord('S'):
            current_dir["dx"] = 0;  current_dir["dy"] =  1
        elif key == curses.KEY_LEFT  or key == ord('a') or key == ord('A'):
            current_dir["dx"] = -1; current_dir["dy"] =  0
        elif key == curses.KEY_RIGHT or key == ord('d') or key == ord('D'):
            current_dir["dx"] =  1; current_dir["dy"] =  0
        elif key == ord(' '):
            current_dir["dx"] = 0;  current_dir["dy"] =  0
        elif key == ord('\t'):
            sim_idx = (sim_idx + 1) % 3
        elif key == ord('+') or key == ord('='):
            if   sim_idx == 0: sim_vals[0] = min(500, sim_vals[0] + 10)
            elif sim_idx == 1: sim_vals[1] = min(80,  sim_vals[1] + 5)
            elif sim_idx == 2: sim_vals[2] = min(200, sim_vals[2] + 10)
            sim_latency, sim_loss, sim_jitter = sim_vals
        elif key == ord('-'):
            if   sim_idx == 0: sim_vals[0] = max(0, sim_vals[0] - 10)
            elif sim_idx == 1: sim_vals[1] = max(0, sim_vals[1] - 5)
            elif sim_idx == 2: sim_vals[2] = max(0, sim_vals[2] - 10)
            sim_latency, sim_loss, sim_jitter = sim_vals
        elif key == ord('q') or key == ord('Q'):
            if my_id:
                send_pkt(make_leave(my_id))
            break

        # ── Layout ──
        stdscr.erase()
        H, W = stdscr.getmaxyx()

        # Dimensions
        ARENA_COLS  = arena_w + 2   # inner + border
        ARENA_ROWS  = arena_h + 2
        PANEL_W     = min(30, W - ARENA_COLS - 3)
        PANEL_X     = ARENA_COLS + 2
        STATS_H     = 14
        SIM_H       = 7
        LB_H        = min(12, H - STATS_H - SIM_H - 2)

        # ── Title bar ──
        title = "COIN COLLECTOR  —  RAW UDP"
        safe_addstr(stdscr, 0, max(0, W//2 - len(title)//2),
                    title, C_TITLE)
        sub = f"Server {args.host}:{args.port}   You: {args.name}  [{my_id or '...'}]"
        safe_addstr(stdscr, 1, max(0, W//2 - len(sub)//2), sub, C_INFO)

        # ── Arena box ──
        AY = 2   # arena top row
        AX = 0   # arena left col
        draw_panel_box(stdscr, AY, AX, ARENA_ROWS, ARENA_COLS, "ARENA", 7)

        # Snapshot state
        with state_lock:
            gs = dict(game_state)

        # Draw coins
        for c in gs.get("coins", {}).values():
            cx = AX + 1 + int(round(c["x"] * (arena_w - 1) / max(arena_w, 1)))
            cy = AY + 1 + int(round(c["y"] * (arena_h - 1) / max(arena_h, 1)))
            cx = max(AX+1, min(AX+ARENA_COLS-2, cx))
            cy = max(AY+1, min(AY+ARENA_ROWS-2, cy))
            safe_addch(stdscr, cy, cx, ord('$'), C_COIN)

        # Draw other players
        for pid, p in gs.get("players", {}).items():
            if pid == my_id:
                continue
            px = AX + 1 + int(round(p["x"] * (arena_w - 1) / max(arena_w, 1)))
            py = AY + 1 + int(round(p["y"] * (arena_h - 1) / max(arena_h, 1)))
            px = max(AX+1, min(AX+ARENA_COLS-2, px))
            py = max(AY+1, min(AY+ARENA_ROWS-2, py))
            safe_addch(stdscr,  py, px, ord('@'), C_OTHER)
            safe_addstr(stdscr, py, px+1, p["name"][:5], C_OTHER)

        # Draw me (predicted position)
        with pred_lock:
            mpx = pred_x
            mpy = pred_y
        mx = AX + 1 + int(round(mpx * (arena_w - 1) / max(arena_w, 1)))
        my = AY + 1 + int(round(mpy * (arena_h - 1) / max(arena_h, 1)))
        mx = max(AX+1, min(AX+ARENA_COLS-2, mx))
        my = max(AY+1, min(AY+ARENA_ROWS-2, my))
        safe_addch(stdscr,  my, mx, ord('@'), C_ME)
        safe_addstr(stdscr, my, mx+1, args.name[:5], C_ME)

        # ── Right panels ──
        if PANEL_X + PANEL_W < W:

            # NET STATS panel
            draw_panel_box(stdscr, AY, PANEL_X, STATS_H, PANEL_W, "NET STATS  UDP", 5)
            r = AY + 1
            pe = pred_err_acc / pred_err_count if pred_err_count else 0
            rows = [
                (f"  RTT avg  {rtt:>7.1f} ms",   stat_attr(rtt, 80, 150)),
                (f"  RTT min  {(rtt_min if rtt_min != float('inf') else 0):>7.1f} ms", C_GOOD),
                (f"  RTT max  {rtt_max:>7.1f} ms", stat_attr(rtt_max, 100, 200)),
                (f"  Jitter   {jitter:>7.1f} ms",  stat_attr(jitter, 20, 50)),
                (f"  Pkt Sent {pkts_sent:>7}",     C_NORMAL),
                (f"  Pkt Recv {pkts_recv:>7}",     C_NORMAL),
                (f"  OOO Pkts {out_of_order:>7}",  stat_attr(out_of_order, 5, 20)),
                (f"  Tick/s   {tick_rate:>6} Hz",  C_GOOD),
                (f"  Pred Err {pe:>7.2f} px",      stat_attr(pe, 1, 3)),
                (f"  Loss sim {sim_vals[1]:>6} %",  C_NORMAL),
            ]
            for txt, attr in rows:
                safe_addstr(stdscr, r, PANEL_X+1, txt[:PANEL_W-2], attr)
                r += 1

            # NET SIM panel
            SIM_Y = AY + STATS_H + 1
            draw_panel_box(stdscr, SIM_Y, PANEL_X, SIM_H, PANEL_W, "NET SIM", 5)
            labels = ["Latency", "Loss   ", "Jitter "]
            units  = ["ms", "% ", "ms"]
            for i in range(3):
                arrow = ">" if i == sim_idx else " "
                hi    = C_TITLE if i == sim_idx else C_NORMAL
                line  = f"  {arrow} {labels[i]}: {sim_vals[i]:>4} {units[i]}"
                safe_addstr(stdscr, SIM_Y+1+i, PANEL_X+1, line[:PANEL_W-2], hi)
            safe_addstr(stdscr, SIM_Y+4, PANEL_X+1,
                        "  TAB=select  +/-=value"[:PANEL_W-2], C_BORDER)

            # LEADERBOARD panel
            LB_Y = SIM_Y + SIM_H + 1
            if LB_Y + 3 < H:
                actual_lb_h = min(LB_H, H - LB_Y - 1)
                draw_panel_box(stdscr, LB_Y, PANEL_X, actual_lb_h, PANEL_W, "LEADERBOARD", 5)
                ps = sorted(gs.get("players", {}).values(),
                            key=lambda p: p["score"], reverse=True)
                for i, p in enumerate(ps[:actual_lb_h-2]):
                    star = "*" if p["id"] == my_id else " "
                    attr = C_ME if p["id"] == my_id else C_NORMAL
                    line = f"  {star}{i+1}. {p['name'][:8]:<8} {p['score']:>5}"
                    safe_addstr(stdscr, LB_Y+1+i, PANEL_X+1, line[:PANEL_W-2], attr)

        # ── Bottom hint ──
        hint = " W/A/S/D or ARROWS=move   SPACE=stop   TAB=sim   +/-=adjust   Q=quit "
        safe_addstr(stdscr, H-1, max(0, W//2 - len(hint)//2), hint, C_BORDER)

        stdscr.refresh()
        time.sleep(0.033)

# ═══════════════════════════════════════════════════════════════════════════════
# ENTRY
# ═══════════════════════════════════════════════════════════════════════════════
if __name__ == "__main__":
    threading.Thread(target=recv_loop,       daemon=True).start()
    threading.Thread(target=ping_loop,       daemon=True).start()
    threading.Thread(target=input_send_loop, daemon=True).start()
    curses.wrapper(main)
