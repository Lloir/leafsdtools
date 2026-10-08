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
