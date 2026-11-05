/* Host test: the PSMCT32 local->local copy (run-based scratch path) must give the same GS memory
 * and invalidate the same pages as the original in-order per-pixel copy, for disjoint ranges
 * (fast path) and for overlapping ones (must fall back to the per-pixel order).
 *
 *   cc -O2 -I../src/gs gs_localcopy_test.c -o localcopy_test && ./localcopy_test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/gs/gs_mem.c"

void gsXferDone(unsigned int bp, unsigned int bw, unsigned int psm, unsigned int x, unsigned int y, unsigned int w, unsigned int h, int c) { (void)bp; (void)bw; (void)psm; (void)x; (void)y; (void)w; (void)h; (void)c; }
void gsFlushLocalToLocal(unsigned int bp, unsigned int bw, unsigned int psm, unsigned int x, unsigned int y, unsigned int w, unsigned int h) { (void)bp; (void)bw; (void)psm; (void)x; (void)y; (void)w; (void)h; }
void gsFlushLocalToLocalCopy(unsigned int bp, unsigned int bw, unsigned int psm, unsigned int x, unsigned int y, unsigned int w, unsigned int h, unsigned int dbp, unsigned int dbw, unsigned int dpsm, unsigned int dy, unsigned int dh) { (void)bp; (void)bw; (void)psm; (void)x; (void)y; (void)w; (void)h; (void)dbp; (void)dbw; (void)dpsm; (void)dy; (void)dh; }
double pcOsTime(void) { return 0; }

static unsigned int rng = 7;
static unsigned int rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }

static unsigned char snap_vram[GS_VRAM_SIZE];
static unsigned int snap_gen[GS_PAGES];

int main(void)
{
    int iter, fast_cases = 0, overlap_cases = 0, bad = 0;

    gsMemInit();

    for (iter = 0; iter < 3000; iter++)
    {
        unsigned int sbw = 1 + rnd() % 10, dbw = 1 + rnd() % 10;
        unsigned int w = 1 + rnd() % (((sbw < dbw) ? sbw : dbw) * 64);
        unsigned int h = 1 + rnd() % 90;
        unsigned int ssx = rnd() % (sbw * 64 - w + 1), dsx = rnd() % (dbw * 64 - w + 1);
        unsigned int ssy = rnd() % 70, dsy = rnd() % 70;
        int disjoint = (rnd() % 3) != 0;
        unsigned int sbp = (rnd() % 4) * 32 * 8;
        unsigned int dbp = disjoint ? sbp + 32 * 8 * (12 + rnd() % 8) : sbp + (rnd() % 3) * 32;
        GsRegs r;
        unsigned int x, y, k;
        unsigned long long ref_h, fast_h;
        unsigned char ref_pages[GS_PAGES], fast_pages[GS_PAGES];

        if (dbp + 256 > 16384) dbp = sbp + 32;

        /* fill the source area with random pixels (and the destination with other data) */
        for (y = 0; y < ssy + h; y++)
            for (x = 0; x < sbw * 64; x++)
                gsMemWrite(GS_PSMCT32, sbp, sbw, x, y, rnd() * 2654435761u);

        memset(&r, 0, sizeof(r));
        r.bitbltbuf = ((u64)GS_PSMCT32 << 56) | ((u64)dbw << 48) | ((u64)dbp << 32) | ((u64)GS_PSMCT32 << 24) | ((u64)sbw << 16) | sbp;
        r.trxpos = ((u64)dsy << 48) | ((u64)dsx << 32) | ((u64)ssy << 16) | ssx;
        r.trxreg = ((u64)h << 32) | w;
        r.trxdir = 2;

        memcpy(snap_vram, gsVram, sizeof(gsVram));
        memcpy(snap_gen, gsPageGen, sizeof(gsPageGen));

        /* reference: original in-order per-pixel copy */
        for (y = 0; y < h; y++)
            for (x = 0; x < w; x++)
                gsMemWrite(GS_PSMCT32, dbp, dbw, dsx + x, dsy + y, gsMemRead(GS_PSMCT32, sbp, sbw, ssx + x, ssy + y));
        ref_h = 0; for (k = 0; k < sizeof(gsVram); k++) ref_h = (ref_h ^ gsVram[k]) * 1099511628211ULL;
        for (k = 0; k < GS_PAGES; k++) ref_pages[k] = gsPageGen[k] != snap_gen[k];

        memcpy(gsVram, snap_vram, sizeof(gsVram));
        memcpy(gsPageGen, snap_gen, sizeof(gsPageGen));

        gsMemStartTransfer(&r);
        fast_h = 0; for (k = 0; k < sizeof(gsVram); k++) fast_h = (fast_h ^ gsVram[k]) * 1099511628211ULL;
        for (k = 0; k < GS_PAGES; k++) fast_pages[k] = gsPageGen[k] != snap_gen[k];

        if (ref_h != fast_h || memcmp(ref_pages, fast_pages, sizeof(ref_pages)) != 0)
        {
            if (bad < 5) printf("MISMATCH iter %d: sbp %u sbw %u dbp %u dbw %u w %u h %u\n", iter, sbp, sbw, dbp, dbw, w, h);
            bad++;
        }

        if (disjoint) fast_cases++; else overlap_cases++;
    }

    printf("gs_localcopy_test: %d disjoint-intent, %d overlap-intent cases, %d mismatches\n", fast_cases, overlap_cases, bad);
    return bad != 0;
}
