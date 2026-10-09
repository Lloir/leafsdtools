#!/bin/bash
# Build a small raw FAT16 image with EnableDbgShell.sh and RunProgram.s at its
# root, for USBSTICK=... with qy8-run.sh. Needs mkfs.vfat and mtools (mcopy).
# Arch: sudo pacman -S --needed dosfstools mtools
#
# Usage: make_test_stick.sh OUT.img [RunProgram.s content line]
set -e
out=${1:?usage: make_test_stick.sh OUT.img ['RunProgram.s line']}
line=${2:-Consult.exe}

command -v mkfs.vfat >/dev/null || { echo "need mkfs.vfat (dosfstools)" >&2; exit 1; }
command -v mcopy >/dev/null || { echo "need mcopy (mtools)" >&2; exit 1; }

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
: > "$work/EnableDbgShell.sh"
printf '%s\r\n' "$line" > "$work/RunProgram.s"

rm -f "$out"
truncate -s 64M "$out"
mkfs.vfat -F 16 -n QY8TEST "$out" >/dev/null
mcopy -i "$out" "$work/EnableDbgShell.sh" "$work/RunProgram.s" ::/

echo "wrote $out"
echo "  /EnableDbgShell.sh  (empty)"
echo "  /RunProgram.s       : $line"
echo
echo "Content is a guess at the real file format — the point of this test is to"
echo "see how the unit reacts (or errors) in console.log, not to assume it's right."
