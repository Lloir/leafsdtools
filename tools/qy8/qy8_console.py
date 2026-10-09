#!/usr/bin/env python3
"""Line-mode console for the qemu-clarion QY8 debug shell.

The unit's debug shell treats every burst of received bytes as one command, so
typing into a raw terminal sends one command per keystroke. This client reads a
whole line from you and sends it in one write (plus CR).

    qy8_console.py SOCK                    interactive; type "00 ti" + Enter
    qy8_console.py SOCK -c "00 ti" "00 showid"     run commands and exit

Lines starting with "!" are not sent; "!quit" exits.
"""
import argparse
import select
import socket
import sys
import time


def connect(path):
    s = socket.socket(socket.AF_UNIX)
    for _ in range(100):
        try:
            s.connect(path)
            return s
        except OSError:
            time.sleep(0.2)
    sys.exit("could not connect to %s (is the emulator running?)" % path)


def drain(s, secs, echo=True):
    out = b""
    end = time.time() + secs
    while time.time() < end:
        r, _, _ = select.select([s], [], [], 0.1)
        if r:
            d = s.recv(65536)
            if not d:
                break
            out += d
            if echo:
                sys.stdout.write(d.decode("latin1"))
                sys.stdout.flush()
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("sock")
    ap.add_argument("-c", "--cmd", nargs="+", help="commands to send, then exit")
    ap.add_argument("-w", "--wait", type=float, default=3.0, help="seconds to listen after each command")
    args = ap.parse_args()

    s = connect(args.sock)
    if args.cmd:
        for c in args.cmd:
            print("=== >> %s ===" % c)
            s.sendall(c.encode() + b"\r")
            drain(s, args.wait)
            print()
        return

    print("connected. Type a command such as '00 ti' and press Enter. '!quit' exits.")
    while True:
        # show whatever the unit printed, then read one line from the user
        drain(s, 0.3)
        try:
            line = input("qy8> ")
        except (EOFError, KeyboardInterrupt):
            print()
            return
        if line.strip() == "!quit":
            return
        if line.startswith("!"):
            continue
        s.sendall(line.encode() + b"\r")
        drain(s, args.wait)


if __name__ == "__main__":
    main()
