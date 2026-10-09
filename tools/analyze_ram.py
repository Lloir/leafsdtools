#!/usr/bin/env python3
"""Analyse a RAM dump of a QY8 head unit (from the qemu-clarion emulator).

At boot the unit copies its firmware into DDR: virtual 0x88000000 is physical
0x08000000, so in a dump of that range every virtual address in the ROM tables
is simply (virtual - 0x88000000) bytes into the file. That undoes the chunked
layout seen in the NAND file.

Take the dump from the emulator's monitor (QMP socket in the emulator's work dir):

    qmp.py SOCK "stop" "pmemsave 0x08000000 134217728 \\"/path/ram.bin\\"" "cont"

then:

    python3 analyze_ram.py ram.bin -o ramout
    python3 analyze_ram.py ram.bin -o ramout --extract mqusbh.dll UsbConMngCC.dll

Writes ramout/report.txt and ramout/modules.txt, plus ramout/<name>.flat.bin
(and <name>.sections.txt) for every --extract module. Read-only on the dump.
Reuses the ROM parsing code from analyze_nand.py.
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze_nand as an  # noqa: E402

VERSION = "1"
IMAGE_SCN_COMPRESSED = 0x2000


def u32(d, o):
    return struct.unpack_from("<I", d, o)[0]


def find_regions(data, va_base, out, modules_out):
    out.append("== ROM REGIONS IN RAM ==")
    regions = []
    hits = an.scan_romhdrs(data)
    out.append("  header scan: %d candidate(s)" % len(hits))
    for hdr_file, h in hits:
        base = h["physfirst"]
        if not (va_base <= base < va_base + len(data)):
            continue
        image_start = base - va_base          # file = image_start + (v - base)
        loc = an.locate_toc(data, hdr_file, h, base, image_start)
        if not loc:
            continue
        # the header must live inside the region it describes
        hdr_virt = hdr_file + va_base
        if not (h["physfirst"] <= hdr_virt < h["physlast"]):
            continue
        out.append("  region 0x%08X-0x%08X header@virt 0x%08X: %d modules, %d files" %
                   (h["physfirst"], h["physlast"], hdr_virt, h["nummods"], h["numfiles"]))
        if an.dump_rom(data, image_start, (base, hdr_file, h), out, modules_out):
            regions.append((base, hdr_file, h, loc[0]))
    out.append("")
    return regions


def module_table(data, va_base, region):
    """Return [(name, e32_virt, o32_virt, load_virt)] for a region with modules."""
    base, hdr_file, h, toc = region
    out = []
    for i in range(h["nummods"]):
        t = toc + i * an.TOC_SIZE
        if t + an.TOC_SIZE > len(data):
            break
        name_ptr, e32, o32, load = struct.unpack_from("<IIII", data, t + 16)
        name = an.cstr(data, name_ptr - va_base) or "?"
        out.append((name, e32, o32, load))
    return out


def extract(data, va_base, mods, want, outdir, report):
    """Rebuild a flat memory image of one module from its e32/o32 tables."""
    for name, e32, o32, load in mods:
        if name.lower() != want.lower():
            continue
        e = e32 - va_base
        if not (0 <= e < len(data) - 0x40):
            report.append("%s: e32 pointer 0x%08X outside the dump" % (name, e32))
            return
        objcnt, flags = struct.unpack_from("<HH", data, e)
        entry_rva, vbase = struct.unpack_from("<II", data, e + 4)
        vsize = u32(data, e + 20)
        report.append("%s: e32 @0x%08X objcnt=%d flags=0x%04X entry_rva=0x%X vbase=0x%08X vsize=0x%X load=0x%08X" %
                      (name, e32, objcnt, flags, entry_rva, vbase, vsize, load))
        if not (0 < objcnt <= 32 and 0 < vsize <= 0x4000000):
            report.append("  implausible e32 values; layout differs, giving up on this module")
            return
        img = bytearray(vsize)
        sec = []
        for k in range(objcnt):
            o = o32 - va_base + k * 24
            o_vsize, o_rva, o_psize, o_dataptr, o_real, o_flags = struct.unpack_from("<6I", data, o)
            comp = bool(o_flags & IMAGE_SCN_COMPRESSED)
            sec.append("  sect %d: rva=0x%X vsize=0x%X psize=0x%X dataptr=0x%08X real=0x%08X flags=0x%08X%s" %
                       (k, o_rva, o_vsize, o_psize, o_dataptr, o_real, o_flags, "  COMPRESSED" if comp else ""))
            src = o_dataptr - va_base
            if comp or o_psize == 0 or not (0 <= src <= len(data) - o_psize) or o_rva + o_psize > vsize:
                continue
            img[o_rva:o_rva + o_psize] = data[src:src + o_psize]
        report.extend(sec)
        path = os.path.join(outdir, "%s.flat.bin" % name)
        with open(path, "wb") as f:
            f.write(img)
        report.append("  wrote %s (%d bytes)" % (path, len(img)))
        return
    report.append("%s: not found in any module table" % want)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dump")
    ap.add_argument("-o", "--outdir", default="ram_analysis")
    ap.add_argument("--va", type=lambda x: int(x, 0), default=0x88000000, help="virtual address of the first byte (default 0x88000000)")
    ap.add_argument("--extract", nargs="*", default=[], help="module names to rebuild as flat images")
    args = ap.parse_args()

    with open(args.dump, "rb") as f:
        data = f.read()
    os.makedirs(args.outdir, exist_ok=True)
    out = ["analyze_ram version %s" % VERSION, "File: %s" % args.dump,
           "Size: %d bytes (0x%X), first byte = virtual 0x%08X" % (len(data), len(data), args.va), ""]
    modules_out = []
    regions = find_regions(data, args.va, out, modules_out)

    report = []
    if args.extract:
        mods = []
        for r in regions:
            if r[2]["nummods"]:
                mods += module_table(data, args.va, r)
        for want in args.extract:
            extract(data, args.va, mods, want, args.outdir, report)
        out.append("== EXTRACT ==")
        out.extend(report)

    with open(os.path.join(args.outdir, "report.txt"), "w") as f:
        f.write("\n".join(out))
    with open(os.path.join(args.outdir, "modules.txt"), "w") as f:
        f.write("\n".join(modules_out))
    print("\n".join(out))
    print("Wrote report.txt and modules.txt to %s" % args.outdir)


if __name__ == "__main__":
    main()
