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


EXTENDED_TYPES = (0x05, 0x0F, 0x85)


def report(f, n, ptype, lba, nsec, logical=False):
    off = lba * SECTOR
    size = nsec * SECTOR
    f.seek(off)
    boot = f.read(SECTOR)
    fstype = fs_guess(boot)
    label = volume_label(boot, fstype)
    print("  part %-10s type=0x%02x  start=0x%X (sector %d)  size=%.3f GB  fs=%s%s" %
          (n + (" (logical)" if logical else ""), ptype, off, lba, size / 1e9, fstype,
           ("  label=%r" % label) if label else ""))


def main():
    path = sys.argv[1]
    with open(path, "rb") as f:
        mbr = f.read(SECTOR)
        print("=== MBR @ %s ===" % path)
        if mbr[510:512] != b"\x55\xaa":
            print("  no 0x55AA boot signature; not a standard MBR")
        ext_start = None
        for i in range(4):
            e = mbr[446 + i * 16: 446 + i * 16 + 16]
            ptype, lba, nsec = struct.unpack_from("<x3xB3xII", e)
            if ptype == 0:
                continue
            if ptype in EXTENDED_TYPES:
                print("  part %d: type=0x%02x  start sector %d  (extended container, %.3f GB)" %
                      (i, ptype, lba, nsec * SECTOR / 1e9))
                ext_start = lba
                continue
            report(f, str(i), ptype, lba, nsec)

        # walk the EBR chain for logical partitions inside the extended container
        if ext_start is not None:
            ebr_lba = ext_start
            idx = 5
            seen = set()
            while ebr_lba not in seen and idx < 40:
                seen.add(ebr_lba)
                f.seek(ebr_lba * SECTOR)
                ebr = f.read(SECTOR)
                if ebr[510:512] != b"\x55\xaa":
                    break
                e0 = ebr[446:462]
                e1 = ebr[462:478]
                t0, l0, n0 = struct.unpack_from("<x3xB3xII", e0)
                t1, l1, n1 = struct.unpack_from("<x3xB3xII", e1)
                if t0 != 0:
                    report(f, str(idx), t0, ebr_lba + l0, n0, logical=True)
                    idx += 1
                if t1 in EXTENDED_TYPES:
                    ebr_lba = ext_start + l1
                else:
                    break


if __name__ == "__main__":
    main()
