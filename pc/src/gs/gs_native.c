/* PC port: native GS layer, step 1 (see gs_native.h). Pure data module, no GL. */
#include <stdlib.h>
#include <string.h>
#include "gs_native.h"
#include "gs_internal.h"

static GsNativeTex tex_tab[GS_NATIVE_MAX_TEX];
static int tex_next;

int gsNativeEnabled(void)
{
    static int cached = -1;
    if (cached < 0)
    {
        const char* e = getenv("RECVX_GS_NATIVE");
        cached = (e != NULL) && (e[0] == '1');
    }
    return cached;
}

static uint64_t fnv(const unsigned char* d, size_t n, uint64_t h)
{
    size_t i;
    for (i = 0; i < n; i++) h = (h ^ d[i]) * 0x100000001B3ULL;
    return h;
}

static void tex_free(GsNativeTex* t)
{
    free(t->pixels);
    free(t->clut);
    memset(t, 0, sizeof *t);
}

int gsNativeTexSet(unsigned int tbp, unsigned int cbp, int psm, int w, int h,
                   const unsigned char* pixels, const unsigned int* clut, unsigned int clut_entries)
{
    int indexed = (psm == GS_PSMT8) || (psm == GS_PSMT4);
    size_t bytes;
    GsNativeTex* t;
    int i, slot = -1;

    if (!pixels || w <= 0 || h <= 0 || (indexed && (!clut || !clut_entries))) return -1;
    bytes = (size_t)w * (size_t)h * (indexed ? 1 : 4);

    /* replace an entry for the same address, otherwise take the next slot (round robin) */
    for (i = 0; i < GS_NATIVE_MAX_TEX; i++)
        if (tex_tab[i].pixels && tex_tab[i].tbp == tbp && tex_tab[i].psm == psm) { slot = i; break; }
    if (slot < 0) { slot = tex_next; tex_next = (tex_next + 1) % GS_NATIVE_MAX_TEX; }
    t = &tex_tab[slot];
    tex_free(t);

    t->pixels = (unsigned char*)malloc(bytes);
    if (!t->pixels) return -1;
    memcpy(t->pixels, pixels, bytes);
    if (indexed)
    {
        t->clut = (unsigned int*)malloc(clut_entries * 4);
        if (!t->clut) { tex_free(t); return -1; }
        memcpy(t->clut, clut, clut_entries * 4);
        t->clut_entries = clut_entries;
    }
    t->tbp = tbp; t->cbp = cbp; t->psm = psm; t->w = w; t->h = h; t->indexed = indexed;
    t->content_hash = fnv(t->pixels, bytes, 0xCBF29CE484222325ULL);
    if (indexed) t->content_hash = fnv((const unsigned char*)t->clut, clut_entries * 4, t->content_hash);
    return slot;
}

const GsNativeTex* gsNativeTexFind(unsigned int tbp, unsigned int cbp, int psm, int w, int h)
{
    int i;
    for (i = 0; i < GS_NATIVE_MAX_TEX; i++)
    {
        const GsNativeTex* t = &tex_tab[i];
        if (t->pixels && t->tbp == tbp && t->psm == psm && t->w == w && t->h == h &&
            (!t->indexed || t->cbp == cbp))
            return t;
    }
    return NULL;
}

void gsNativeTexClear(void)
{
    int i;
    for (i = 0; i < GS_NATIVE_MAX_TEX; i++) tex_free(&tex_tab[i]);
    tex_next = 0;
}

/* ---- index-plane store ------------------------------------------------------------------- */
#define GS_NATIVE_MAX_PLANES 1024
#define GS_NATIVE_MAX_PLANE_BYTES (256u * 1024u * 1024u)

typedef struct { unsigned int asset_id; uint64_t hash; int psm, w, h; unsigned char* data; } GsNativePlane;
static GsNativePlane planes[GS_NATIVE_MAX_PLANES];
static int planes_next;
static size_t planes_bytes;

static void plane_free(GsNativePlane* p)
{
    if (p->data) planes_bytes -= (size_t)p->w * (size_t)p->h;
    free(p->data);
    memset(p, 0, sizeof *p);
}

const unsigned char* gsNativeIndexFind(unsigned int asset_id, uint64_t content_hash, int psm, int w, int h)
{
    int i;
    for (i = 0; i < GS_NATIVE_MAX_PLANES; i++)
    {
        const GsNativePlane* p = &planes[i];
        if (p->data && p->asset_id == asset_id && p->hash == content_hash && p->psm == psm && p->w == w && p->h == h)
            return p->data;
    }
    return NULL;
}

int gsNativeIndexStore(unsigned int asset_id, uint64_t content_hash, int psm, int w, int h, const unsigned char* indices)
{
    size_t bytes = (size_t)w * (size_t)h;
    GsNativePlane* p;

    if (!indices || w <= 0 || h <= 0) return -1;
    if (gsNativeIndexFind(asset_id, content_hash, psm, w, h)) return 0;
    while (planes_bytes + bytes > GS_NATIVE_MAX_PLANE_BYTES)
    {
        int guard = 0;
        while (guard++ < GS_NATIVE_MAX_PLANES && !planes[planes_next].data) planes_next = (planes_next + 1) % GS_NATIVE_MAX_PLANES;
        if (!planes[planes_next].data) return -1;
        plane_free(&planes[planes_next]);
    }
    p = &planes[planes_next];
    plane_free(p);
    p->data = (unsigned char*)malloc(bytes);
    if (!p->data) return -1;
    memcpy(p->data, indices, bytes);
    p->asset_id = asset_id; p->hash = content_hash; p->psm = psm; p->w = w; p->h = h;
    planes_bytes += bytes;
    planes_next = (planes_next + 1) % GS_NATIVE_MAX_PLANES;
    return 1;
}

