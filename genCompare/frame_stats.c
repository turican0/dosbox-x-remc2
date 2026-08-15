/*
 * frame_stats - rychla statistika pres adresar frame_NNNNN.raw snimku
 * (stejny format jako DUMPFRAME / REORION2_DUMP_FRAME_RANGE: 768B palety +
 * width*height indexovanych pixelu).
 *
 * Pro kazdy snimek vypise:
 *   nonzero_pal  - pocet nenulovych paletovych kanalu (0..768); 0 = cerna
 *   distinct_pix - pocet ruznych indexu pixelu pouzitych ve framebufferu
 *   top_pix      - nejcastejsi index a kolik procent plochy zabira
 *
 * Pouziti: frame_stats <dir> <width> <height> [maxframes]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: %s <dir> <width> <height> [maxframes]\n", argv[0]);
        return 1;
    }
    const char* dir = argv[1];
    long width = atol(argv[2]);
    long height = atol(argv[3]);
    long maxframes = (argc > 4) ? atol(argv[4]) : 100000;
    long fbBytes = width * height;

    unsigned char* pal = (unsigned char*)malloc(768);
    unsigned char* fb = (unsigned char*)malloc((size_t)fbBytes);
    if (!pal || !fb) { fprintf(stderr, "oom\n"); return 1; }

    for (long n = 0; n < maxframes; n++) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/frame_%05ld.raw", dir, n);
        FILE* f = fopen(path, "rb");
        if (!f) break;
        if (fread(pal, 1, 768, f) != 768 ||
            fread(fb, 1, (size_t)fbBytes, f) != (size_t)fbBytes) {
            fclose(f);
            fprintf(stderr, "frame %ld: kratky soubor\n", n);
            break;
        }
        fclose(f);

        int nonzeroPal = 0;
        for (int i = 0; i < 768; i++)
            if (pal[i]) nonzeroPal++;

        long hist[256];
        memset(hist, 0, sizeof(hist));
        for (long i = 0; i < fbBytes; i++)
            hist[fb[i]]++;

        int distinct = 0, topIdx = 0;
        long topCount = 0;
        for (int i = 0; i < 256; i++) {
            if (hist[i]) distinct++;
            if (hist[i] > topCount) { topCount = hist[i]; topIdx = i; }
        }

        /* Bounding box of everything that is NOT the dominant (background)
           index - i.e. where the actual picture/video rect lives. Lets a
           port frame whose background index differs from the original's
           still be compared on WHERE its content sits. */
        long minx = width, maxx = -1, miny = height, maxy = -1, inBox = 0;
        for (long y = 0; y < height; y++) {
            for (long x = 0; x < width; x++) {
                if (fb[y * width + x] == (unsigned char)topIdx) continue;
                if (x < minx) minx = x;
                if (x > maxx) maxx = x;
                if (y < miny) miny = y;
                if (y > maxy) maxy = y;
                inBox++;
            }
        }

        printf("frame %5ld: nonzero_pal=%3d distinct_pix=%3d top_pix=%3d (%5.1f%%)"
               "  content_bbox=(%ld,%ld)-(%ld,%ld) n=%ld\n",
               n, nonzeroPal, distinct, topIdx, 100.0 * (double)topCount / (double)fbBytes,
               minx, miny, maxx, maxy, inBox);
    }

    free(pal);
    free(fb);
    return 0;
}
