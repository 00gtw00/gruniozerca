# =============================================================================
# Gruniożerca DOS — Makefile
# Autor: port DOS, 2024
# Kompilator: DJGPP (GCC dla DOS, 32-bit protected mode / DPMI)
# Cel: DOS 6.22 / FreeDOS, klasa 486
# =============================================================================

# --------------- Konfiguracja kompilatora ------------------------------------
CC      := i586-pc-msdosdjgpp-gcc
AS      := i586-pc-msdosdjgpp-as
STRIP   := i586-pc-msdosdjgpp-strip
HOST_CC := /c/msys64/mingw64/bin/gcc   # kompilator hosta — do narzędzi konwersji

# Ścieżka do DJGPP (Windows: C:\djgpp\djgpp, Linux: /usr/local/djgpp)
DJGPP_DIR ?= C:/djgpp/djgpp

BUILD   := build
SRC     := src
INC     := include
ASSETS  := assets
TOOLS   := tools

CFLAGS  := -O2 -march=i486 -std=gnu99 -Wall -Wextra \
            -I$(INC) \
            -DDOS_BUILD \
            -fno-exceptions -fno-unwind-tables
LDFLAGS := -Wl,-Map,$(BUILD)/grunio.map

# --------------- Pliki źródłowe gry ------------------------------------------
GAME_SRCS := \
    $(SRC)/main.c      \
    $(SRC)/video.c     \
    $(SRC)/timer.c     \
    $(SRC)/input.c     \
    $(SRC)/player.c    \
    $(SRC)/carrot.c    \
    $(SRC)/score.c     \
    $(SRC)/sound.c     \
    $(SRC)/sound_sb.c  \
    $(SRC)/sound_opl.c \
    $(SRC)/sound_speaker.c \
    $(SRC)/config.c    \
    $(SRC)/memory.c    \
    $(SRC)/title.c     \
    $(SRC)/pcx.c       \
    $(SRC)/gameover.c  \
    $(SRC)/hiscore.c   \
    $(SRC)/pack.c

GAME_OBJS := $(patsubst $(SRC)/%.c,$(BUILD)/%.o,$(GAME_SRCS))

# --------------- Narzędzia hosta (konwersja assetów) -------------------------
TOOL_CHR2VGA := $(TOOLS)/chr2vga
TOOL_MUS2OPL := $(TOOLS)/mus2opl
TOOL_MKPACK  := $(TOOLS)/mkpack

# Lista assetów wchodzących do GRUNIO.DAT (w kolejności; bez GRUNIO.SAV/.CFG)
PACK_ASSETS := \
    $(ASSETS)/palette.dat   \
    $(ASSETS)/sprites.dat   \
    $(ASSETS)/title.rad     \
    $(ASSETS)/ingame.rad    \
    $(ASSETS)/gamovr.rad    \
    $(ASSETS)/hiscore.rad   \
    $(ASSETS)/bonus.rad     \
    $(ASSETS)/music_0.opl   \
    $(ASSETS)/music_1.opl   \
    $(ASSETS)/music_2.opl   \
    $(ASSETS)/music_3.opl   \
    $(ASSETS)/menu.pcx      \
    $(ASSETS)/settings.pcx  \
    $(ASSETS)/gameplay.pcx  \
    $(ASSETS)/hiscore.pcx

# =============================================================================
# Cele główne
# =============================================================================

.PHONY: all assets pack clean run run-qemu tools help

all: $(BUILD)/GRUNIO.EXE

assets: tools $(ASSETS)/sprites.dat $(ASSETS)/palette.dat $(ASSETS)/music_0.opl

# pack — buduje GRUNIO.DAT ze wszystkich assetów
pack: $(BUILD)/GRUNIO.DAT

tools: $(TOOL_CHR2VGA) $(TOOL_MUS2OPL) $(TOOL_MKPACK)

