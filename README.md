# yaz0encdec

`yaz0encdec` is a small C99 command-line tool for compressing and
decompressing Nintendo 64 Ocarina of Time ROMs.

It understands the ROM's DMA table, detects the ROM version from the embedded
build date, and recompresses each file with the codec used by that ROM family:

- Yaz0 for the standard N64, GameCube and Master Quest ROMs.
- raw deflate for iQue ROMs, including the iQue footer format.

The project is dependency-free. Yaz0, raw deflate encode/decode, DMA handling
and N64 CRC calculation are all implemented in this source tree.

The tool was created as a study of how OoT ROMs are structured, how the DMA
file table maps virtual addresses to physical segments, and how those segments
are compressed. It is primarily used for producing ROMs compatible with my
Swedish translation of Ocarina of Time.

## Supported ROMs

- NTSC 1.0, 1.1, 1.2
- PAL 1.0, 1.1
- NTSC GameCube
- PAL GameCube
- NTSC Master Quest
- PAL Master Quest
- NTSC iQue
- PAL iQue
- NTSC iQue Master Quest
- PAL iQue Master Quest

Unknown ROMs are rejected instead of guessed. The version table lives in
`src/romdb.c`.

## Usage

Compress a decompressed ROM:

```sh
./yaz0encdec --compress --in input.z64 --out output.z64
```

Decompress a compressed ROM:

```sh
./yaz0encdec --decompress --in input.z64 --out output.z64
```

Short forms are also available:

```sh
./yaz0encdec -c --in input.z64 --out output.z64
./yaz0encdec -d --in input.z64 --out output.z64
```

Batch-compress every recognized `.z64` file in a directory:

```sh
./yaz0encdec --batch --in source_dir --out target_dir
```

Batch mode skips unrecognized ROMs and writes recognized ROMs to the target
directory using the same filenames.

## Building

Build the native Linux binary:

```sh
make native
```

This produces `yaz0encdec`.

Build the Windows executable from WSL/Linux:

```sh
make
```

This requires `x86_64-w64-mingw32-gcc` from `mingw-w64` and produces
`yaz0encdec.exe`.

Remove generated build outputs:

```sh
make clean
```

## Source Layout

```text
src/
  main.c              command-line parsing and batch mode
  romdb.c/.h          ROM version database, codec selection and skip lists
  dma.c/.h            DMA table parsing, validation and writing
  compress.c/.h       full-ROM compression pipeline
  decompress.c/.h     full-ROM decompression pipeline
  yaz0.c/.h           Yaz0 encoder and decoder
  zlib.c/.h           wrapper for OoT zlib/raw-deflate file formats
  deflate.c/.h        dependency-free raw deflate decoder and public API
  deflate_encode.c    dependency-free raw deflate encoder
  n64crc.c/.h         N64 ROM checksum calculation
  util.c/.h           shared byte I/O, alignment and allocation helpers
Makefile
README.md
```

## Notes

For iQue ROMs, the deflate stream is stored without a normal zlib header. The
tool writes the raw deflate payload followed by the iQue footer: CRC32 and
uncompressed size, both little-endian.
