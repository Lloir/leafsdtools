#!/usr/bin/env python3
"""Send a single synthetic touch tap into qy8-run.sh's emulator over QMP,
bypassing the GTK window (so it works headless too), and print the wall
time it was sent so the moment can be lined up against guest.log (run
qy8-run.sh with -msg timestamp=on, already the default).

    tap.py SOCK [X] [Y] [--hold MS] [--no-release]

X/Y are screen pixels in the unit's native 800x480 (default: screen center,
400x240). Sends an absolute pointer move to (X,Y), a left button down, a
hold (default 80ms), then a button up -- a normal single tap.

--no-release leaves the button held down (no up event sent) so you can
screendump.py the display mid-press, e.g. to check for any visual feedback
(cursor, button highlight) before deciding touch is fully silent. Release
it afterwards with:  tap.py SOCK X Y --hold 0   (down+immediate up at the
same spot clears the held state either way).
"""
import argparse
import datetime
import json
import socket
import sys

SCREEN_W = 800
SCREEN_H = 480
ABS_MAX = 0x7FFF


def connect(path):
    s = socket.socket(socket.AF_UNIX)
    s.connect(path)
    f = s.makefile("rw")
    json.loads(f.readline())
    f.write(json.dumps({"execute": "qmp_capabilities"}) + "\n")
    f.flush()
    f.readline()
    return f


def send_input_event(f, events):
    f.write(json.dumps({"execute": "input-send-event",
                         "arguments": {"events": events}}) + "\n")
    f.flush()
    reply = json.loads(f.readline())
    if "error" in reply:
        raise RuntimeError("input-send-event failed: %r" % reply["error"])


def abs_events(x, y):
    ax = int(x * ABS_MAX / (SCREEN_W - 1))
    ay = int(y * ABS_MAX / (SCREEN_H - 1))
    return [
        {"type": "abs", "data": {"axis": "x", "value": ax}},
        {"type": "abs", "data": {"axis": "y", "value": ay}},
    ]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("sock")
    ap.add_argument("x", type=int, nargs="?", default=SCREEN_W // 2)
    ap.add_argument("y", type=int, nargs="?", default=SCREEN_H // 2)
    ap.add_argument("--hold", type=int, default=80, help="button-down hold time in ms (default 80)")
    ap.add_argument("--no-release", action="store_true", help="leave the button held down, don't send the up event")
    args = ap.parse_args()

    f = connect(args.sock)

    send_input_event(f, abs_events(args.x, args.y))
    send_input_event(f, [{"type": "btn", "data": {"down": True, "button": "left"}}])
    t0 = datetime.datetime.now()
    print("tap DOWN sent at %s  (x=%d y=%d -> abs %s)" %
          (t0.isoformat(sep=" ", timespec="milliseconds"), args.x, args.y,
           abs_events(args.x, args.y)))

    import time
    time.sleep(args.hold / 1000.0)

    if args.no_release:
        print("button left DOWN (no up event sent) -- screendump now if you want,"
              " release later with: tap.py SOCK %d %d --hold 0" % (args.x, args.y))
        return

    send_input_event(f, [{"type": "btn", "data": {"down": False, "button": "left"}}])
    t1 = datetime.datetime.now()
    print("tap UP   sent at %s" % t1.isoformat(sep=" ", timespec="milliseconds"))
    print()
    print("now check guest.log for lines timestamped between %s and a couple"
          " seconds after %s" % (t0.time(), t1.time()))


if __name__ == "__main__":
    main()
