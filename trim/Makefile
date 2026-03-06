TRIM_SRC := trim.c
RC := trim.rc
ICO := trim.ico
PCRE2_DIR := third_party/pcre2/src
OBJDIR := obj

CC64 := x86_64-w64-mingw32-gcc
CC32 := i686-w64-mingw32-gcc
RC64 := x86_64-w64-mingw32-windres
RC32 := i686-w64-mingw32-windres
SIGN ?= cs

CFLAGS_COMMON := -std=c11 -Wall -Wextra -Wpedantic -O2 -flto -municode -fno-asynchronous-unwind-tables -fno-unwind-tables
CFLAGS_PCRE2 := -std=c11 -O2 -flto -w -fno-asynchronous-unwind-tables -fno-unwind-tables -I$(PCRE2_DIR) -DHAVE_CONFIG_H -DPCRE2_CODE_UNIT_WIDTH=16
LDFLAGS := -Wl,-s -Wl,--gc-sections -flto -luser32 -lwinmm
RCFLAGS := --codepage=65001 -O coff

TARGET64 := trim64.exe
TARGET32 := trim32.exe
RES64 := trim64.res
RES32 := trim32.res
TRIM_OBJ64 := $(OBJDIR)/trim64.o
TRIM_OBJ32 := $(OBJDIR)/trim32.o
LEGACY_OBJ64 := trim64.o
LEGACY_OBJ32 := trim32.o
SIGN_AND_WARN = status=0; $(SIGN) "$@" || status=$$?; if [ $$status -ne 0 ]; then echo "Warning: code signing failed for $@ (exit $$status)" >&2; else touch "$@"; fi

PCRE2_HEADERS := $(wildcard $(PCRE2_DIR)/*.h)
PCRE2_SRC := \
	$(PCRE2_DIR)/pcre2_auto_possess.c \
	$(PCRE2_DIR)/pcre2_chkdint.c \
	$(PCRE2_DIR)/pcre2_chartables.c \
	$(PCRE2_DIR)/pcre2_compile.c \
	$(PCRE2_DIR)/pcre2_compile_cgroup.c \
	$(PCRE2_DIR)/pcre2_compile_class.c \
	$(PCRE2_DIR)/pcre2_config.c \
	$(PCRE2_DIR)/pcre2_context.c \
	$(PCRE2_DIR)/pcre2_convert.c \
	$(PCRE2_DIR)/pcre2_dfa_match.c \
	$(PCRE2_DIR)/pcre2_error.c \
	$(PCRE2_DIR)/pcre2_extuni.c \
	$(PCRE2_DIR)/pcre2_find_bracket.c \
	$(PCRE2_DIR)/pcre2_jit_compile.c \
	$(PCRE2_DIR)/pcre2_maketables.c \
	$(PCRE2_DIR)/pcre2_match.c \
	$(PCRE2_DIR)/pcre2_match_data.c \
	$(PCRE2_DIR)/pcre2_match_next.c \
	$(PCRE2_DIR)/pcre2_newline.c \
	$(PCRE2_DIR)/pcre2_ord2utf.c \
	$(PCRE2_DIR)/pcre2_pattern_info.c \
	$(PCRE2_DIR)/pcre2_script_run.c \
	$(PCRE2_DIR)/pcre2_serialize.c \
	$(PCRE2_DIR)/pcre2_string_utils.c \
	$(PCRE2_DIR)/pcre2_study.c \
	$(PCRE2_DIR)/pcre2_substitute.c \
	$(PCRE2_DIR)/pcre2_substring.c \
	$(PCRE2_DIR)/pcre2_tables.c \
	$(PCRE2_DIR)/pcre2_ucd.c \
	$(PCRE2_DIR)/pcre2_valid_utf.c \
	$(PCRE2_DIR)/pcre2_xclass.c

PCRE2_OBJ64 := $(PCRE2_SRC:$(PCRE2_DIR)/%.c=$(OBJDIR)/pcre2_64_%.o)
PCRE2_OBJ32 := $(PCRE2_SRC:$(PCRE2_DIR)/%.c=$(OBJDIR)/pcre2_32_%.o)
LEGACY_PCRE2_OBJ64 := $(PCRE2_SRC:$(PCRE2_DIR)/%.c=pcre2_64_%.o)
LEGACY_PCRE2_OBJ32 := $(PCRE2_SRC:$(PCRE2_DIR)/%.c=pcre2_32_%.o)

all: $(TARGET64) $(TARGET32)

$(TARGET64): $(TRIM_OBJ64) $(PCRE2_OBJ64) $(RES64)
	$(CC64) $(CFLAGS_COMMON) $(TRIM_OBJ64) $(PCRE2_OBJ64) $(RES64) -o $@ $(LDFLAGS)
	@$(SIGN_AND_WARN)

$(TARGET32): $(TRIM_OBJ32) $(PCRE2_OBJ32) $(RES32)
	$(CC32) $(CFLAGS_COMMON) $(TRIM_OBJ32) $(PCRE2_OBJ32) $(RES32) -o $@ $(LDFLAGS)
	@$(SIGN_AND_WARN)

$(TRIM_OBJ64): $(TRIM_SRC) $(PCRE2_HEADERS) | $(OBJDIR)
	$(CC64) $(CFLAGS_COMMON) -I$(PCRE2_DIR) -DHAVE_CONFIG_H -DPCRE2_CODE_UNIT_WIDTH=16 -c $< -o $@

$(TRIM_OBJ32): $(TRIM_SRC) $(PCRE2_HEADERS) | $(OBJDIR)
	$(CC32) $(CFLAGS_COMMON) -I$(PCRE2_DIR) -DHAVE_CONFIG_H -DPCRE2_CODE_UNIT_WIDTH=16 -c $< -o $@

$(OBJDIR):
	mkdir -p $@

$(OBJDIR)/pcre2_64_%.o: $(PCRE2_DIR)/%.c $(PCRE2_HEADERS) | $(OBJDIR)
	$(CC64) $(CFLAGS_PCRE2) -c $< -o $@

$(OBJDIR)/pcre2_32_%.o: $(PCRE2_DIR)/%.c $(PCRE2_HEADERS) | $(OBJDIR)
	$(CC32) $(CFLAGS_PCRE2) -c $< -o $@

$(RES64): $(RC) $(ICO)
	$(RC64) $(RCFLAGS) $(RC) -o $@

$(RES32): $(RC) $(ICO)
	$(RC32) $(RCFLAGS) $(RC) -o $@

clean:
	rm -f $(TARGET64) $(TARGET32) $(RES64) $(RES32) $(LEGACY_OBJ64) $(LEGACY_OBJ32) $(LEGACY_PCRE2_OBJ64) $(LEGACY_PCRE2_OBJ32)
	rm -rf $(OBJDIR)

.PHONY: all clean
