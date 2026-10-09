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

VERSION = "5 (header copies, table search, diagnostics)"
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


def hexrows(data, start, n=0x60):
    start = max(start, 0)
    return ["    %08X  %s" % (start + i, data[start + i:start + i + 16].hex(" "))
            for i in range(0, n, 16) if start + i < len(data)]


def fmt_off(v):
    return "-0x%X" % -v if v < 0 else "0x%X" % v


def sane_header(h):
    """A files-only region has nummods == 0, so only require numfiles then."""
    if not (0 <= h["nummods"] < 5000 and 0 <= h["numfiles"] < 1000000):
        return False
    if h["nummods"] == 0 and h["numfiles"] == 0:
        return False
    if not (0x80000000 <= h["physfirst"] < 0xC0000000):
        return False
    return h["physfirst"] < h["physlast"] <= h["physfirst"] + 0x10000000


def parse_rom(data, ecec_off, out):
    """Try to parse the ROM image whose 'ECEC' marker is at ecec_off."""
    image_start = ecec_off - 0x40
    if image_start < 0 or ecec_off + 12 > len(data):
        return None
    romhdr_virt, romhdr_off = struct.unpack_from("<II", data, ecec_off + 4)

    def check(base, hdr_file, strict):
        if hdr_file < 0 or hdr_file + STABLE_SIZE > len(data):
            return None
        vals = struct.unpack_from(STABLE_FMT, data, hdr_file)
        h = dict(zip(ROMHDR_FIELDS, vals))
        if strict and h["physfirst"] != base:
            return None
        if not sane_header(h):
            return None
        return base, hdr_file, h

    # 1. The word after the ROMHDR pointer is the header's offset inside the
    #    image, which gives the image base directly (works for chained XIP
    #    regions that do not start on a block boundary).
    if 0 < romhdr_off < len(data) and romhdr_virt >= romhdr_off:
        r = check(romhdr_virt - romhdr_off, image_start + romhdr_off, False)
        if r:
            return r

    # 2. Otherwise search for a base whose header agrees with itself.
    for step in range(0, len(data), 0x1000):
        base = (romhdr_virt & ~0xFFF) - step
        r = check(base, image_start + (romhdr_virt - base), True)
        if r:
            return r
    return None


