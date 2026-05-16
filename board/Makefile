SRC := board.c
RC := board.rc
ICON := board.ico
OBJDIR := obj

CC64 := x86_64-w64-mingw32-gcc
CC32 := i686-w64-mingw32-gcc
RC64 := x86_64-w64-mingw32-windres
RC32 := i686-w64-mingw32-windres
SIGN ?= cs

CFLAGS_COMMON := -std=c11 -Wall -Wextra -Wpedantic -O2 -flto -municode -DUNICODE -D_UNICODE -DCOBJMACROS -fno-asynchronous-unwind-tables -fno-unwind-tables
LDFLAGS := -Wl,-s -Wl,--gc-sections -flto -mwindows -ld2d1 -ld3d11 -ldxgi -ldwrite -ldwmapi -luxtheme -lole32 -lwindowscodecs -lgdi32 -ladvapi32 -luuid -lcomctl32
RCFLAGS := --codepage=65001 -O coff

TARGET64 := board64.exe
TARGET32 := board32.exe
RES64 := board64.res
RES32 := board32.res
OBJ64 := $(OBJDIR)/board64.o
OBJ32 := $(OBJDIR)/board32.o
LEGACY_OBJ64 := board64.o
LEGACY_OBJ32 := board32.o
SIGN_AND_WARN = status=0; $(SIGN) "$@" || status=$$?; if [ $$status -ne 0 ]; then echo "Warning: code signing failed for $@ (exit $$status)" >&2; else touch "$@"; fi

all: $(TARGET64) $(TARGET32)

$(TARGET64): $(OBJ64) $(RES64)
	$(CC64) $(CFLAGS_COMMON) $(OBJ64) $(RES64) -o $@ $(LDFLAGS)
	@$(SIGN_AND_WARN)

$(TARGET32): $(OBJ32) $(RES32)
	$(CC32) $(CFLAGS_COMMON) $(OBJ32) $(RES32) -o $@ $(LDFLAGS)
	@$(SIGN_AND_WARN)

$(OBJ64): $(SRC) d2d1_3_compat.h | $(OBJDIR)
	$(CC64) $(CFLAGS_COMMON) -c $< -o $@

$(OBJ32): $(SRC) d2d1_3_compat.h | $(OBJDIR)
	$(CC32) $(CFLAGS_COMMON) -c $< -o $@

$(OBJDIR):
	mkdir -p $@

$(RES64): $(RC) $(ICON) board.h board.manifest
	$(RC64) $(RCFLAGS) $(RC) -o $@

$(RES32): $(RC) $(ICON) board.h board.manifest
	$(RC32) $(RCFLAGS) $(RC) -o $@

clean:
	rm -f $(TARGET64) $(TARGET32) $(RES64) $(RES32) $(LEGACY_OBJ64) $(LEGACY_OBJ32)
	rm -rf $(OBJDIR)

.PHONY: all clean
