# yaz0encdec

yaz0encdec is a command-line tool for compressing and decompressing N64 Ocarina of Time ROMs using the Yaz0 algorithm. It was created as a study of how OoT ROMs are typically compressed and how the DMA file table relates to the Yaz0-encoded segments within the ROM.

The tool is primarily intended for use with the ROMs produced by my Swedish translation of Ocarina of Time and may not work correctly with official retail ROMs.

## Usage

Compress a decompressed ROM:

    yaz0encdec --compress --in <decompressed.z64> --out <compressed.z64>

Decompress a compressed ROM:

    yaz0encdec --decompress --in <compressed.z64> --out <decompressed.z64>

Batch compress all recognized ROMs in a directory:

    yaz0encdec --batch --in <source_dir> --out <target_dir>

This scans the source directory for `.z64` files, identifies each ROM version automatically, compresses them, and saves the results to the target directory using the same filenames. Unrecognized files are skipped. Existing files in the target directory are overwritten without prompting.

The ROM version is detected automatically from the build date string embedded in the ROM header.

## Building

The project is written in C99 with no external dependencies.

### Windows (cross-compile from WSL/Linux)

Requires `mingw-w64`:

    sudo apt install gcc-mingw-w64-x86-64

Build:

    make

This produces `yaz0encdec.exe`.

### Linux

    make native

This produces `yaz0encdec`.

### Cleaning build artifacts

    make clean

## Project structure

    src/
      main.c          Entry point and argument parsing
      yaz0.c/.h       Yaz0 encoder and decoder
      n64crc.c/.h     N64 ROM CRC calculation
      dma.c/.h        DMA table parsing, validation and writing
      romdb.c/.h      ROM version database and detection
      compress.c/.h   Full-ROM compression pipeline
      decompress.c/.h Full-ROM decompression pipeline
      util.c/.h       Shared helpers (byte I/O, alignment, dynamic buffers)
    Makefile
