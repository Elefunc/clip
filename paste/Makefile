SRC := paste.c

CC64 := x86_64-w64-mingw32-gcc
CC32 := i686-w64-mingw32-gcc

CFLAGS_COMMON := -std=c11 -Wall -Wextra -Wpedantic -O2 -flto -municode -DUNICODE -D_UNICODE -DCOBJMACROS -fno-asynchronous-unwind-tables -fno-unwind-tables
LDFLAGS := -Wl,-s -Wl,--gc-sections -flto -lole32 -lwindowscodecs -lgdi32

TARGET64 := paste64.exe
TARGET32 := paste32.exe

all: $(TARGET64) $(TARGET32)

$(TARGET64): $(SRC)
	$(CC64) $(CFLAGS_COMMON) $(SRC) -o $@ $(LDFLAGS)

$(TARGET32): $(SRC)
	$(CC32) $(CFLAGS_COMMON) $(SRC) -o $@ $(LDFLAGS)

clean:
	rm -f $(TARGET64) $(TARGET32)

.PHONY: all clean
