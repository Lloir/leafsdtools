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

## USB host stack (from the emulator RAM dump + disassembly)

How to get the data: boot the dump in qemu-clarion, `pmemsave 0x08000000 134217728` of
RAM (VA 0x88000000 = PA 0x08000000), then `tools/analyze_ram.py` (module list, flat
module images, export lists). Module images are 32-bit ARM code.

- The nav image has 282 modules; the OS image 94; a small region 17; a 25 MB skin file.
- `mqusbh.dll` (Nov 24 2016 build) is a complete third-party USB host stack, NOT the
  standard Windows CE USBD. It contains OHCI + EHCI drivers, the hub class and an internal
  `usbd*` layer, and EXPORTS its API (110 names). Notable exports:
  `usbdCreatePipe`, `usbdDestroyPipe`, `usbdDataTransfer`, `usbdDeviceRequest`(2),
  `usbdGetDeviceDesc`/`ConfigDesc`/`InterfaceDesc`/`EndpointDesc`, `usbdAbortPipe`,
  `usbdResetPipe`, `usbdClearEndpointStall`, `usbdSetIsoBufPipe`,
  `usbClassDrvInstall`/`Uninstall`, `usbdSetUnknownDeviceHandler`,
  `usbdUnknownDeviceGet`, `usbdSetHubErrorStateHandler`, `usbdSysGetHcInfo`.
- Class drivers are stream drivers that sit on top: `umass2.dll` (MSC_*/USD_*),
  `uheadset.dll` (HSC_*), `ipodusb.dll` (POD_*), `UsbIpodMgr.dll` (UIM_*), `ucdc*.dll`.
  Manager apps: `Usb.exe` (753 KB), `UsbConMngCC.dll` (COM component).
- Verified by disassembly (mqusbh.dll): `usbdSetUnknownDeviceHandler(fn)` stores one
  function pointer and returns 0. The stack later calls `fn()` with no arguments;
  `usbdUnknownDeviceGet(uint32_t *a, uint32_t *b)` then pops one queued record and returns
  its two words (0 = ok, -1 = none). This is the natural hook for a phone (Android) that no
  class driver claims. What `a` and `b` mean is not yet determined.
- Plan sketch: handler -> fetch device -> send the Android accessory (AOA) vendor requests
  with `usbdDeviceRequest` -> phone re-enumerates -> claim the accessory interface with a
  class driver registered through `usbClassDrvInstall` -> bulk IN/OUT with `usbdDataTransfer`.
  Function signatures still have to be recovered from the existing class drivers.
- No H.264 decoder and no Wi-Fi driver in the image; TLS support unknown (schannel present).
- Open: how to get our own driver loaded in kernel mode without editing the ROM, and
  whether the emulator's EHCI model can pass a real phone through (libusb is enabled in the
  build) so the protocol can be developed on a PC first.

### Recovered call signatures (from ucdc.dll disassembly, tools/qy8/usb_callsites.py)

- `int usbClassDrvInstall(uint8 classId, uint8 subClassId, uint16 vendorId, uint16 productId,
   fn cb1 ... fn cb11)` : 4 register arguments (r0-r3) plus eleven callback pointers on the
  stack; returns a class index, -1 on failure. ucdc.dll reads class/subclass/vid/pid from
  its registry settings (defaults class 2, subclass 2, 0, 0 = CDC ACM). Some callbacks
  create/abort pipes and issue control requests (set/get line coding). Roles of the 11
  callbacks are not yet mapped.
- `ucdc.dll` imports `usbdCreatePipe`, `usbdDataTransfer`, `usbdDeviceRequest`,
  `usbdAbortPipe`, `usbdResetPipe`, `usbdClearEndpointStall`, `usbdGetConfigDesc`,
  `usbdGetEndpointAddress`, `usbdPipeToDevice/Interface/Endpoint` and fd-info helpers
  (`usbdGetNewFdInfo`, `usbdSetFdInfo`, `usbdIndexToFdInfo`). A bulk CDC data driver is
  therefore the closest template for an Android accessory driver (bulk in + bulk out).

## Getting our code to run: built-in script hooks (strings in the nav image)

`CMaintenanceCtl::MaintenanceInit` (MaintenanceCC.dll, probably) references these paths,
each in four locations (`\USB Disk`, `\USB Disk2`, `\SystemSD`, `\UserSD`):

