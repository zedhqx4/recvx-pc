/* Host tests for the render-target readback path (gs_rtmap.h + gsMemWriteRect32):
 *  A. gsMemWriteRect32 leaves GS memory and the set of invalidated pages identical to one
 *     gsMemWrite() per pixel.
 *  B. Copying back only the region chosen by gsRtReadRegion() gives the same result as copying
 *     back everything that was drawn, for every pixel the transfer reads.
 *
 *   cc -std=gnu11 -I../include -I../src -I../src/gs gs_readback_test.c -o rb && ./rb
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/gs/gs_mem.c"
#include "../src/gs/gs_rtmap.h"

double pcOsTime(void) { return 0.0; }
void gsXferDone(unsigned int bp, unsigned int bw, unsigned int psm, unsigned int x, unsigned int y, unsigned int w, unsigned int h, int c) { (void)bp; (void)bw; (void)psm; (void)x; (void)y; (void)w; (void)h; (void)c; }
void gsFlushLocalToLocal(unsigned int bp, unsigned int bw, unsigned int psm, unsigned int x, unsigned int y, unsigned int w, unsigned int h) { (void)bp; (void)bw; (void)psm; (void)x; (void)y; (void)w; (void)h; }

static unsigned int rng = 7;
static unsigned int rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }

static unsigned char vram_save[GS_VRAM_SIZE];
static unsigned int gen_save[GS_PAGES];
static unsigned int pix[1024 * 512];

static void fill_random(void)
{
    int i;
    for (i = 0; i < GS_VRAM_SIZE; i++) gsVram[i] = (unsigned char)((rnd() & 3) ? 0 : rnd());
}

int main(void)
{
    int failures = 0, it;

    gsMemInit();

    /* ---- A ---- */
    for (it = 0; it < 600; it++)
    {
        unsigned int bw = 1 + rnd() % 8, bp = (rnd() % 40) * 32 + ((rnd() & 1) ? 0 : (rnd() % 32));
        unsigned int x0 = rnd() % 70, y0 = rnd() % 70, w = 1 + rnd() % 200, h = 1 + rnd() % 90, x, y, p;
        unsigned char vram_a[GS_VRAM_SIZE];
        unsigned int gen_a[GS_PAGES];

        if (w > bw * 64 - 0) w = bw * 64;
        fill_random();
        for (y = 0; y < h; y++) for (x = 0; x < w; x++) pix[y * w + x] = (rnd() % 3) ? (unsigned int)rnd() : 0;
        memcpy(vram_save, gsVram, sizeof(gsVram));
        memcpy(gen_save, gsPageGen, sizeof(gsPageGen));

        for (y = 0; y < h; y++) for (x = 0; x < w; x++) gsMemWrite(GS_PSMCT32, bp, bw, x0 + x, y0 + y, pix[y * w + x]);
        memcpy(vram_a, gsVram, sizeof(gsVram));
        memcpy(gen_a, gsPageGen, sizeof(gsPageGen));

        memcpy(gsVram, vram_save, sizeof(gsVram));
        memcpy(gsPageGen, gen_save, sizeof(gsPageGen));
        gsMemWriteRect32(bp, bw, x0, y0, w, h, pix, w);

        if (memcmp(vram_a, gsVram, sizeof(gsVram)) != 0) { printf("FAIL A: memory differs (it %d)\n", it); failures++; }
        for (p = 0; p < GS_PAGES; p++)
        {
            if ((gen_a[p] != gen_save[p]) != (gsPageGen[p] != gen_save[p])) { printf("FAIL A: invalidated page set differs (it %d page %u)\n", it, p); failures++; break; }
        }
    }

    /* ---- B ---- */
    {
        int none = 0, region = 0, full = 0;

        for (it = 0; it < 1500; it++)
        {
            unsigned int bw = 1 + rnd() % 6;
            int rt_w = (rnd() % 8) ? (int)(bw * 64) : (int)(bw * 64 + 64 * (rnd() % 2));
            int rows = 1 + rnd() % 200;
            unsigned int fbp = rnd() % 150;
            unsigned int bp = ((rnd() % 4) ? ((rnd() % 150) * 32) : (rnd() % 4000));
            unsigned int x = (rnd() % 3) ? (rnd() % (bw * 64)) : (rnd() % 500);
            unsigned int y = rnd() % 260, w = 1 + rnd() % (bw * 64 + 40), h = 1 + rnd() % 120;
            int out[4] = { 0, 0, 0, 0 }, map, px, py;
            int aligned = (rnd() % 2);
            unsigned int rx, ry;
            static unsigned char vram_full[GS_VRAM_SIZE];

            if (aligned)
            {
                /* requests whose source buffer sits a whole number of page rows from the render target */
                int k = (int)(rnd() % 7) - 3;
                int page = (int)fbp - k * (int)bw;

                rt_w = (int)(bw * 64);
                if (page < 0) page = (int)fbp;
                bp = (unsigned int)page * 32;
                x = rnd() % (bw * 64);
                if (w > bw * 64 - x) w = bw * 64 - x;
            }

            /* RT pixels (already alpha-converted) */
            for (py = 0; py < rows; py++) for (px = 0; px < rt_w; px++) pix[py * rt_w + px] = 0xFF000000u | (unsigned int)rnd();

            fill_random();
            memcpy(vram_save, gsVram, sizeof(gsVram));

            gsMemWriteRect32(fbp * 32, bw, 0, 0, (unsigned int)rt_w, (unsigned int)rows, pix, (unsigned int)rt_w);
            memcpy(vram_full, gsVram, sizeof(gsVram));
            memcpy(gsVram, vram_save, sizeof(gsVram));

            map = gsRtReadRegion(rt_w, rows, fbp, bw, 1, bp, bw, 1, x, y, w, h, out);

            if (map == GS_RTMAP_NONE) none++;
            else if (map == GS_RTMAP_REGION)
            {
                region++;
                if (out[0] < 0 || out[1] < 0 || out[2] > rt_w || out[3] > rows || out[0] >= out[2] || out[1] >= out[3]) { printf("FAIL B: bad region\n"); failures++; continue; }
                gsMemWriteRect32(fbp * 32, bw, (unsigned int)out[0], (unsigned int)out[1], (unsigned int)(out[2] - out[0]), (unsigned int)(out[3] - out[1]),
                                 &pix[out[1] * rt_w + out[0]], (unsigned int)rt_w);
            }
            else
            {
                full++;
                gsMemWriteRect32(fbp * 32, bw, 0, 0, (unsigned int)rt_w, (unsigned int)rows, pix, (unsigned int)rt_w);
            }

            {
                static unsigned int got[400 * 200];
                unsigned int n = 0, bad = 0;

                for (ry = y; ry < y + h; ry++) for (rx = x; rx < x + w; rx++) got[n++] = gsMemRead(GS_PSMCT32, bp, bw, rx, ry);
                memcpy(gsVram, vram_full, sizeof(gsVram));
                n = 0;
                for (ry = y; ry < y + h; ry++) for (rx = x; rx < x + w; rx++) if (got[n++] != gsMemRead(GS_PSMCT32, bp, bw, rx, ry)) bad++;

                if (bad) { printf("FAIL B: %u request pixels differ (it %d, map %d)\n", bad, it, map); failures++; }
            }
        }

        printf("readback cases: %d none, %d region, %d full\n", none, region, full);
        if (!region || !none || !full) { printf("FAIL: not every branch was exercised\n"); failures++; }
    }

    printf(failures ? "gs_readback_test: %d FAILED\n" : "gs_readback_test: all checks passed\n", failures);
    return failures != 0;
}
