# Android Auto on the QY8 - working notes

Goal: Android Auto (own implementation, no third-party paid firmware) on a
QY8 head unit (set QY8252NF, ApplicationB 102 / ApplicationN 112, EU build).

Status: research / feasibility. Nothing here is proven on hardware yet.

## What is known (and from where)

- Gen 1 units (QY7xxx) are Windows CE; some later Gen 2 units run Linux and ship
  with Android Auto / CarPlay natively over USB. Source: Car Hacking Wiki,
  <https://nissanleaf.carhackingwiki.com/index.php/AV_Control_Unit>.
- The map SD holds the map data, a hidden skin partition, and the Windows CE
  firmware image in the unpartitioned space at the start of the card. Gen 1
  cards are password locked (the PIN this tool reads); Gen 2 cards are not.
  (Same source. QY8 itself is not covered there, so check on the real card.)
- Gen 1 units expose a UART debug shell on the 40-pin connector. This tool's
  `SerialLog` / `DEBUGSHELLDLL.DLL` output goes to the same place. Unconfirmed
  for QY8.
- Xanavi's installer (screenshot, not reproduced here) flashes several named NAND
  blocks and writes `\SystemSD\01\40.img`, i.e. it replaces OS blocks from
  update mode, the same machinery as `Flash.cpp` / `WriteNAND_QY8.cpp`.
- The NAND strings scan found `vin.dll` (video in), the DebugShell module,
  `CApi_*` functions and 12 "iAP" strings (iPod accessory protocol) near
  `0xfe59bb`. No Android / USB host driver names were in that filtered scan.
- Android Auto itself: the phone does all the work; the head unit decodes H.264
  video + PCM audio and sends touch/button input. Open-source reference:
  OpenAuto / aasdk (GPLv3, not Google certified). Reports say newer Android
  versions have broken older aasdk-based code, so expect protocol work.
  <https://github.com/f1xpl/openauto>

## Hardware, from the QY8 emulator (albertbm/qemu-clarion, branch qy8-bridge)

Source: <https://github.com/albertbm/qemu-clarion/tree/qy8-bridge> (`README-QY8.md`,
`hw/arm/clarion_qy8.c`). Its memory map was read from the unit's own bootloader.

- SoC: Renesas R8A7778 (R-Car M1A), single Cortex-A9. Windows CE runs XIP from NAND.
- GPU: PowerVR SGX. Display unit, SD hosts (SDHI), I2C, CAN, SCIF serial ports.
- USB host: EHCI + OHCI at 0xFFE70000 with a USB PHY at 0xFFE70800. The firmware's
  USB driver enumerates devices in the emulator. This answers unknown 1 (a USB
  host exists in the OS). Whether we can drive it from our own code is still open.
- The emulator boots the unit from a leafsdtools NAND dump plus a map SD image,
  has a debug shell, touch input and a software UI renderer. Not emulated: map,
  camera video, audio, CAN data.
- Our unit (QY8252NF, EU) should match its ZE0 board (QY8202NA): `BOARD=ze0`.
- No hardware video decoder is mentioned in the emulator. H.264 at 800x480 would
  likely have to be decoded in software on the A9 (to be verified).

## NAND layout findings (QY8252NF, G114ELNI.112 / G214ELNI.112)

From `tools/analyze_nand.py` on a real 64 MB dump (offsets are file offsets):

- `0x000000` boot stage 1, `0x060000` boot stage 2 (each a tiny `nk.exe`).
- `0x1C0000` OS image `G114ELNI.112`: ROM base 0x88000000, 94 modules, 14 files.
  Fully parsed (kernel, gwes, coredll, SDHC, serial_scif, ddraw, gdisub, Camera.exe,
  VIN.dll, waveapic, tomato_RDSTMCDAB_ML (tuner), ...). No USB host driver here.
- `0x1C2AB8` files-only region `ObjSubstance.skn` (25.7 MB UI skin), VA 0x8E100000.
- `0x9064F0` onwards: chunks of the navigation/app image `G214ELNI.112`
  (label at 0xAC0016). Its ROMHDR (copies at 0x9074F0 and 0x27FA5A0):
  physfirst 0x88840000, physlast 0x8B49F1E8 (~45 MB), 282 modules, 43 files,
  CPU type 0x01C2 (Thumb), TOC at header+0x54 with 32-byte entries. A second
  region at 0x8B8D0000 has 17 modules.
- Not solved: the TOC's name/E32 pointers (e.g. first name ptr 0x88AD3FEC) do not map
  linearly to file offsets, so the 282 module names cannot be listed from the file yet.
  The image seems stored as several chunks, each starting with `<name>\0 ... ECEC
  <ptr> <off>`. The nav module bodies themselves (UsbConMngCC, ws2.dll, ...) are
  stored uncompressed.
- The runtime debug log inside the dump shows `mqusbh.dll` loaded at 0xEEE00000.

Ways forward: (a) brute-force the virtual->file mapping against E32 structures;
(b) boot the dump in the qemu-clarion emulator (it already handles G214ELNI.112) and
read the modules from RAM through the debug shell or gdb stub.

## Unknowns that decide feasibility

1. Does the Windows CE image contain a USB **host** stack the app side can use
   (usbd.dll, ehci/ohci drivers)? -> `tools/analyze_nand.py`, `registry.txt`
2. Which SoC, and is there a hardware H.264 decoder reachable from user code?
   Software decode at 800x480 on this CPU may be too slow.
3. Can the OS image or SD firmware area be modified and still boot (signature /
   checksum checks)?
4. How are screen, audio routing and the steering/back keys handed to a
   third-party app in the normal OS?

## Plan

1. Collect data: NAND dump, `registry.txt` + `sysinfo.txt` (Tweaks menu), and the
   first ~64 MB of the map SD (the firmware area).
2. Run `tools/analyze_nand.py` on the NAND and on the SD head. Answer 1-3 above.
3. If USB host + a usable display/audio path exist: build a tiny test app that
   enumerates the USB port and puts the phone in accessory mode.
4. Only then port the AA protocol (TLS, protobuf, channels), then video decode.

## Safety rules

- Back up the NAND (the tool verifies a checksum) and keep an untouched copy of
  the map SD before writing anything.
- Never flash an image that was only checked by eye; verify checksums.
- Don't use a half-working build while driving.