# --------------- Kompilacja GRUNIO.EXE ---------------------------------------
$(BUILD)/GRUNIO.EXE: $(GAME_OBJS) | $(BUILD)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^
	$(STRIP) $@
	@echo ">>> Zbudowano: $@"

$(BUILD)/%.o: $(SRC)/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

# --------------- Narzędzia hosta ---------------------------------------------
$(TOOL_CHR2VGA): $(TOOLS)/chr2vga.c
	$(HOST_CC) -O2 -Wall -o $@ $<

$(TOOL_MUS2OPL): $(TOOLS)/mus2opl.c
	$(HOST_CC) -O2 -Wall -o $@ $<

$(TOOL_MKPACK): $(TOOLS)/mkpack.c
	$(HOST_CC) -O2 -Wall -o $@ $<

# --------------- Pakowanie assetów → GRUNIO.DAT ------------------------------
$(BUILD)/GRUNIO.DAT: $(TOOL_MKPACK) $(PACK_ASSETS) | $(BUILD)
	$(TOOL_MKPACK) $@ $(PACK_ASSETS)
	@echo ">>> Spakowano assety: $@"

# --------------- Konwersja assetów -------------------------------------------
$(ASSETS)/sprites.dat $(ASSETS)/palette.dat: $(TOOL_CHR2VGA) ../nes/Gfx/chr.chr | $(ASSETS)
	$(TOOL_CHR2VGA) ../nes/Gfx/chr.chr $(ASSETS)/sprites.dat $(ASSETS)/palette.dat
	@echo ">>> Skonwertowano CHR-ROM → sprites.dat / palette.dat"

$(ASSETS)/music_0.opl: $(TOOL_MUS2OPL) ../nes/Muzyka/title.mus | $(ASSETS)
	$(TOOL_MUS2OPL) ../nes/Muzyka/title.mus   $(ASSETS)/music_0.opl
	$(TOOL_MUS2OPL) ../nes/Muzyka/ingame.mus  $(ASSETS)/music_1.opl
	$(TOOL_MUS2OPL) ../nes/Muzyka/over.mus    $(ASSETS)/music_2.opl
	$(TOOL_MUS2OPL) ../nes/Muzyka/empty.mus   $(ASSETS)/music_3.opl
	@echo ">>> Skonwertowano .mus → .opl"

# --------------- Uruchamianie ------------------------------------------------
run: all
	@echo "Uruchamianie w DOSBox-X..."
	dosbox-x -conf dosbox.conf -c "mount c $(BUILD)" -c "c:" -c "GRUNIO.EXE"

run-qemu: all
	@echo "Uruchamianie w QEMU z FreeDOS..."
	qemu-system-i386 -drive format=raw,file=freedos.img -drive format=raw,file=game.img

# --------------- Pomocnicze --------------------------------------------------
$(BUILD):
	mkdir -p $(BUILD)

$(ASSETS):
	mkdir -p $(ASSETS)

clean:
	rm -rf $(BUILD)/*.o $(BUILD)/*.EXE $(BUILD)/*.map $(BUILD)/*.DAT
	@echo ">>> Wyczyszczono build/"

distclean: clean
	rm -rf $(ASSETS)/*.dat $(ASSETS)/*.opl
	rm -f $(TOOL_CHR2VGA) $(TOOL_MUS2OPL) $(TOOL_MKPACK)

help:
	@echo "Dostępne cele:"
	@echo "  make all        — kompiluje GRUNIO.EXE"
	@echo "  make assets     — konwertuje grafiki i muzykę z NES"
	@echo "  make pack       — buduje GRUNIO.DAT (archiwum assetów)"
	@echo "  make tools      — buduje narzędzia konwersji (hosta)"
	@echo "  make run        — uruchamia w DOSBox-X"
	@echo "  make run-qemu   — uruchamia w QEMU/FreeDOS"
	@echo "  make clean      — czyści build/"
	@echo "  make distclean  — czyści build/ i assets/"
