#!/usr/bin/env python3
"""Send one QEMU HMP (human monitor) command line over QMP and print the
reply. Generic version of what ehci_portsc.py/tap.py do inline.

    hmp.py SOCK 'command line'
"""
import json
import socket
import sys


def main():
    sock, cmd = sys.argv[1], sys.argv[2]
    s = socket.socket(socket.AF_UNIX)
    s.connect(sock)
    f = s.makefile("rw")
    json.loads(f.readline())
    f.write(json.dumps({"execute": "qmp_capabilities"}) + "\n")
    f.flush()
    f.readline()
    f.write(json.dumps({"execute": "human-monitor-command",
                         "arguments": {"command-line": cmd}}) + "\n")
    f.flush()
    reply = json.loads(f.readline())
    print(reply.get("return", reply))


if __name__ == "__main__":
    main()
