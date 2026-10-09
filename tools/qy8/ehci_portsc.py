#!/usr/bin/env python3
"""Read the QY8's EHCI PORTSC registers straight from the QEMU hardware model,
bypassing the guest entirely (via the HMP "xp" physical-memory-examine command
over QMP). Tells us whether a device is connected at the hardware level even
if the guest's own USB driver never notices it.

    ehci_portsc.py SOCK [EHCI_BASE]

EHCI_BASE defaults to 0xFFE70000 (the QY8's EHCI window, per clarion_qy8.c).
"""
import json
import re
import socket
import sys


def connect(path):
    s = socket.socket(socket.AF_UNIX)
    s.connect(path)
    f = s.makefile("rw")
    json.loads(f.readline())
    f.write(json.dumps({"execute": "qmp_capabilities"}) + "\n")
    f.flush()
    f.readline()
    return f


def xp(f, size, addr):
    cmd = "xp /1x%s 0x%x" % ({1: "b", 2: "h", 4: "w"}[size], addr)
    f.write(json.dumps({"execute": "human-monitor-command",
                         "arguments": {"command-line": cmd}}) + "\n")
    f.flush()
    out = json.loads(f.readline()).get("return", "")
    m = re.findall(r"0x[0-9a-fA-F]+", out)
    if not m:
        raise RuntimeError("unexpected xp reply: %r" % out)
    return int(m[-1], 16)


def main():
    sock = sys.argv[1]
    base = int(sys.argv[2], 0) if len(sys.argv) > 2 else 0xFFE70000
    f = connect(sock)

    caplen = xp(f, 1, base) & 0xFF
    hcsparams = xp(f, 4, base + 4)
    nports = hcsparams & 0xF
    opbase = base + caplen
    print("EHCI @0x%08X: CAPLENGTH=0x%02X HCSPARAMS=0x%08X N_PORTS=%d  (operational @0x%08X)" %
          (base, caplen, hcsparams, nports, opbase))

    usbcmd = xp(f, 4, opbase)
    usbsts = xp(f, 4, opbase + 4)
    print("USBCMD=0x%08X  USBSTS=0x%08X  (RS=%d HCHalted=%d PCD=%d)" %
          (usbcmd, usbsts, usbcmd & 1, (usbsts >> 12) & 1, (usbsts >> 2) & 1))

    for p in range(1, nports + 1):
        v = xp(f, 4, opbase + 0x44 + (p - 1) * 4)
        ccs = v & 1
        csc = (v >> 1) & 1
        ped = (v >> 2) & 1
        pedc = (v >> 3) & 1
        print("PORTSC%d = 0x%08X   CurrentConnectStatus=%d ConnectStatusChange=%d "
              "PortEnabled=%d PortEnableChange=%d" % (p, v, ccs, csc, ped, pedc))


if __name__ == "__main__":
    main()
