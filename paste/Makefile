SRC := paste.c
RC := paste.rc

CC64 := x86_64-w64-mingw32-gcc
CC32 := i686-w64-mingw32-gcc
RC64 := x86_64-w64-mingw32-windres
RC32 := i686-w64-mingw32-windres

CFLAGS_COMMON := -std=c11 -Wall -Wextra -Wpedantic -O2 -flto -municode -DUNICODE -D_UNICODE -DCOBJMACROS -fno-asynchronous-unwind-tables -fno-unwind-tables
LDFLAGS := -Wl,-s -Wl,--gc-sections -flto -lole32 -lwindowscodecs -lgdi32
RCFLAGS := --codepage=65001 -O coff

TARGET64 := paste64.exe
TARGET32 := paste32.exe
RES64 := paste64.res
RES32 := paste32.res

all: $(TARGET64) $(TARGET32)

$(TARGET64): $(SRC) $(RES64)
	$(CC64) $(CFLAGS_COMMON) $(SRC) $(RES64) -o $@ $(LDFLAGS)

$(TARGET32): $(SRC) $(RES32)
	$(CC32) $(CFLAGS_COMMON) $(SRC) $(RES32) -o $@ $(LDFLAGS)

$(RES64): $(RC)
	$(RC64) $(RCFLAGS) $(RC) -o $@

$(RES32): $(RC)
	$(RC32) $(RCFLAGS) $(RC) -o $@

clean:
	rm -f $(TARGET64) $(TARGET32) $(RES64) $(RES32)

.PHONY: all clean
