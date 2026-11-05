/* Host test: the PSMT8/PSMT4 block upload paths in gs_mem.c must leave GS memory, page
 * generations and transfer-done notifications identical to the per-pixel reference.
 *
 *   cc -O2 -DGS_BLOCK_FASTPATH=1 -I../src/gs gs_mem_blockpath_test.c -o fast
 *   cc -O2 -DGS_BLOCK_FASTPATH=0 -I../src/gs gs_mem_blockpath_test.c -o ref
 *   ./fast > fast.txt; ./ref > ref.txt; cmp fast.txt ref.txt
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/gs/gs_mem.c"

static unsigned long long done_hash;
static unsigned int done_count;

void gsXferDone(unsigned int bp, unsigned int bw, unsigned int psm, unsigned int x, unsigned int y, unsigned int w, unsigned int h, int changed)
{
    done_hash = (done_hash * 1099511628211ULL) ^ (bp | ((u64)bw << 14) | ((u64)psm << 20) | ((u64)x << 26) | ((u64)y << 38) | ((u64)changed << 50));
    done_hash = (done_hash * 1099511628211ULL) ^ (w | ((u64)h << 16));
    done_count++;
}

void gsFlushLocalToLocal(unsigned int bp, unsigned int bw, unsigned int psm, unsigned int x, unsigned int y, unsigned int w, unsigned int h)
{
    (void)bp; (void)bw; (void)psm; (void)x; (void)y; (void)w; (void)h;
}

static unsigned int rng = 1;
static unsigned int rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }

static unsigned long long fnv(const void* p, size_t n)
{
    const unsigned char* b = p;
    unsigned long long h = 1469598103934665603ULL;
    size_t i;
    for (i = 0; i < n; i++) h = (h ^ b[i]) * 1099511628211ULL;
    return h;
}

int main(void)
{
    static unsigned char data[1 << 20];
    int iter;

    gsMemInit();

    for (iter = 0; iter < 4000; iter++)
    {
        int t4 = rnd() & 1;
        int aligned = (rnd() % 4) != 0;
        unsigned int unit = t4 ? 32 : 16;
        unsigned int bw = ((rnd() % 4) + 1) * 2;              /* even, as the game uses for 4/8-bit */
        unsigned int bp = (rnd() % 64) * 32;
        unsigned int w = aligned ? unit * (1 + rnd() % 6) : 1 + rnd() % 200;
        unsigned int h = aligned ? 16 * (1 + rnd() % 6) : 1 + rnd() % 100;
        unsigned int sx = aligned ? unit * (rnd() % 3) : rnd() % 40;
        unsigned int sy = aligned ? 16 * (rnd() % 3) : rnd() % 20;
        size_t total = t4 ? ((size_t)w * h + 1) / 2 : (size_t)w * h;
        size_t padded = (total + 15) & ~(size_t)15;
        size_t off = 0;
        GsRegs r;
        int same = (rnd() % 3) == 0;
        size_t k;

        memset(&r, 0, sizeof(r));
        r.bitbltbuf = ((u64)(t4 ? GS_PSMT4 : GS_PSMT8) << 56) | ((u64)bw << 48) | ((u64)bp << 32);
        r.trxpos = ((u64)sy << 48) | ((u64)sx << 32);
        r.trxreg = ((u64)h << 32) | w;
        r.trxdir = 0;

        if (padded > sizeof(data)) continue;

        if (!same || iter == 0)
        {
            for (k = 0; k < padded; k++) data[k] = (unsigned char)((rnd() % 5) ? (k * 7 + iter) : rnd());
        }

        gsMemStartTransfer(&r);

        while (off < padded)
        {
            size_t chunk = ((rnd() % 3) == 0) ? 16 * (1 + rnd() % 64) : padded;   /* split the packet at random */
            if (off + chunk > padded) chunk = padded - off;
            gsMemTransferData(data + off, (int)(chunk / 16));
            off += chunk;
        }
    }

    printf("vram %016llx\n", fnv(gsVram, sizeof(gsVram)));
    printf("gen  %016llx\n", fnv(gsPageGen, sizeof(gsPageGen)));
    printf("done %016llx %u\n", done_hash, done_count);
    return 0;
}
