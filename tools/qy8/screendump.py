#!/usr/bin/env python3
"""Grab a screenshot of the emulated display over QMP, as a PPM (viewable
with most image tools, or `convert x.ppm x.png`).

    screendump.py SOCK OUT.ppm
"""
import json
import socket
import sys


def main():
    sock, out = sys.argv[1], sys.argv[2]
    s = socket.socket(socket.AF_UNIX)
    s.connect(sock)
    f = s.makefile("rw")
    json.loads(f.readline())
    f.write(json.dumps({"execute": "qmp_capabilities"}) + "\n")
    f.flush()
    f.readline()
    f.write(json.dumps({"execute": "screendump",
                         "arguments": {"filename": out}}) + "\n")
    f.flush()
    reply = json.loads(f.readline())
    if "error" in reply:
        raise RuntimeError("screendump failed: %r" % reply["error"])
    print("wrote %s" % out)


if __name__ == "__main__":
    main()
