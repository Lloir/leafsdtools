#!/bin/bash
# Boot a Clarion QY8 head unit in the qemu-clarion emulator with the debug
# console on a unix socket (so whole command lines can be sent at once; the
# stock launcher's stdio console treats every keystroke as a separate command).
#
# Usage:  qy8-run.sh NAND CARD
#   NAND  64 MiB dump made by Leaf SD Tools ("nand_*.bin")
#   CARD  image of the unit's map SD card
# Env:
#   QY8_QEMU  path to your qemu-clarion checkout   (default ~/source/qemu-clarion)
#   BOARD     ze0 (2014-2017, default) or ze1
#   TMPDIR    where the working copies go. They are 16 GiB, so NOT a tmpfs /tmp.
#   HEADLESS  1 = no window
#   IMMO      1 = keep the stored immobiliser setting
#   USBSTICK  path to a raw FAT image to attach as a USB mass storage device
#             on the unit's own EHCI/OHCI port (see make_test_stick.sh)
#
# Then, in another terminal:   python3 tools/qy8/qy8_console.py <work dir>/ser.sock
# Everything the unit prints is also saved in <work dir>/console.log.
# This only reads your NAND and card; the guest writes to copies.
set -e
here=${QY8_QEMU:-$HOME/source/qemu-clarion}
nand=${1:-$QY8_NAND}
card=${2:-$QY8_CARD}
board=${BOARD:-ze0}

if [ -z "$nand" ] || [ -z "$card" ]; then
    sed -n '2,19p' "$0" | sed 's/^# \{0,1\}//'
    exit 1
fi
for f in "$nand" "$card" "$here/build/qemu-system-arm"; do
    [ -f "$f" ] || { echo "no such file: $f" >&2; exit 1; }
done
case $board in
    ze0) glsyms=$here/contrib/plugins/qy8gl-g214.syms ;;
    ze1) glsyms=$here/contrib/plugins/qy8gl.syms ;;
    *)   echo "BOARD must be ze0 or ze1" >&2; exit 1 ;;
esac
case $(uname) in
    Darwin) plugin=libqy8gl.dylib; display=cocoa ;;
    *)      plugin=libqy8gl.so;    display=gtk ;;
esac
[ -n "$HEADLESS" ] && display=none

work=$(mktemp -d "${TMPDIR:-/tmp}/qy8.XXXXXX")
cp "$nand" "$work/nand.bin"
# leafsdtools dumps carry VEUP 'Z' (update requested), which boots the update kernel
printf '\x00\x00' | dd of="$work/nand.bin" bs=1 seek=$((0x100010)) conv=notrunc 2>/dev/null
cp --reflink=auto "$card" "$work/card.img"
truncate -s 16G "$work/card.img"

export QY8_GL_FRAME="$work/glframe.bin"
glarg="$here/build/contrib/plugins/$plugin,syms=$glsyms,log=$work/gl.log"
[ -z "$IMMO" ] && glarg="$glarg,cnf=0x0f:0"

stick=()
if [ -n "$USBSTICK" ]; then
    [ -f "$USBSTICK" ] || { echo "no such file: $USBSTICK" >&2; exit 1; }
    # a copy: the guest can write to a real USB stick (e.g. it may set a
    # "notify" flag after reading RunProgram.s), so don't touch the original
    cp "$USBSTICK" "$work/stick.img"
    stick=(-drive if=none,id=stick0,format=raw,file="$work/stick.img"
           -device usb-storage,drive=stick0)
    echo "USB stick: $USBSTICK (as $work/stick.img)"
fi

echo "work dir: $work"
echo "console:  python3 $(dirname "$0")/qy8_console.py $work/ser.sock"
echo "QMP:      $work/q.sock      (Ctrl-C here quits)"
exec "$here/build/qemu-system-arm" \
    -M clarion-qy8,board="$board",dipsw=1,du-dotclk=33333333 \
    -drive if=pflash,format=raw,file="$work/nand.bin" \
    -drive if=sd,index=0,format=raw,file="$work/card.img" \
    "${stick[@]}" \
    -plugin "$glarg" \
    -display "$display" \
    -chardev socket,id=ser0,path="$work/ser.sock",server=on,wait=off,logfile="$work/console.log" \
    -serial chardev:ser0 \
    -qmp unix:"$work/q.sock",server,nowait 2>"$work/qemu.log"
