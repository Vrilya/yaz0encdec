#include "util.h"
#include "romdb.h"
#include "dma.h"
#include "compress.h"
#include "decompress.h"

#include <dirent.h>
#include <sys/stat.h>

#define MB_DEFAULT 32

static void usage(void) {
    fprintf(stderr,
        "yaz0encdec - N64 OoT ROM Yaz0 compressor/decompressor\n"
        "\n"
        "Usage:\n"
        "    yaz0encdec --compress --in <rom.z64> --out <compressed.z64>\n"
        "    yaz0encdec --decompress --in <compressed.z64> --out <decompressed.z64>\n"
        "    yaz0encdec --batch --in <source_dir> --out <target_dir>\n"
        "\n"
        "Options:\n"
        "    --in <file>       Input ROM file (or source directory for --batch)\n"
        "    --out <file>      Output ROM file (or target directory for --batch)\n"
        "    --compress, -c    Compress a decompressed ROM\n"
        "    --decompress, -d  Decompress a compressed ROM\n"
        "    --batch           Compress all recognized ROMs from --in dir to --out dir\n"
        "\n"
    );
    exit(1);
}

static uint8_t *load_file(const char *path, long *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = (uint8_t *)malloc(len);
    if (!data) { fclose(f); return NULL; }
    if (fread(data, 1, len, f) != (size_t)len) { free(data); fclose(f); return NULL; }
    fclose(f);
    *out_len = len;
    return data;
}

static int write_file(const char *path, const uint8_t *data, size_t size) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    size_t written = fwrite(data, 1, size, f);
    fclose(f);
    return written == size;
}

static void ensure_dir(const char *path) {
#ifdef _WIN32
    mkdir(path);
#else
    mkdir(path, 0755);
#endif
}

static int has_z64_ext(const char *name) {
    size_t len = strlen(name);
    if (len < 4) return 0;
    return strcmp(name + len - 4, ".z64") == 0;
}

static int do_batch(const char *in_dir, const char *out_dir) {
    if (!in_dir)  die("--batch requires --in <source directory>");
    if (!out_dir) die("--batch requires --out <target directory>");
    if (strcmp(in_dir, out_dir) == 0)
        die("--in and --out cannot be the same directory");

    DIR *dir = opendir(in_dir);
    if (!dir) {
        fprintf(stderr, "error: cannot open directory '%s'\n", in_dir);
        return 1;
    }

    ensure_dir(out_dir);

    int total = 0, success = 0, skipped = 0;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (!has_z64_ext(ent->d_name))
            continue;

        total++;

        /* Build input path */
        char in_path[1024];
        snprintf(in_path, sizeof(in_path), "%s/%s", in_dir, ent->d_name);

        fprintf(stderr, "\n=== [%d] %s ===\n", total, ent->d_name);
        fprintf(stderr, "loading '%s'...\n", in_path);

        long rom_len = 0;
        uint8_t *rom_data = load_file(in_path, &rom_len);
        if (!rom_data) {
            fprintf(stderr, "error: cannot read '%s', skipping\n", in_path);
            skipped++;
            continue;
        }
        fprintf(stderr, "ROM size: %ld bytes (%.1f MiB)\n",
                rom_len, (double)rom_len / (1024 * 1024));

        /* Detect ROM version */
        const rom_version_t *detected = detect_rom_version(rom_data, (size_t)rom_len);
        if (!detected) {
            fprintf(stderr, "warning: could not identify ROM version for '%s', skipping\n",
                    ent->d_name);
            free(rom_data);
            skipped++;
            continue;
        }
        fprintf(stderr, "detected: %s\n", detected->name);

        uint32_t dma_offset = detected->dma_offset;
        int dma_count = detected->dma_count;

        fprintf(stderr, "DMA table: 0x%X, %d entries\n", dma_offset, dma_count);

        /* Reset global state before each ROM */
        memset(entries, 0, sizeof(entries));
        num_entries = 0;

        parse_dma_table(rom_data, dma_offset, dma_count);
        validate_dma((size_t)rom_len);
        apply_rom_config(detected);

        int comp_count = 0;
        for (int i = 0; i < num_entries; i++)
            if (entries[i].compress) comp_count++;
        fprintf(stderr, "files to compress: %d\n", comp_count);

        size_t out_rom_size;
        uint8_t *out_rom = compress_rom(rom_data, MB_DEFAULT, dma_offset, dma_count,
                                         &out_rom_size);
        free(rom_data);

        /* Build output path */
        char out_path[1024];
        snprintf(out_path, sizeof(out_path), "%s/%s", out_dir, ent->d_name);

        if (!write_file(out_path, out_rom, out_rom_size)) {
            fprintf(stderr, "error: cannot write '%s'\n", out_path);
            free(out_rom);
            skipped++;
            continue;
        }

        free(out_rom);
        fprintf(stderr, "compressed ROM written to '%s'\n", out_path);
        success++;
    }

    closedir(dir);

    fprintf(stderr, "\n=== batch complete ===\n");
    fprintf(stderr, "total: %d, compressed: %d, skipped: %d\n",
            total, success, skipped);

    return (success > 0) ? 0 : 1;
}

