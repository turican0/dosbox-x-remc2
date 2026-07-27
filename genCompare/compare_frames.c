/*
 * compare_frames.c (reorion2 wave 25p, on user's request)
 *
 * Batch-compares dosbox-x DUMPFRAME captures of the original against
 * reorion2 port_vga.cpp DumpFrameIfRequested captures (REORION2_DUMP_FRAME_RANGE),
 * frame by frame, palette + framebuffer, and reports where/how much they
 * diverge. Both sides write the SAME file layout to make this trivial:
 *
 *   frame_NNNNN.raw = 256*3 bytes RGB8 palette + width*height index bytes
 *
 * dosbox-x's palette is 6-bit VGA DAC values (0-63); this tool expands them
 * to 8-bit the same way PortVga_SetPaletteEntry does ((v<<2)|(v>>4)) before
 * comparing, so "0 mismatches" means "the port's actual displayed colors
 * match the original's actual displayed colors", not just "same raw bytes".
 *
 * Usage:
 *   compare_frames <dosbox_dir> <port_dir> <width> <height> [max_frames] [pixel_tolerance]
 *
 * Exit code: 0 if every compared frame matched exactly (within tolerance),
 * 1 if any frame diverged, 2 on usage/IO error. Meant to be run after a
 * matching pair of DUMPFRAME (dosbox_ctl.cfg) / REORION2_DUMP_FRAME_RANGE
 * (port) captures over the SAME stretch of gameplay/video.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char expand6to8(unsigned char v) {
    /* Same formula as PortVga_SetPaletteEntry / sub_132AF8's scaling. */
    return (unsigned char)((v << 2) | (v >> 4));
}

static long file_size(FILE* f) {
    long cur = ftell(f);
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, cur, SEEK_SET);
    return sz;
}

int main(int argc, char** argv) {
    if (argc < 5) {
        fprintf(stderr,
            "usage: %s <dosbox_dir> <port_dir> <width> <height> [max_frames] [pixel_tolerance]\n",
            argv[0]);
        return 2;
    }
    const char* dosboxDir = argv[1];
    const char* portDir = argv[2];
    int width = atoi(argv[3]);
    int height = atoi(argv[4]);
    int maxFrames = (argc > 5) ? atoi(argv[5]) : 100000;
    int pixelTolerance = (argc > 6) ? atoi(argv[6]) : 0; /* max differing bytes to still call it a match */

    if (width <= 0 || height <= 0) {
        fprintf(stderr, "bad width/height\n");
        return 2;
    }
    long pixelCount = (long)width * (long)height;
    unsigned char* dosboxBuf = (unsigned char*)malloc(768 + (size_t)pixelCount);
    unsigned char* portBuf = (unsigned char*)malloc(768 + (size_t)pixelCount);
    if (!dosboxBuf || !portBuf) {
        fprintf(stderr, "out of memory\n");
        return 2;
    }

    int framesCompared = 0;
    int framesMismatched = 0;
    int firstMismatchFrame = -1;

    for (int i = 0; i < maxFrames; i++) {
        char dPath[1024], pPath[1024];
        snprintf(dPath, sizeof(dPath), "%s/frame_%05d.raw", dosboxDir, i);
        snprintf(pPath, sizeof(pPath), "%s/frame_%05d.raw", portDir, i);

        FILE* df = fopen(dPath, "rb");
        FILE* pf = fopen(pPath, "rb");
        if (!df || !pf) {
            if (df) fclose(df);
            if (pf) fclose(pf);
            if (i == 0) {
                fprintf(stderr, "no frame pairs found (missing %s or %s)\n", dPath, pPath);
                free(dosboxBuf); free(portBuf);
                return 2;
            }
            break; /* ran out of frames on one side - stop, that's the batch end */
        }

        long dSize = file_size(df), pSize = file_size(pf);
        long expected = 768 + pixelCount;
        if (dSize != expected || pSize != expected) {
            printf("frame %5d: SIZE MISMATCH dosbox=%ld port=%ld expected=%ld\n",
                   i, dSize, pSize, expected);
            fclose(df); fclose(pf);
            framesMismatched++;
            if (firstMismatchFrame < 0) firstMismatchFrame = i;
            framesCompared++;
            continue;
        }
        fread(dosboxBuf, 1, (size_t)expected, df);
        fread(portBuf, 1, (size_t)expected, pf);
        fclose(df); fclose(pf);

        /* --- palette compare (dosbox 6-bit -> 8-bit expanded) --- */
        int paletteMismatches = 0;
        int worstPaletteDelta = 0;
        int worstPaletteIndex = -1;
        for (int c = 0; c < 256 * 3; c++) {
            unsigned char d = expand6to8(dosboxBuf[c]);
            unsigned char p = portBuf[c];
            int delta = (int)d - (int)p;
            if (delta < 0) delta = -delta;
            if (delta > pixelTolerance) {
                paletteMismatches++;
                if (delta > worstPaletteDelta) {
                    worstPaletteDelta = delta;
                    worstPaletteIndex = c / 3;
                }
            }
        }

        /* --- framebuffer compare (raw index bytes, no scaling) --- */
        long pixelMismatches = 0;
        long firstMismatchOffset = -1;
        for (long px = 0; px < pixelCount; px++) {
            if (dosboxBuf[768 + px] != portBuf[768 + px]) {
                pixelMismatches++;
                if (firstMismatchOffset < 0) firstMismatchOffset = px;
            }
        }

        int frameOk = (paletteMismatches == 0 && pixelMismatches == 0);
        framesCompared++;
        if (!frameOk) {
            framesMismatched++;
            if (firstMismatchFrame < 0) firstMismatchFrame = i;
        }

        double pixelPct = 100.0 * (double)pixelMismatches / (double)pixelCount;
        if (frameOk) {
            printf("frame %5d: MATCH\n", i);
        } else {
            printf("frame %5d: DIFF  palette_mismatches=%d (worst idx=%d delta=%d)"
                   "  pixel_mismatches=%ld/%ld (%.2f%%) first_at=(%ld,%ld)\n",
                   i, paletteMismatches, worstPaletteIndex, worstPaletteDelta,
                   pixelMismatches, pixelCount, pixelPct,
                   firstMismatchOffset < 0 ? -1 : firstMismatchOffset % width,
                   firstMismatchOffset < 0 ? -1 : firstMismatchOffset / width);
        }
    }

    free(dosboxBuf);
    free(portBuf);

    printf("---\n%d frame(s) compared, %d matched, %d diverged.\n",
           framesCompared, framesCompared - framesMismatched, framesMismatched);
    if (framesMismatched > 0)
        printf("first divergence at frame %d.\n", firstMismatchFrame);

    return framesCompared == 0 ? 2 : (framesMismatched > 0 ? 1 : 0);
}
