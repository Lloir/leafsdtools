# ---------------------------------------------------------------------------
# Experimental Linux-hosted build for Leaf SD Tools using the cegcc /
# mingw32ce cross-compiler (GCC that targets Windows CE / ARM).
#
# This is an ALTERNATIVE to the canonical eMbedded Visual C++ 4.0 build
# (LeafSDTools.vcw). It does NOT replace it and does not touch it.
#
# Quick start (see README.md for full details):
#     sudo apt-get install mingw32ce        # or build cegcc from source
#     cd build-linux
#     make                                  # builds the QY8 (ARM) image
#     make TARGET=QY7                        # builds the QY7 (SH4) image*
#
#   * QY7 is an SH4 device. cegcc only targets ARM/MIPS/x86, so a real QY7
#     binary still requires eVC4. The QY7 target here exists only so the
#     ARM-incompatible sources can be substituted for compile-testing.
# ---------------------------------------------------------------------------

# Cross toolchain prefix. Common values:
#   arm-mingw32ce-     (mingw32ce / cegcc, ARM, Windows CE PE output)
CROSS    ?= arm-mingw32ce-

CC       := $(CROSS)gcc
CXX      := $(CROSS)g++
WINDRES  := $(CROSS)windres
STRIP    := $(CROSS)strip

# Which head unit to target: QY8 (ARM, DirectDraw) or QY7 (SH4, GDI-sub).
TARGET   ?= QY8

# Windows CE version the headers/SDK target. mingw32ce normally defines
# _WIN32_WCE itself; override here only if your SDK needs a specific value.
WCEVER   ?= 0x420

SRCDIR   := ../LeafSDTools
OUTDIR   := out/$(TARGET)
BIN      := $(OUTDIR)/LeafSDTools.exe

# --- Source selection ------------------------------------------------------
# Sources compiled for every target.
COMMON_SRC := \
	ExitUpdateMode.cpp \
	ReadNAND.cpp \
	Checksum.cpp \
	Tweaks.cpp \
	SDPinControl.cpp \
	UserSRAM.cpp \
	Flash.cpp \
	LeafSDTools.cpp \
	Logger.cpp \
	StdAfx.cpp \
	Touch.cpp

ifeq ($(TARGET),QY8)
  # QY8XXX: ARM head unit, DirectDraw display + partition-aware QY8 writer.
  TARGET_SRC := DisplayDirectDraw.cpp WriteNAND_QY8.cpp
  TARGET_DEF := -DQY8XXX -DARM -D_ARM_ -DARMV4I
else ifeq ($(TARGET),QY7)
  # QY7XXX: GDI-sub display + classic NAND writer (SH4 in reality).
  TARGET_SRC := Display.cpp WriteNAND.cpp
  TARGET_DEF := -DSHx -DSH4 -D_SH4_
else
  $(error Unknown TARGET '$(TARGET)'. Use QY8 or QY7.)
endif

SRC  := $(COMMON_SRC) $(TARGET_SRC)
OBJ  := $(patsubst %.cpp,$(OUTDIR)/%.o,$(SRC))
RES  := $(OUTDIR)/LeafSDTools.res.o

# --- Flags -----------------------------------------------------------------
# A local shim include dir provides any DDK header (e.g. ceddk.h) that the
# cegcc SDK may not ship. Real SDK headers still take precedence if present.
DEFS := $(TARGET_DEF) \
	-DUNDER_CE=$(WCEVER) -D_WIN32_WCE=$(WCEVER) \
	-DUNICODE -D_UNICODE -DNDEBUG -DWIN32_LEAN_AND_MEAN

CPPFLAGS := -I$(SRCDIR) -Icompat $(DEFS)
CXXFLAGS := -O2 -fno-exceptions -fno-rtti -Wno-write-strings -Wno-multichar
LDFLAGS  := -mwindows
LDLIBS   := -lcoredll -lddraw

# --- Rules -----------------------------------------------------------------
.PHONY: all clean toolcheck

all: toolcheck $(BIN)

toolcheck:
	@command -v $(CXX) >/dev/null 2>&1 || { \
		echo "ERROR: '$(CXX)' not found on PATH."; \
		echo "Install the cegcc/mingw32ce toolchain or set CROSS=<prefix>."; \
		echo "See build-linux/README.md."; \
		exit 1; }

$(OUTDIR):
	mkdir -p $(OUTDIR)

$(OUTDIR)/%.o: $(SRCDIR)/%.cpp | $(OUTDIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(RES): $(SRCDIR)/LeafSDTools.rc | $(OUTDIR)
	$(WINDRES) -I$(SRCDIR) $(DEFS) $< -O coff -o $@

$(BIN): $(OBJ) $(RES)
	$(CXX) $(LDFLAGS) $(OBJ) $(RES) $(LDLIBS) -o $@
	$(STRIP) $@ || true
	@echo ""
	@echo "Built: $(BIN)  (TARGET=$(TARGET))"

clean:
	rm -rf out
