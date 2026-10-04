# =============================================================================
# Makefile for Tetris C64
# Requires the cc65 toolchain: https://cc65.github.io/
#
# Targets:
#   all       - Build tetris.prg (default)
#   clean     - Remove generated files
#   run       - Build and launch in VICE emulator (x64sc)
#   disk      - Create a .d64 disk image containing tetris.prg
#
# Tools required:
#   cl65      - cc65 all-in-one compiler/linker
#   x64sc     - VICE Commodore 64 emulator (optional, for 'run' target)
#   c1541     - VICE disk image utility   (optional, for 'disk' target)
# =============================================================================

# --- Toolchain ---------------------------------------------------------------

CC      = cl65
AS      = ca65
LD      = ld65
C1541   = c1541
VICE    = x64sc

# --- Target platform ---------------------------------------------------------

TARGET  = c64

# --- Output files ------------------------------------------------------------

PRG     = tetris.prg
D64     = tetris.d64
DISK_LABEL = TETRIS64

# --- Source files ------------------------------------------------------------

SRCS    = tetris.c

# --- Compiler/linker flags ---------------------------------------------------

#  -O            Enable optimiser (safe for C64 target)
#  -t c64        Compile/link for Commodore 64
#  --standard c99  Use C99 standard
#  -Wl           Pass options to the linker
#  -m tetris.map  Generate a linker map file (useful for debugging)

CFLAGS  = -O \
           -t $(TARGET) \
           --standard c99

LDFLAGS = -t $(TARGET) \
           -m tetris.map

# --- VICE flags (for 'run' target) -------------------------------------------

# -autostart     Load and run the .prg immediately
# -fullscreen    Optional: start in full screen
VICE_FLAGS = -autostart $(PRG)

# =============================================================================
# Build rules
# =============================================================================

.PHONY: all clean run disk help

all: $(PRG)

# Link everything together using cl65 (handles compile + assemble + link)
$(PRG): $(SRCS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

# Remove build artifacts
clean:
	$(RM) $(PRG) tetris.map $(D64) $(RM) -f *.o

# Build and launch in VICE emulator
run: $(PRG)
	$(VICE) $(VICE_FLAGS)

# Create a .d64 disk image with the PRG on it
disk: $(PRG)
	$(C1541) -format "$(DISK_LABEL),01" d64 $(D64) \
	         -write $(PRG) tetris

# =============================================================================
# Help
# =============================================================================

help:
	@echo ""
	@echo "  Tetris C64 — cc65 Makefile"
	@echo "  ================================="
	@echo "  make          Build tetris.prg"
	@echo "  make run      Build and run in VICE (x64sc)"
	@echo "  make disk     Build and wrap in a .d64 disk image"
	@echo "  make clean    Remove generated files"
	@echo ""
	@echo "  Requirements:"
	@echo "    cc65 toolchain  https://cc65.github.io/"
	@echo "    VICE emulator   https://vice-emu.sourceforge.io/  (for 'run'/'disk')"
	@echo ""
