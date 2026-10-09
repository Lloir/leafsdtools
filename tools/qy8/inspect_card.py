#!/usr/bin/env python3
"""Read-only inspection of a map SD card image's partition table and each
partition's filesystem type, so files can be placed in the right one without
guessing. Never writes anything.

    inspect_card.py card.img
"""
import struct
import sys

SECTOR = 512


def fs_guess(data):
    if data[3:11] == b"EXFAT   ":
        return "exFAT"
    if data[54:62].rstrip(b" ") in (b"FAT12", b"FAT16"):
        return "FAT16/12 (%s)" % data[54:62].rstrip(b" ").decode()
    if data[82:90].rstrip(b" ") == b"FAT32":
        return "FAT32"
    if data[3:8] == b"NTFS ":
        return "NTFS"
    return "unknown (boot sector sig 0x%02x%02x, oem=%r)" % (data[0], data[1], data[3:11])


def volume_label(data, fstype):
    try:
        if fstype == "exFAT":
            return None  # label is in the root directory, not the boot sector
        if fstype.startswith("FAT32"):
            return data[71:82].decode("ascii", "replace").strip()
        if fstype.startswith("FAT16"):
            return data[43:54].decode("ascii", "replace").strip()
    except Exception:
        pass
    return None


def main():
    path = sys.argv[1]
    with open(path, "rb") as f:
        mbr = f.read(SECTOR)
        print("=== MBR @ %s ===" % path)
        if mbr[510:512] != b"\x55\xaa":
            print("  no 0x55AA boot signature; not a standard MBR")
        for i in range(4):
            e = mbr[446 + i * 16: 446 + i * 16 + 16]
            status, s_chs0, s_chs1, s_chs2, ptype, e_chs0, e_chs1, e_chs2, lba, nsec = \
                struct.unpack_from("<BBBBBBBBII", e)
            if ptype == 0:
                continue
            off = lba * SECTOR
            size = nsec * SECTOR
            f.seek(off)
            boot = f.read(SECTOR)
            fstype = fs_guess(boot)
            label = volume_label(boot, fstype)
            print("  part %d: type=0x%02x  start=0x%X (sector %d)  size=%.2f GB  fs=%s%s" %
                  (i, ptype, off, lba, size / 1e9, fstype,
                   ("  label=%r" % label) if label else ""))


if __name__ == "__main__":
    main()