- `EnableDbgShell.sh`   (presence probably enables the debug shell)
- `RunProgram.s`        (a script that names programs to start)

Nearby: `Launch Process Create!!! -> %s`, `Launch Process Create Success!!!`,
`Launch Process Not Create!!! -> %s`, `Autorun information = %d`. If `RunProgram.s` works
as the names suggest, a text file on a USB stick or SD card runs our executable at boot
with no firmware edit. Still to determine: file format, when it is read (boot / device
insertion / maintenance mode only), and which volume names map to which physical slot.
Next step: extract MaintenanceCC.dll and AppLaunch.dll with tools/analyze_ram.py and read
the code that opens RunProgram.s.

### MaintenanceInit traced (disassembly of the extracted MaintenanceCC.dll)

Right after boot, `MaintenanceInit` builds an 8-entry table of all the
`EnableDbgShell.sh` / `RunProgram.s` paths (one per `\USB Disk`, `\USB Disk2`,
`\SystemSD`, `\UserSD`), then loops over it: for each entry it calls a check
function (file offset 0x4fce4 in the extracted image) and, if that returns
nonzero, a second function (0x4fb18), then marks the slot done. From how the
table is built, the `\USB Disk` entries look like the ones checked by
default; the others read as fallbacks. Not yet confirmed: what the two called
functions actually do (very plausibly "does file exist" / "run it"), and the
exact content format `RunProgram.s` expects.

### Test harness: a virtual USB stick for the emulator

qemu-clarion's EHCI/OHCI are QEMU's real USB host models (not just register
stubs), so a FAT-formatted raw image can be attached as a USB mass storage
device on the unit's own USB port, in the emulator, with no car involved:

- `tools/qy8/make_test_stick.sh OUT.img ['RunProgram.s line']` builds a FAT16
  image with an empty `EnableDbgShell.sh` and a one-line `RunProgram.s` at its
  root. Needs `mkfs.vfat` + `mcopy` (Arch: `dosfstools` `mtools`). NOT run
  against a real build here (this container has neither tool); only
  `bash -n` syntax-checked.
- `qy8-run.sh` now takes `USBSTICK=path/to/image.img` and attaches it as
  `-device usb-storage` on the emulated unit's host port (on a copy, so the
  guest can scribble on it freely).

Next: boot with a test stick, watch console.log for any reaction when the
drive appears, to learn the real file format and confirm the hook is live
(this has not been run yet).

### MaintenanceInit's check() function, traced

File offset 0xfce4 in the extracted MaintenanceCC.dll (vbase 0x42A40000):
`CreateFile(path, GENERIC_READ, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0)`,
then (if the handle is valid and a nonzero size was requested) a read of the raw
file bytes into a caller buffer, then CloseHandle, returning 1 if the file
existed. No encoding assumption is baked in at this layer; whatever interprets
the buffer afterward (`launch`, 0xfb18) hasn't been traced yet.

### Emulator USB limitation found (qemu-clarion, this fork's EHCI model)

Confirmed via tools/qy8/ehci_portsc.py, reading PORTSC/USBSTS directly: a
usb-storage device attached via `-device`/`device_add` lands in the port
already CurrentConnectStatus=1 AND PortEnabled=1, with BOTH change bits
(ConnectStatusChange, PortEnableChange) and USBSTS.PCD left at 0 — i.e. no
insert edge/interrupt ever fires for the guest. Tried both at cold boot and
via hot device_del/device_add; same result both times. The guest's USB driver
(mqusbh.dll) never reads the device descriptor (checked via RAM string search
for "QEMU USB MSD", absent both times) and nothing appears in console.log.

This reads as a QEMU EHCI *emulation* gap (devices presented pre-enabled
instead of a real insert sequence), not evidence against the mechanism on
real hardware: real QY8 units are well known to support USB flash drives for
music, and umass2.dll's class-driver registration (traced via
usb_callsites.py: class=8, subclass 5/6, vendorId=0 productId=0 — a wildcard,
no VID/PID filtering) confirms generic mass-storage devices are accepted.

Next: untestable further in the emulator without attaching a live debugger to
force the port-change bits by hand (not attempted — diminishing returns).
The decisive test for RunProgram.s / EnableDbgShell.sh now needs the real
unit. First real-car test should be a deliberately safe probe (a RunProgram.s
naming a program that does not exist), to learn whether the hook fires at all
before ever pointing it at something real.

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
