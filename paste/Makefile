SRC := paste.c
RC := paste.rc
OBJDIR := obj

CC64 := x86_64-w64-mingw32-gcc
CC32 := i686-w64-mingw32-gcc
RC64 := x86_64-w64-mingw32-windres
RC32 := i686-w64-mingw32-windres
SIGN ?= cs

CFLAGS_COMMON := -std=c11 -Wall -Wextra -Wpedantic -O2 -flto -municode -DUNICODE -D_UNICODE -DCOBJMACROS -fno-asynchronous-unwind-tables -fno-unwind-tables
LDFLAGS := -Wl,-s -Wl,--gc-sections -flto -lole32 -lwindowscodecs -lgdi32
RCFLAGS := --codepage=65001 -O coff

TARGET64 := paste64.exe
TARGET32 := paste32.exe
RES64 := paste64.res
RES32 := paste32.res
OBJ64 := $(OBJDIR)/paste64.o
OBJ32 := $(OBJDIR)/paste32.o
LEGACY_OBJ64 := paste64.o
LEGACY_OBJ32 := paste32.o
SIGN_AND_WARN = status=0; $(SIGN) "$@" || status=$$?; if [ $$status -ne 0 ]; then echo "Warning: code signing failed for $@ (exit $$status)" >&2; else touch "$@"; fi

all: $(TARGET64) $(TARGET32)

$(TARGET64): $(OBJ64) $(RES64)
	$(CC64) $(CFLAGS_COMMON) $(OBJ64) $(RES64) -o $@ $(LDFLAGS)
	@$(SIGN_AND_WARN)

$(TARGET32): $(OBJ32) $(RES32)
	$(CC32) $(CFLAGS_COMMON) $(OBJ32) $(RES32) -o $@ $(LDFLAGS)
	@$(SIGN_AND_WARN)

$(OBJ64): $(SRC) | $(OBJDIR)
	$(CC64) $(CFLAGS_COMMON) -c $< -o $@

$(OBJ32): $(SRC) | $(OBJDIR)
	$(CC32) $(CFLAGS_COMMON) -c $< -o $@

$(OBJDIR):
	mkdir -p $@

$(RES64): $(RC)
	$(RC64) $(RCFLAGS) $(RC) -o $@

$(RES32): $(RC)
	$(RC32) $(RCFLAGS) $(RC) -o $@

clean:
	rm -f $(TARGET64) $(TARGET32) $(RES64) $(RES32) $(LEGACY_OBJ64) $(LEGACY_OBJ32)
	rm -rf $(OBJDIR)

.PHONY: all clean
