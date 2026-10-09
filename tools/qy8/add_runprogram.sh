#!/bin/bash
# Copy a map SD image and drop EnableDbgShell.sh + RunProgram.s into the root
# of each of the three small exFAT partitions found by inspect_card.py
# (part1/part5/part6 on this specific card — offsets below are THIS card's,
# re-run inspect_card.py and edit them if you image a different one).
#
# Usage: add_runprogram.sh CARD.img OUT.img ['RunProgram.s line']
#
# Needs root (loop-mounts exFAT) and kernel exFAT support (Linux 5.4+, or
# exfat-fuse). NOT run/tested in this session — no loop devices available
# here. Run it, then sanity check with `file` / a quick mount + ls before
# trusting it.
set -e
card=${1:?usage: add_runprogram.sh CARD.img OUT.img ['RunProgram.s line']}
out=${2:?usage: add_runprogram.sh CARD.img OUT.img ['RunProgram.s line']}
line=${3:-LEAFSDTOOLS_PROBE.EXE}

# byte offsets from: python3 tools/qy8/inspect_card.py mapsd_stock.img
declare -A PARTS=(
    [part1_524M]=0x391400200
    [part5_42M]=0x3B0800400
    [part6_126M]=0x3B3000600
)

echo "copying $card -> $out (this takes a while for a 16G card)..."
cp --reflink=auto "$card" "$out"

work=$(mktemp -d)
mnt="$work/mnt"
mkdir -p "$mnt"
: > "$work/EnableDbgShell.sh"
printf '%s\r\n' "$line" > "$work/RunProgram.s"

for name in "${!PARTS[@]}"; do
    off=${PARTS[$name]}
    echo "=== $name @ $off ==="
    sudo mount -o loop,offset="$off" "$out" "$mnt"
    sudo cp "$work/EnableDbgShell.sh" "$work/RunProgram.s" "$mnt/"
    ls -la "$mnt" | tail -5
    sudo umount "$mnt"
done

rm -rf "$work"
echo
echo "wrote EnableDbgShell.sh (empty) and RunProgram.s (\"$line\") to the root"
echo "of all three small partitions in $out."