def locate_toc(data, hdr_file, h, base, image_start):
    """Find the module (or file) table after a ROMHDR. Returns (offset, score)."""
    n = h["nummods"]
    esz, name_at = (TOC_SIZE, 16) if n else (FILE_SIZE, 20)
    cnt = n if n else h["numfiles"]
    probe = min(cnt, 8)
    best = None
    for delta in range(0x40, 0x200, 4):
        t = hdr_file + delta
        if t + probe * esz > len(data):
            break
        ok = 0
        for i in range(probe):
            ptr, = struct.unpack_from("<I", data, t + i * esz + name_at)
            if cstr(data, image_start + (ptr - base)):
                ok += 1
        if ok >= max(1, probe * 3 // 4) and (best is None or ok > best[1]):
            best = (t, ok)
            if ok == probe:
                break
    return best


def dump_rom(data, image_start, parsed, out, modules_out):
    base, hdr_file, h = parsed

    def v2f(v):
        return image_start + (v - base)

    out.append("== ROM image (virtual base maps to file %s), header @0x%X ==" %
               (fmt_off(image_start), hdr_file))
    out.append("  physfirst=0x%08X physlast=0x%08X (%d KB)" %
               (h["physfirst"], h["physlast"], (h["physlast"] - h["physfirst"]) // 1024))
    out.append("  modules=%d files=%d RAM 0x%08X-0x%08X" %
               (h["nummods"], h["numfiles"], h["ulRAMStart"], h["ulRAMEnd"]))

    loc = locate_toc(data, hdr_file, h, base, image_start)
    if not loc:
        out.append("  (could not locate the module table; header bytes follow)")
        out.extend(hexrows(data, hdr_file, 0x120))
        return False
    toc = loc[0]

    n = h["nummods"]
    modules = []
    for i in range(n):
        t = toc + i * TOC_SIZE
        if t + TOC_SIZE > len(data):
            break
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
    modules_out.append("# ROM image, header @0x%X, virtual base -> file %s" % (hdr_file, fmt_off(image_start)))
    modules_out.append("## modules (DLL/EXE)")
    for name, size, attrs in sorted(modules, key=lambda m: m[0].lower()):
        modules_out.append("%-40s %10d" % (name, size))
    modules_out.append("## files")
    if len(files) > 3000:
        out.append("  (modules.txt lists only the first 3000 files of this image)")
        files = files[:3000]
    for name, real, comp in sorted(files, key=lambda f: f[0].lower()):
        modules_out.append("%-40s %10d (compressed %d)" % (name, real, comp))
    modules_out.append("")
    return True


def scan_romhdrs(data):
    """Signature-less search for ROMHDR-looking structures (4-byte aligned)."""
    if sys.byteorder != "little" or len(data) % 4:
        return []
    view = memoryview(data).cast("I")
    hits = []
    for i in range(len(view) - 13):
        pf = view[i + 2]
        if not (0x80000000 <= pf < 0xC0000000):
            continue
        pl = view[i + 3]
        if not (pf < pl <= pf + 0x10000000):
            continue
        nm = view[i + 4]
        if nm >= 5000 or not (view[i + 5] <= view[i + 6] <= view[i + 7]):
            continue
        h = dict(zip(ROMHDR_FIELDS, view[i:i + 13]))
        if sane_header(h) and view[i + 5] >= 0x80000000:
            hits.append((i * 4, h))
    return hits


def find_roms(data, out, modules_out):
    out.append("== WINCE ROM IMAGES ==")
    done = set()       # header file offsets already parsed
    eceps = []         # (image_start, ptr, off) of every plausible signature
    count = 0

    for m in re.finditer(b"ECEC", data):
        off = m.start()
        if off < 0x40 or off + 12 > len(data):
            continue
        ptr, hoff = struct.unpack_from("<II", data, off + 4)
        if not (0x80000000 <= ptr < 0xC0000000):
            continue  # not a ROM signature (e.g. counter-like data)
        eceps.append((off - 0x40, ptr, hoff))
        try:
            parsed = parse_rom(data, off, out)
        except struct.error:
            parsed = None
        if parsed:
            count += 1
            done.add(parsed[1])
            dump_rom(data, off - 0x40, parsed, out, modules_out)
        else:
            out.append("  ECEC at 0x%X (image start 0x%X): ROMHDR ptr 0x%08X offset 0x%X, could not parse" %
                       (off, off - 0x40, ptr, hoff))
            out.extend(hexrows(data, off - 0x40, 0x80))
            cand = off - 0x40 + hoff
            out.append("    candidate header at 0x%X:" % cand)
            out.extend(hexrows(data, cand, 0x60))

    # Fallback: find headers without relying on the ECEC marker and match
    # each to a signature whose (pointer - offset) equals its physfirst.
    hits = scan_romhdrs(data)
    out.append("  signature-less header scan: %d candidate(s)" % len(hits))
    for hdr_file, h in hits:
        if hdr_file in done:
            continue
        out.append("  header candidate @0x%X physfirst=0x%08X physlast=0x%08X mods=%d files=%d" %
                   (hdr_file, h["physfirst"], h["physlast"], h["nummods"], h["numfiles"]))
        # Two hypotheses for where the region's virtual base lies in the file:
        # the signature's own offset says this header is the one it points at,
        # or the header is a copy near the start of the region.
        hyps = []
        for e in eceps:
            if e[1] - e[2] == h["physfirst"]:
                hyps.append(hdr_file - e[2])
                hyps.append(e[0])
        best = None
        for img in dict.fromkeys(hyps):
            r = locate_toc(data, hdr_file, h, h["physfirst"], img)
            if r and (best is None or r[1] > best[1]):
                best = (img, r[1])
        if best:
            done.add(hdr_file)
            count += 1
            dump_rom(data, best[0], (h["physfirst"], hdr_file, h), out, modules_out)
        else:
            out.append("    no table found under either hypothesis; header bytes:")
            out.extend(hexrows(data, hdr_file, 0x120))

    if not count:
        out.append("  No parseable ROMHDR found. The OS may be stored compressed or in")
        out.append("  a vendor container; the keyword search below still works.")
    # Image labels such as G114ELNI.112 (OS) / G214ELNI.112 (navigation app).
    for m in re.finditer(rb"G[0-9]{3}[A-Z]{4}\.[0-9]{2,4}", data):
        out.append("  image label %s at 0x%X" % (m.group().decode(), m.start()))
        out.extend(hexrows(data, m.start() - 0x20, 0x60))
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

    out = ["analyze_nand version %s" % VERSION, "File: %s" % args.dump, "Size: %d bytes (0x%X)" % (len(data), len(data)), ""]
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
