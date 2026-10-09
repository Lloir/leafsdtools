#!/usr/bin/env python3
"""Analyse a QY8 NAND dump made by Leaf SD Tools ("Read NAND").

Standard library only. Read-only: it never modifies the dump.

    python3 analyze_nand.py nand_XXXXXXXX_XXXXXXXX.bin [-o outdir]

What it does
  1. Block map  - classifies every 128 KB block (erased / data) and prints the
                  contiguous regions, so you can see how the NAND is laid out.
  2. WinCE ROM  - finds ROMHDR structures (the 'ECEC' signature at image
                  offset 0x40), then lists every module (DLL/EXE) and file in
                  the OS image by name, with sizes.
  3. Keywords   - searches the whole dump (ASCII and UTF-16) for USB, audio,
                  video decoder, Bluetooth and phone-projection related terms.
  4. Writes     - report.txt, modules.txt, strings.txt into the output dir.

The module list is the most useful output for the Android Auto question: it
shows which USB host / audio / video drivers the OS image actually contains.
"""
import argparse
import os
import re
import struct
import sys
from collections import Counter

BLOCK = 0x20000  # 128 KB, QY8 (NEW_NAV) read size used by ReadNAND.cpp

KEYWORDS = [
    # USB
    "usbd", "usbhost", "ehci", "ohci", "uhci", "musb", "usbotg", "usbclient",
    "usbfn", "mass storage", "usbmass", "usbhid", "usbser", "rndis",
    # phone projection / accessory protocols
    "android", "carplay", "iap", "ipod", "aoa", "accessory", "mirrorlink",
    "wifi", "wlan", "bluetooth", "btd", "bthport",
    # media
    "h264", "h.264", "avc", "mpeg4", "vpu", "decoder", "ffmpeg", "directshow",
    "dshow", "wmvdecmod", "audio", "wavedev", "wavapi",
    # misc interesting
    "openssl", "ssleay", "schannel", "protobuf",
]


def blocks(data):
    for off in range(0, len(data), BLOCK):
        yield off, data[off:off + BLOCK]


def block_map(data, out):
    kinds = []
    for off, b in blocks(data):
        if len(b) == b.count(0xFF):
            kinds.append("erased")
        elif len(b) == b.count(0):
            kinds.append("zero")
        else:
            kinds.append("data")
    out.append("== BLOCK MAP (%d blocks of 0x%X) ==" % (len(kinds), BLOCK))
    start = 0
    for i in range(1, len(kinds) + 1):
        if i == len(kinds) or kinds[i] != kinds[start]:
            out.append("  0x%08X - 0x%08X  %-6s  (%d blocks)" %
                       (start * BLOCK, i * BLOCK, kinds[start], i - start))
            start = i
    out.append("")


# ---------------------------------------------------------------------------
# WinCE ROMHDR / TOC parsing
# ---------------------------------------------------------------------------
ROMHDR_FIELDS = [
    "dllfirst", "dlllast", "physfirst", "physlast", "nummods", "ulRAMStart",
    "ulRAMFree", "ulRAMEnd", "ulCopyEntries", "ulCopyOffset", "ulProfileLen",
    "ulProfileOffset", "numfiles",
]
# Only the stable leading fields are parsed; later ROMHDR fields vary by CE version.
STABLE_FMT = "<IIIIIIIIIIIII"  # up to numfiles
STABLE_SIZE = struct.calcsize(STABLE_FMT)

TOC_SIZE = 32    # dwFileAttributes, ftTime(8), nFileSize, lpszFileName, E32, O32, Load
FILE_SIZE = 28   # attrs, ftTime(8), nRealFileSize, nCompFileSize, lpszFileName, load


def cstr(data, off, maxlen=260):
    if off < 0 or off >= len(data):
        return None
    end = data.find(b"\0", off, off + maxlen)
    if end < 0:
        return None
    s = data[off:end]
    if not s or not all(32 <= c < 127 for c in s):
        return None
    return s.decode("ascii")