uint64_t gsNativeIdentityGen(unsigned int asset_id, uint64_t content_hash)
{
    uint64_t h = 0x9E3779B97F4A7C15ULL ^ content_hash;
    h = (h ^ asset_id) * 0x100000001B3ULL;
    return h ? h : 1;
}

void gsNativeIndexClear(void)
{
    int i;
    for (i = 0; i < GS_NATIVE_MAX_PLANES; i++) plane_free(&planes[i]);
    planes_next = 0;
}

/* ---- game-facing API and TBP bindings ---------------------------------------------------- */
int pcGsNativeEnabled(void) { return gsNativeEnabled(); }

static int tim2_uses_p32(int w, int h) { return (w < 1024) && (h < 1024) && (w > 128) && (h > 128); }

static int tim2_buf_width(int w)
{
    int tbw = (w + 63) / 64;
    return (tbw & 1) ? tbw + 1 : tbw;
}

int pcGsNativeEnsureTim2Plane(const unsigned char* image, unsigned int image_size, unsigned int gindex,
                              uint64_t image_hash, int is_t4, int w, int h)
{
    int psm = is_t4 ? GS_PSMT4 : GS_PSMT8;
    size_t expect;
    unsigned char* plane;
    int ok;

    if (!gsNativeEnabled() || !image || w <= 0 || h <= 0 || w > 1024 || h > 1024)
        return 0;
    if (gsNativeIndexFind(gindex, image_hash, psm, w, h))
        return 1;

    expect = (size_t)w * (size_t)h / (is_t4 ? 2u : 1u);
    if (image_size < expect)
        return 0;

    plane = (unsigned char*)malloc((size_t)w * (size_t)h);
    if (!plane)
        return 0;
    ok = gsMemDecodeTim2Indices(image, is_t4, tim2_uses_p32(w, h), w, h, tim2_buf_width(w), plane);
    if (ok)
        ok = gsNativeIndexStore(gindex, image_hash, psm, w, h, plane) >= 0;
    free(plane);
    return ok;
}

#define GS_NATIVE_MAX_BINDS 128
typedef struct {
    int used;
    unsigned int tbp, gindex;
    uint64_t hash;
    int psm, w, h, tbw;
    unsigned int pages;
    unsigned long long gen_sum;
} GsNativeBind;
static GsNativeBind binds[GS_NATIVE_MAX_BINDS];

static unsigned int bind_pages(int psm, int w, int h)
{
    unsigned long bytes = (unsigned long)w * (unsigned long)h / (psm == GS_PSMT4 ? 2u : 1u);
    return (unsigned int)((bytes + 8191u) / 8192u) + 1u;   /* +1: TBP need not be page aligned */
}

static unsigned long long bind_gen_sum(unsigned int tbp, unsigned int pages)
{
    unsigned long long sum = 0;
    unsigned int i;
    for (i = 0; i < pages; i++) sum += gsPageGen[((tbp >> 5) + i) & (GS_PAGES - 1)];
    return sum;
}

void pcGsNativeBindTbp(unsigned int tbp, unsigned int gindex, uint64_t image_hash, int is_t4, int w, int h)
{
    int i, slot = -1;
    GsNativeBind* b;

    if (!gsNativeEnabled()) return;
    for (i = 0; i < GS_NATIVE_MAX_BINDS; i++)
    {
        if (binds[i].used && binds[i].tbp == tbp) { slot = i; break; }
        if (!binds[i].used && slot < 0) slot = i;
    }
    if (slot < 0) return;   /* table full: the draw falls back to the VRAM path */
    b = &binds[slot];
    b->used = 1; b->tbp = tbp; b->gindex = gindex; b->hash = image_hash;
    b->psm = is_t4 ? GS_PSMT4 : GS_PSMT8; b->w = w; b->h = h; b->tbw = tim2_buf_width(w);
    b->pages = bind_pages(b->psm, w, h);
    b->gen_sum = bind_gen_sum(tbp, b->pages);
}

void pcGsNativeBindClear(void) { memset(binds, 0, sizeof binds); }

int gsNativeTbpLookup(unsigned int tbp, unsigned int* gindex, uint64_t* image_hash, int* psm, int* w, int* h, int* tbw)
{
    int i;
    if (!gsNativeEnabled()) return 0;
    for (i = 0; i < GS_NATIVE_MAX_BINDS; i++)
    {
        const GsNativeBind* b = &binds[i];
        if (b->used && b->tbp == tbp)
        {
            if (bind_gen_sum(b->tbp, b->pages) != b->gen_sum)
                return 0;   /* a real GS write hit those pages after the bind */
            if (!gsNativeIndexFind(b->gindex, b->hash, b->psm, b->w, b->h))
                return 0;   /* plane evicted: the caller falls back to the VRAM path */
            *gindex = b->gindex; *image_hash = b->hash; *psm = b->psm; *w = b->w; *h = b->h; *tbw = b->tbw;
            return 1;
        }
    }
    return 0;
}
