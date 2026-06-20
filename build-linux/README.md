# Building Leaf SD Tools on Linux (experimental, cegcc / mingw32ce)

This directory provides an **alternative, best-effort** way to compile
`LeafSDTools.exe` on Linux using the **cegcc / mingw32ce** GCC cross-compiler,
which targets **Windows CE on ARM** and emits a real CE PE executable.

> The canonical, fully-supported build is still the eMbedded Visual C++ 4.0
> workspace `LeafSDTools/LeafSDTools.vcw`. This Makefile does not modify or
> replace it. Use eVC4 (on Windows or under Wine) if you need the exact,
> reference binary before flashing — see the caveats below.

---

## 1. Why this is "experimental"

- The project was written for **eMbedded Visual C++ 4.0** (`.vcw`/`.vcp`),
  not GCC. cegcc is a different compiler/CRT, so subtle differences are
  possible (calling conventions for the raw-vtable DirectDraw access in
  `DisplayDirectDraw.cpp`, struct packing, intrinsics, etc.).
- cegcc only targets **ARM** (and MIPS/x86). The **QY8** units are ARM, so the
  default `TARGET=QY8` is the meaningful one. The **QY7** units are **SH4**,
  which cegcc cannot target — `TARGET=QY7` exists only for compile-checking
  the QY7 source set, not for producing a flashable QY7 binary.
- **Validate the output on a backed-up unit before trusting it.** A bad flash
  can brick the head unit. Always take a full `Read NAND` + SHA512 backup first
  (the tool does this automatically now).

---

## 2. Install the toolchain

### Option A — distro package (quickest, if available)
Some distros ship a `mingw32ce` package:
```sh
# Debian/Ubuntu derivatives (package availability varies):
sudo apt-get update
sudo apt-get install mingw32ce
```
After install you should have `arm-mingw32ce-gcc` on your `PATH`:
```sh
arm-mingw32ce-gcc --version
```

### Option B — build cegcc from source
If no package exists, build cegcc (provides `arm-mingw32ce-*`):
- Project: https://github.com/cegcc/cegcc (and the historical SourceForge tree)
- Follow its build instructions, then add its `bin/` to your `PATH`:
```sh
export PATH=/opt/cegcc/bin:$PATH
```

If your toolchain uses a different prefix, pass it explicitly:
```sh
make CROSS=arm-cegcc-       # example
```

---

## 3. Build

```sh
cd build-linux

# QY8 (ARM) — the flashable target:
make

# Build the bootable NK.bin (B000FF format):
make nkbin

# Build a full 128MB SD card image (.img) ready for Etcher:
make image

# Clean:
make clean

# Compile-check the QY7 source set (NOT a flashable SH4 binary):
make TARGET=QY7
```

Output:
```
out/QY8/LeafSDTools.exe   # WinCE Executable
out/QY8/nk.bin           # Bootable container (B000FF)
out/QY8/LeafSDTools.img  # Raw 128MB SD image for Etcher
```

> **Note on `nk.bin`**: The `nkbin` target wraps the `.exe` in a `B000FF` container.
> This is useful for chainloading (e.g., via U-Boot) or if your unit's bootloader
> can jump directly into the PE entry point. It is NOT a full WinCE kernel.

Useful overrides:
| Variable | Default            | Meaning                                    |
|----------|--------------------|--------------------------------------------|
| `CROSS`  | `arm-mingw32ce-`   | cross-toolchain prefix                     |
| `TARGET` | `QY8`              | `QY8` (ARM/DirectDraw) or `QY7` (SH4/GDI)  |
| `WCEVER` | `0x420`            | `_WIN32_WCE` / `UNDER_CE` value            |
| `NKADDR` | `0x80200000`       | `nk.bin` load/execution address             |

---

## 4. What the Makefile compiles

- **Common** to both targets: `ExitUpdateMode, ReadNAND, Checksum, Tweaks,
  SDPinControl, UserSRAM, Flash, LeafSDTools, Logger, StdAfx, Touch`.
- **QY8 only**: `DisplayDirectDraw`, `WriteNAND_QY8`  (defines `QY8XXX`).
- **QY7 only**: `Display`, `WriteNAND`.

This mirrors the per-configuration `Exclude_From_Build` settings in
`LeafSDTools.vcp` (SH4 config = QY7 sources, ARMV4I config = QY8 sources).

The `compat/ceddk.h` shim stands in for the WinCE DDK header if your cegcc SDK
does not ship one. If your SDK *does* provide a real `ceddk.h`, drop
`-Icompat` from `CPPFLAGS` so the genuine header wins.

---

## 5. If the build fails

Common things to resolve, in order:
1. **`arm-mingw32ce-g++ not found`** — toolchain not installed / not on `PATH`.
   Re-check section 2 or set `CROSS=`.
2. **Missing header (e.g. `ddraw.h`, some `winuser` symbol)** — your CE SDK is
   incomplete. Add the missing header to `compat/` (mirroring section 4's
   approach) or point `-I` at your SDK's include dir.
3. **Unresolved symbol at link (e.g. some `coredll` export)** — add the right
   import lib to `LDLIBS` in the Makefile (`-l<name>`).
4. **DirectDraw vtable offsets** (`DisplayDirectDraw.cpp`) — these are
   hard-coded for the QY8 ROM's `IDirectDraw`/surface vtable layout; they are
   not a compile problem but are why on-device testing matters.

---

## 6. Before flashing

1. Build with `make` (QY8).
2. Copy `out/QY8/LeafSDTools.exe` to the SD per the main `README.md`.
3. On the unit, confirm the new build is running:
   - The main menu shows the **TWEAKS** entry.
   - **Read NAND** prints a `SHA512:` line and `Backup verified OK.`
4. Take a full backup, then proceed with Write NAND / partial `nk.bin`
   (which is now structurally validated before flashing).