def parse_rom(data, ecec_off, out):
    """Try to parse the ROM image whose 'ECEC' marker is at ecec_off."""
    image_start = ecec_off - 0x40
    if image_start < 0:
        return None
    romhdr_virt, = struct.unpack_from("<I", data, ecec_off + 4)

    # Image is stored flat: file = image_start + (virt - physfirst).
    # physfirst is inside ROMHDR itself, so test candidate bases until the
    # header found at the implied position agrees with the candidate.
    for step in range(0, len(data), 0x1000):
        base = (romhdr_virt & ~0xFFF) - step
        hdr_file = image_start + (romhdr_virt - base)
        if hdr_file < 0 or hdr_file + STABLE_SIZE > len(data):
            continue
        vals = struct.unpack_from(STABLE_FMT, data, hdr_file)
        h = dict(zip(ROMHDR_FIELDS, vals))
        if h["physfirst"] != base:
            continue
        if not (0 < h["nummods"] < 2000 and 0 <= h["numfiles"] < 20000):
            continue
        if h["physlast"] <= h["physfirst"]:
            continue
        return base, hdr_file, h
    return None


def dump_rom(data, ecec_off, parsed, out, modules_out):
    base, hdr_file, h = parsed
    image_start = ecec_off - 0x40

    def v2f(v):
        return image_start + (v - base)

    out.append("== ROM image @ file 0x%X ==" % image_start)
    out.append("  physfirst=0x%08X physlast=0x%08X (%d KB)" %
               (h["physfirst"], h["physlast"], (h["physlast"] - h["physfirst"]) // 1024))
    out.append("  modules=%d files=%d RAM 0x%08X-0x%08X" %
               (h["nummods"], h["numfiles"], h["ulRAMStart"], h["ulRAMEnd"]))

    toc = hdr_file + 0x4C  # TOC follows the 0x4C-byte ROMHDR on common builds
    # Locate the TOC robustly: the first TOCentry's name pointer must resolve
    # to a printable string.
    found = False
    for delta in range(0x40, 0x90, 4):
        t = hdr_file + delta
        if t + TOC_SIZE > len(data):
            break
        name_ptr, = struct.unpack_from("<I", data, t + 16)
        if cstr(data, v2f(name_ptr)):
            toc = t
            found = True
            break
    if not found:
        out.append("  (could not locate the module table; header layout differs)")
        return

    n = h["nummods"]
    modules = []
    for i in range(n):
        t = toc + i * TOC_SIZE
        attrs, = struct.unpack_from("<I", data, t)
        size, name_ptr = struct.unpack_from("<II", data, t + 12)
        name = cstr(data, v2f(name_ptr)) or "?"
        modules.append((name, size, attrs))

    files_off = toc + n * TOC_SIZE
    files = []
    for i in range(h["numfiles"]):
        t = files_off + i * FILE_SIZE
        if t + FILE_SIZE > len(data):
            break
        attrs, = struct.unpack_from("<I", data, t)
        real, comp, name_ptr = struct.unpack_from("<III", data, t + 12)
        name = cstr(data, v2f(name_ptr)) or "?"
        files.append((name, real, comp))

    out.append("  %d modules, %d files parsed (see modules.txt)" % (len(modules), len(files)))
    modules_out.append("# ROM image @ file 0x%X" % image_start)
    modules_out.append("## modules (DLL/EXE)")
    for name, size, attrs in sorted(modules, key=lambda m: m[0].lower()):
        modules_out.append("%-40s %10d" % (name, size))
    modules_out.append("## files")
    for name, real, comp in sorted(files, key=lambda f: f[0].lower()):
        modules_out.append("%-40s %10d (compressed %d)" % (name, real, comp))
    modules_out.append("")


def find_roms(data, out, modules_out):
    out.append("== WINCE ROM IMAGES ==")
    count = 0
    for m in re.finditer(b"ECEC", data):
        off = m.start()
        if off < 0x40:
            continue
        try:
            parsed = parse_rom(data, off, out)
        except struct.error:
            parsed = None
        if parsed:
            count += 1
            dump_rom(data, off, parsed, out, modules_out)
        else:
            # Help diagnose images the parser does not understand yet.
            rv = struct.unpack_from("<I", data, off + 4)[0] if off + 8 <= len(data) else 0
            out.append("  ECEC at 0x%X (image start 0x%X): ROMHDR ptr 0x%08X, could not parse" %
                       (off, off - 0x40, rv))
            start = max(off - 0x40, 0)
            for i in range(0, 0x80, 16):
                row = data[start + i:start + i + 16]
                out.append("    %08X  %s" % (start + i, row.hex(" ")))
    if not count:
        out.append("  No parseable ROMHDR found. The OS may be stored compressed or in")
        out.append("  a vendor container; the keyword search below still works.")
    # Image labels such as G114ELNI.112 (OS) / G214ELNI.112 (navigation app).
    for m in re.finditer(rb"G[0-9]{3}[A-Z]{4}\.[0-9]{2,4}", data):
        out.append("  image label %s at 0x%X" % (m.group().decode(), m.start()))
        start = max(m.start() - 0x20, 0)
        for i in range(0, 0x60, 16):
            out.append("    %08X  %s" % (start + i, data[start + i:start + i + 16].hex(" ")))
    for m in re.finditer(rb"B000FF\n", data):
        out.append("  B000FF record header at 0x%X" % m.start())
    out.append("")


# ---------------------------------------------------------------------------
# Keyword / string search
# ---------------------------------------------------------------------------
def ascii_strings(data, minlen=5):
    for m in re.finditer(rb"[\x20-\x7e]{%d,}" % minlen, data):
        yield m.start(), m.group().decode("ascii")


def utf16_strings(data, minlen=5):
    for m in re.finditer(rb"(?:[\x20-\x7e]\x00){%d,}" % minlen, data):
        yield m.start(), m.group().decode("utf-16le")


def keyword_search(data, out, outdir):
    out.append("== KEYWORD HITS ==")
    pats = {k: re.compile(re.escape(k), re.I) for k in KEYWORDS}
    hits = {k: [] for k in KEYWORDS}
    all_lines = []
    for source in (ascii_strings, utf16_strings):
        for off, s in source(data):
            if len(s) > 200:
                s = s[:200]
            for k, p in pats.items():
                if p.search(s):
                    hits[k].append((off, s))
                    break
            all_lines.append("%08X %s" % (off, s))
    for k in KEYWORDS:
        if hits[k]:
            out.append("  %-14s %5d hits, e.g. 0x%X %s" %
                       (k, len(hits[k]), hits[k][0][0], hits[k][0][1][:80]))
    out.append("")
    with open(os.path.join(outdir, "strings.txt"), "w") as f:
        f.write("\n".join(all_lines))

    # distinct module-like names across the whole dump, in case the ROM parse failed
    names = Counter()
    for _, s in list(ascii_strings(data)) + list(utf16_strings(data)):
        for n in re.findall(r"[A-Za-z0-9_\-]{2,32}\.(?:dll|exe|sys|drv)", s, re.I):
            names[n.lower()] += 1
    out.append("== DLL/EXE NAMES MENTIONED ANYWHERE (%d distinct) ==" % len(names))
    out.append("  " + ", ".join(sorted(names)))
    out.append("")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dump")
    ap.add_argument("-o", "--outdir", default="nand_analysis")
    args = ap.parse_args()

    with open(args.dump, "rb") as f:
        data = f.read()
    os.makedirs(args.outdir, exist_ok=True)

    out = ["File: %s" % args.dump, "Size: %d bytes (0x%X)" % (len(data), len(data)), ""]
    if len(data) % BLOCK:
        out.append("WARNING: size is not a multiple of 0x%X; layout assumptions may be off.\n" % BLOCK)
    modules_out = []

    block_map(data, out)
    find_roms(data, out, modules_out)
    keyword_search(data, out, args.outdir)

    with open(os.path.join(args.outdir, "report.txt"), "w") as f:
        f.write("\n".join(out))
    with open(os.path.join(args.outdir, "modules.txt"), "w") as f:
        f.write("\n".join(modules_out))
    print("\n".join(out))
    print("Wrote report.txt, modules.txt, strings.txt to %s" % args.outdir)


if __name__ == "__main__":
    sys.exit(main())
