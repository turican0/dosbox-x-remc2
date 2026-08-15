/*
 * find_match - najde v adresari referencnich snimku ty, ktere nejlepe
 * odpovidaji jednomu zadanemu snimku.
 *
 * Pouziti: find_match <frame.raw> <refdir> <width> <height> [maxrefs]
 *
 * Format souboru je stejny jako u DUMPFRAME / REORION2_DUMP_FRAME_RANGE:
 * 768 B palety + width*height indexovanych pixelu. Porovnava se zvlast
 * pixelovy obsah a paleta (referencni 6bitova paleta se roztahuje na 8 bitu
 * stejnym vzorcem jako PortVga_SetPaletteEntry), aby slo odlisit "jina
 * scena" od "stejna scena, jina paleta".
 *
 * Vypise 8 nejlepsich shod podle poctu shodnych pixelu.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char expand6to8(unsigned char v)
{
    return (unsigned char)((v << 2) | (v >> 4));
}

typedef struct { long idx; long pixSame; int palSame; } Hit;

static int cmpHit(const void* a, const void* b)
{
    const Hit* x = (const Hit*)a;
    const Hit* y = (const Hit*)b;
    if (x->pixSame < y->pixSame) return 1;
    if (x->pixSame > y->pixSame) return -1;
    return 0;
}

int main(int argc, char** argv)
{
    if (argc < 5) {
        fprintf(stderr, "usage: %s <frame.raw> <refdir> <width> <height> [maxrefs]\n", argv[0]);
        return 1;
    }
    long width = atol(argv[3]), height = atol(argv[4]);
    long maxrefs = (argc > 5) ? atol(argv[5]) : 100000;
    long fbBytes = width * height;

    unsigned char* pal = (unsigned char*)malloc(768);
    unsigned char* fb = (unsigned char*)malloc((size_t)fbBytes);
    unsigned char* rpal = (unsigned char*)malloc(768);
    unsigned char* rfb = (unsigned char*)malloc((size_t)fbBytes);
    Hit* hits = (Hit*)malloc(sizeof(Hit) * (size_t)(maxrefs > 0 ? maxrefs : 1));
    if (!pal || !fb || !rpal || !rfb || !hits) { fprintf(stderr, "oom\n"); return 1; }

    FILE* f = fopen(argv[1], "rb");
    if (!f) { fprintf(stderr, "nelze otevrit %s\n", argv[1]); return 1; }
    if (fread(pal, 1, 768, f) != 768 || fread(fb, 1, (size_t)fbBytes, f) != (size_t)fbBytes) {
        fprintf(stderr, "kratky soubor %s\n", argv[1]); fclose(f); return 1;
    }
    fclose(f);

    long nHits = 0;
    for (long n = 0; n < maxrefs; n++) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/frame_%05ld.raw", argv[2], n);
        FILE* r = fopen(path, "rb");
        if (!r) break;
        if (fread(rpal, 1, 768, r) != 768 || fread(rfb, 1, (size_t)fbBytes, r) != (size_t)fbBytes) {
            fclose(r); break;
        }
        fclose(r);

        long pixSame = 0;
        for (long i = 0; i < fbBytes; i++)
            if (fb[i] == rfb[i]) pixSame++;
        int palSame = 0;
        for (int i = 0; i < 768; i++)
            if (pal[i] == expand6to8(rpal[i])) palSame++;

        hits[nHits].idx = n;
        hits[nHits].pixSame = pixSame;
        hits[nHits].palSame = palSame;
        nHits++;
    }

    qsort(hits, (size_t)nHits, sizeof(Hit), cmpHit);
    printf("porovnano %ld referencnich snimku, 8 nejlepsich:\n", nHits);
    for (long i = 0; i < nHits && i < 8; i++)
        printf("  ref %4ld: pixelu shodnych %7ld/%ld (%5.1f%%)  palety shodnych %3d/768\n",
               hits[i].idx, hits[i].pixSame, fbBytes,
               100.0 * (double)hits[i].pixSame / (double)fbBytes, hits[i].palSame);

    return 0;
}
