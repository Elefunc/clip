SRC := cliptrim.c

CC64 := x86_64-w64-mingw32-gcc
CC32 := i686-w64-mingw32-gcc

CFLAGS_COMMON := -std=c11 -Wall -Wextra -Wpedantic -O2 -municode
LDFLAGS := -luser32

TARGET64 := cliptrim64.exe
TARGET32 := cliptrim32.exe

all: $(TARGET64) $(TARGET32)

$(TARGET64): $(SRC)
	$(CC64) $(CFLAGS_COMMON) $< -o $@ $(LDFLAGS)

$(TARGET32): $(SRC)
	$(CC32) $(CFLAGS_COMMON) $< -o $@ $(LDFLAGS)

clean:
	rm -f $(TARGET64) $(TARGET32)

.PHONY: all clean
