#!/usr/bin/env python3
"""Show how an extracted QY8 module calls functions imported from another module.

Works on the flat images written by tools/analyze_ram.py (32-bit ARM code, import
thunks of the form  ldr ip,[pc,#4]; ldr ip,[ip]; bx ip). Needs the `capstone` package.

    usb_callsites.py ucdc.dll.flat.bin 0xEED80000 usbClassDrvInstall usbdCreatePipe

The base address is the module's virtual base (e32_vbase) from analyze_ram's report.
For each named import it prints the call sites with the instructions leading up to the
call, so the argument registers (r0-r3) and stack slots can be read off.
"""
import re
import struct
import sys

import capstone as C

THUNK = (0xE59FC004, 0xE59CC000, 0xE12FFF1C)   # ldr ip,[pc,#4]; ldr ip,[ip]; bx ip


def load(fn, vbase):
    d = open(fn, "rb").read()
    imps = {}
    for off in range(0, len(d) - 20, 4):
        oft, ts, fc, name, ft = struct.unpack_from("<IIIII", d, off)
        if name and name < len(d) and oft and oft < len(d) and ft and fc in (0, 0xFFFFFFFF) and ts == 0:
            s = d[name:name + 40].split(b"\0")[0]
            if re.fullmatch(rb"[A-Za-z0-9_]+\.[dD][lL][lL]", s):
                i = 0
                while True:
                    v = struct.unpack_from("<I", d, oft + 4 * i)[0]
                    if v == 0:
                        break
                    nm = d[v + 2:v + 2 + 60].split(b"\0")[0].decode()
                    if nm:
                        imps[vbase + ft + 4 * i] = nm
                    i += 1
    return d, imps


def thunks(d, imps):
    out = {}
    for a in range(0, len(d) - 16, 4):
        if struct.unpack_from("<III", d, a) == THUNK:
            lit = struct.unpack_from("<I", d, a + 12)[0]
            if lit in imps:
                out[a] = imps[lit]
    return out


def call_sites(d, target):
    res = []
    for a in range(0, len(d) - 4, 4):
        w = struct.unpack_from("<I", d, a)[0]
        if (w & 0x0F000000) in (0x0B000000, 0x0A000000):
            off = w & 0xFFFFFF
            if off & 0x800000:
                off -= 0x1000000
            if a + 8 + off * 4 == target:
                res.append(a)
    return res


def show(d, vbase, st, name, n=2, before=16, after=3):
    md = C.Cs(C.CS_ARCH_ARM, C.CS_MODE_ARM)
    addr = [a for a, nm in st.items() if nm == name]
    if not addr:
        print("#### %s: not imported" % name)
        return
    cs = call_sites(d, addr[0])
    print("#### %s: thunk 0x%x, %d call sites" % (name, addr[0], len(cs)))
    for a in cs[:n]:
        print("--- call at 0x%x" % a)
        for i in md.disasm(d[a - before * 4:a + after * 4 + 4], a - before * 4):
            extra = ""
            if i.mnemonic.startswith("ldr") and ", [pc" in i.op_str and "#" in i.op_str:
                try:
                    off = int(i.op_str.split("#")[1].rstrip("]"), 16)
                    extra = "   ; =0x%08x" % struct.unpack_from("<I", d, i.address + 8 + off)[0]
                except (ValueError, struct.error):
                    pass
            print("%s %x: %-7s %s%s" % (">>" if i.address == a else "  ", i.address, i.mnemonic, i.op_str, extra))


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    d, imps = load(sys.argv[1], int(sys.argv[2], 16))
    st = thunks(d, imps)
    names = sorted(set(st.values()))
    if len(sys.argv) == 3:
        print("%d imported functions with thunks:" % len(names))
        print(", ".join(names))
        return
    for n in sys.argv[3:]:
        show(d, int(sys.argv[2], 16), st, n)


if __name__ == "__main__":
    main()
