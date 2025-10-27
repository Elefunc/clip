SRC := cliptrim.c
RC := cliptrim.rc
ICO := cliptrim.ico

CC64 := x86_64-w64-mingw32-gcc
CC32 := i686-w64-mingw32-gcc
RC64 := x86_64-w64-mingw32-windres
RC32 := i686-w64-mingw32-windres

CFLAGS_COMMON := -std=c11 -Wall -Wextra -Wpedantic -O2 -flto -municode -fno-asynchronous-unwind-tables -fno-unwind-tables
LDFLAGS := -Wl,-s -Wl,--gc-sections -flto -luser32 -lwinmm
RCFLAGS := --codepage=65001 -O coff

TARGET64 := trim64.exe
TARGET32 := trim32.exe
RES64 := trim64.res
RES32 := trim32.res

all: $(TARGET64) $(TARGET32)

$(TARGET64): $(SRC) $(RES64)
	$(CC64) $(CFLAGS_COMMON) $(SRC) $(RES64) -o $@ $(LDFLAGS)

$(TARGET32): $(SRC) $(RES32)
	$(CC32) $(CFLAGS_COMMON) $(SRC) $(RES32) -o $@ $(LDFLAGS)

$(RES64): $(RC) $(ICO)
	$(RC64) $(RCFLAGS) $(RC) -o $@

$(RES32): $(RC) $(ICO)
	$(RC32) $(RCFLAGS) $(RC) -o $@

clean:
	rm -f $(TARGET64) $(TARGET32) $(RES64) $(RES32)

.PHONY: all clean
