/* PC port: native (non-emulated) GS layer, step 1.
 *
 * Goal: let the game hand textures/palettes to the renderer as linear pixel data instead of
 * swizzling them into emulated GS VRAM and decoding them back. Disabled unless the environment
 * variable RECVX_GS_NATIVE=1 is set; the emulated path is untouched and stays the fallback. */
#pragma once

#include <stdint.h>

#define GS_NATIVE_MAX_TEX 256

typedef struct {
    unsigned int tbp;        /* TEX0.TBP0 the game would have used (lookup key) */
    unsigned int cbp;        /* TEX0.CBP of its palette (0 when not indexed) */
    int psm;                 /* GS_PSMT4 / GS_PSMT8 / GS_PSMCT32 ... */
    int w, h;
    int indexed;
    unsigned int clut_entries;
    unsigned char* pixels;   /* w*h bytes (indexed, 1 index/byte) or w*h*4 (RGBA8) */
    unsigned int* clut;      /* clut_entries RGBA8 (A in 0..0x80 GS scale) or NULL */
    uint64_t content_hash;
} GsNativeTex;

int gsNativeEnabled(void);                                      /* RECVX_GS_NATIVE=1 */
int gsNativeTexSet(unsigned int tbp, unsigned int cbp, int psm, int w, int h,
                   const unsigned char* pixels, const unsigned int* clut, unsigned int clut_entries);
const GsNativeTex* gsNativeTexFind(unsigned int tbp, unsigned int cbp, int psm, int w, int h);
void gsNativeTexClear(void);

/* Index-plane store: one decoded 8-bit index plane per (asset id, source content hash, size, PSM).
 * It is the native source of truth for a static indexed TIM2: once stored, the image never has to
 * be (re)uploaded to or decoded from emulated VRAM again. Bounded; oldest entries are dropped. */
const unsigned char* gsNativeIndexFind(unsigned int asset_id, uint64_t content_hash, int psm, int w, int h);
int gsNativeIndexStore(unsigned int asset_id, uint64_t content_hash, int psm, int w, int h, const unsigned char* indices);
uint64_t gsNativeIdentityGen(unsigned int asset_id, uint64_t content_hash);   /* cache key independent of VRAM pages */
void gsNativeIndexClear(void);