int main(int argc, char **argv) {
    if (argc < 2) usage();

    const char *in_path = NULL;
    const char *out_path = NULL;
    int do_compress = 0;
    int do_decompress = 0;
    int batch_mode = 0;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "--in") == 0) {
            if (++i >= argc) die("--in requires a value");
            in_path = argv[i];
        } else if (strcmp(arg, "--out") == 0) {
            if (++i >= argc) die("--out requires a value");
            out_path = argv[i];
        } else if (strcmp(arg, "--compress") == 0 || strcmp(arg, "-c") == 0) {
            do_compress = 1;
        } else if (strcmp(arg, "--decompress") == 0 || strcmp(arg, "-d") == 0) {
            do_decompress = 1;
        } else if (strcmp(arg, "--batch") == 0) {
            batch_mode = 1;
        } else {
            fprintf(stderr, "error: unknown argument '%s'\n", arg); exit(1);
        }
    }

    if (do_compress && do_decompress)
        die("cannot use --compress and --decompress together");

    if (batch_mode) {
        return do_batch(in_path, out_path);
    }

    if (!do_compress && !do_decompress)
        die("must specify --compress or --decompress");

    if (!in_path)  die("no --in arg provided");
    if (!out_path) die("no --out arg provided");
    if (strcmp(in_path, out_path) == 0)
        die("--in and --out cannot be the same path");

    /* Load ROM */
    fprintf(stderr, "loading '%s'...\n", in_path);
    FILE *f = fopen(in_path, "rb");
    if (!f) { fprintf(stderr, "error: cannot open '%s'\n", in_path); exit(1); }
    fseek(f, 0, SEEK_END);
    long rom_len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *rom_data = (uint8_t *)malloc(rom_len);
    if (!rom_data) die("out of memory");
    if (fread(rom_data, 1, rom_len, f) != (size_t)rom_len) die("failed to read ROM");
    fclose(f);
    fprintf(stderr, "ROM size: %ld bytes (%.1f MiB)\n", rom_len, (double)rom_len/(1024*1024));

    if (do_decompress) {
        /* === Decompress mode === */
        fprintf(stderr, "mode: decompress\n");

        size_t out_rom_size;
        uint8_t *out_rom = do_decompress_rom(rom_data, (size_t)rom_len, &out_rom_size);
        free(rom_data);

        fprintf(stderr, "decompressed ROM size: %zu bytes (%.1f MiB)\n",
                out_rom_size, (double)out_rom_size/(1024*1024));

        f = fopen(out_path, "wb");
        if (!f) { fprintf(stderr, "error: cannot open '%s'\n", out_path); exit(1); }
        fwrite(out_rom, 1, out_rom_size, f);
        fclose(f);
        free(out_rom);
        fprintf(stderr, "decompressed ROM written to '%s'\n", out_path);
    } else {
        /* === Compress mode === */
        fprintf(stderr, "mode: compress\n");

        /* Auto-detect ROM version */
        const rom_version_t *detected = detect_rom_version(rom_data, (size_t)rom_len);
        if (!detected) {
            fprintf(stderr,
                "error: could not identify ROM version.\n"
                "Supported versions:\n");
            for (size_t i = 0; i < num_rom_versions; i++)
                fprintf(stderr, "  %-22s  build: %s  @ 0x%X\n",
                        rom_versions[i].name, rom_versions[i].build_date,
                        rom_versions[i].build_offset);
            exit(1);
        }
        fprintf(stderr, "detected: %s\n", detected->name);

        uint32_t dma_offset = detected->dma_offset;
        int dma_count = detected->dma_count;

        fprintf(stderr, "DMA table: 0x%X, %d entries\n", dma_offset, dma_count);
        parse_dma_table(rom_data, dma_offset, dma_count);
        validate_dma((size_t)rom_len);

        apply_rom_config(detected);

        int comp_count = 0;
        for (int i = 0; i < num_entries; i++)
            if (entries[i].compress) comp_count++;
        fprintf(stderr, "files to compress: %d\n", comp_count);

        size_t out_rom_size;
        uint8_t *out_rom = compress_rom(rom_data, MB_DEFAULT, dma_offset, dma_count,
                                         &out_rom_size);
        free(rom_data);
        fprintf(stderr, "ROM compressed successfully!\n");

        f = fopen(out_path, "wb");
        if (!f) { fprintf(stderr, "error: cannot open '%s'\n", out_path); exit(1); }
        fwrite(out_rom, 1, out_rom_size, f);
        fclose(f);
        free(out_rom);
        fprintf(stderr, "compressed ROM written to '%s'\n", out_path);
    }

    return 0;
}