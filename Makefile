# yaz0encdec - N64 OoT ROM Yaz0 compressor/decompressor
# -------------------------------------------------------
# Build targets:
#   make            - cross-compile Windows .exe (mingw-w64)
#   make native     - build native Linux binary
#   make clean      - remove build artifacts

# Cross-compiler (Windows target from WSL/Linux)
CC_CROSS  = x86_64-w64-mingw32-gcc
# Native compiler
CC_NATIVE = gcc

CFLAGS = -O3 -Wall -Wextra -std=c99 -pedantic
SRCDIR = src
OBJDIR = build

SRCS = $(SRCDIR)/util.c      \
       $(SRCDIR)/yaz0.c      \
       $(SRCDIR)/n64crc.c    \
       $(SRCDIR)/dma.c       \
       $(SRCDIR)/romdb.c     \
       $(SRCDIR)/compress.c  \
       $(SRCDIR)/decompress.c \
       $(SRCDIR)/main.c

# Object files (one set per target)
OBJS_WIN    = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/win/%.o,$(SRCS))
OBJS_NATIVE = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/native/%.o,$(SRCS))

TARGET_WIN    = yaz0encdec.exe
TARGET_NATIVE = yaz0encdec

# Default: cross-compile for Windows
.PHONY: all native clean

all: $(TARGET_WIN)

native: $(TARGET_NATIVE)

$(TARGET_WIN): $(OBJS_WIN)
	$(CC_CROSS) $(CFLAGS) -o $@ $^

$(TARGET_NATIVE): $(OBJS_NATIVE)
	$(CC_NATIVE) $(CFLAGS) -o $@ $^

$(OBJDIR)/win/%.o: $(SRCDIR)/%.c | $(OBJDIR)/win
	$(CC_CROSS) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/native/%.o: $(SRCDIR)/%.c | $(OBJDIR)/native
	$(CC_NATIVE) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/win:
	mkdir -p $@

$(OBJDIR)/native:
	mkdir -p $@

clean:
	rm -rf $(OBJDIR) $(TARGET_WIN) $(TARGET_NATIVE)
