/* Host test for the CLUT handling in gs.c: palette generation, memoized palette hash and the
 * "flush queued primitives only when the palette really changes" rule.
 *
 *   cc -std=gnu11 -I../include -I../src -I../src/gs gs_clut_test.c ../src/gs/gs.c ../src/gs/gs_mem.c -o clut_test && ./clut_test
 */
#include <stdio.h>
#include <string.h>

#include "../src/gs/gs_internal.h"

extern unsigned int gsClutGen, gsStatClutHashes, gsStatClutHashMemo;

static int draws;
static int failures;

void gsRendererInit(void) {}
void gsRendererPresent(unsigned int a, unsigned int b) { (void)a; (void)b; }
void gsRendererDraw(const GsDrawState* st, const GsVertex* v, int n) { (void)st; (void)v; (void)n; draws++; }
void gsXferDone(unsigned int bp, unsigned int bw, unsigned int psm, unsigned int x, unsigned int y, unsigned int w, unsigned int h, int c) { (void)bp; (void)bw; (void)psm; (void)x; (void)y; (void)w; (void)h; (void)c; }
void gsFlushLocalToLocal(unsigned int bp, unsigned int bw, unsigned int psm, unsigned int x, unsigned int y, unsigned int w, unsigned int h) { (void)bp; (void)bw; (void)psm; (void)x; (void)y; (void)w; (void)h; }
int pcFontFilterDrawActive(void) { return 0; }
int pcFontFilterIsLinear(void) { return 0; }
void pcLog(const char* f, ...) { (void)f; }

#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } } while (0)

static void queue_triangle(void)
{
    gsWriteReg(0x00, 3);                 /* PRIM: triangle */
    gsWriteReg(0x05, 0x0000000000100010ULL);
    gsWriteReg(0x05, 0x0000000000200010ULL);
    gsWriteReg(0x05, 0x0000000000100020ULL);
}

static u64 make_tex0(unsigned int cbp, unsigned int psm, unsigned int csa, unsigned int cld)
{
    return (u64)0 | ((u64)2 << 14) | ((u64)psm << 20) | ((u64)7 << 26) | ((u64)7 << 30) | ((u64)1 << 34) |
           ((u64)cbp << 37) | ((u64)0 << 51) | ((u64)0 << 55) | ((u64)csa << 56) | ((u64)cld << 61);
}

int main(void)
{
    unsigned int clut[256];
    unsigned int cbp = 0x100, i, g0;
    u64 t8 = make_tex0(cbp, GS_PSMT8, 0, 2);
    u64 h1, h2, h3;
    int d0;

    gsMemInit();

    for (i = 0; i < 256; i++)
    {
        gsMemWrite(GS_PSMCT32, cbp, 1, i & 15, i >> 4, 0xFF000000u | (i * 0x010203u));
    }

    /* 1. first load: palette differs from the empty buffer -> generation moves, content is right */
    g0 = gsClutGen;
    gsWriteReg(0x06, t8);
    CHECK(gsClutGen == g0 + 1, "first load bumps generation");

    gsReadClut32(t8, 0, 256, clut);
    for (i = 0; i < 256; i++)
    {
        unsigned int j = (i & 0xE7) | ((i & 0x08) << 1) | ((i & 0x10) >> 1);
        unsigned int want = gsMemRead(GS_PSMCT32, cbp, 1, j & 15, j >> 4);

        if (clut[i] != want) { CHECK(0, "CSM1 palette entry mismatch"); break; }
    }

    /* 2. same load again with a primitive queued: no flush, no generation change */
    queue_triangle();
    d0 = draws;
    g0 = gsClutGen;
    gsWriteReg(0x06, t8);
    CHECK(gsClutGen == g0, "identical reload keeps generation");
    CHECK(draws == d0, "identical reload does not flush queued primitives");

    /* 3. hash memo: second call is a memo hit and returns the same value */
    h1 = gsClutHash(t8);
    {
        unsigned int memo0 = gsStatClutHashMemo;

        h2 = gsClutHash(t8);
        CHECK(h1 == h2, "memoized hash equals computed hash");
        CHECK(gsStatClutHashMemo == memo0 + 1, "second hash call is a memo hit");
    }

    /* 4. a real palette change flushes the queued primitive, bumps generation, changes the hash */
    gsMemWrite(GS_PSMCT32, cbp, 1, 3, 3, 0xFF123456u);
    d0 = draws;
    g0 = gsClutGen;
    gsWriteReg(0x06, t8);
    CHECK(draws == d0 + 1, "palette change flushes queued primitives first");
    CHECK(gsClutGen == g0 + 1, "palette change bumps generation");
    h3 = gsClutHash(t8);
    CHECK(h3 != h1, "palette change changes hash");

    /* 5. hash after regeneration equals a from-scratch recomputation (different memo slot path) */
    {
        unsigned int c2[256];
        u64 ref = 1469598103934665603ULL;

        gsReadClut32(t8, 0, 256, c2);
        for (i = 0; i < 256; i++)
        {
            ref = (ref ^ (c2[i] & 0xFFFF)) * 1099511628211ULL;
            ref = (ref ^ (c2[i] >> 16)) * 1099511628211ULL;
        }

        CHECK(ref == h3, "hash equals reference FNV over the palette halves");
    }

    printf(failures ? "gs_clut_test: %d FAILED\n" : "gs_clut_test: all checks passed\n", failures);
    return failures != 0;
}
