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
