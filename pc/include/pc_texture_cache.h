/* Opt-in PC texture reuse for static indexed TIM2 assets, keyed by TIM2 Gindex. */
#pragma once

#include <stdint.h>

#ifdef TARGET_PC
int pcGsTextureAssetCacheEnabled(void);
int pcGsTextureAssetCacheContains(unsigned int asset_id, uint64_t content_hash, uint64_t tex0, int width, int height);
void pcGsSetTextureAssetIdentity(unsigned int asset_id, int valid, uint64_t content_hash);
uint64_t pcGsGetTextureBindingGeneration(void);
void pcGsResetTextureAssetCache(void);
int pcGsApplyAdList(const uint64_t* ad, int n);
/* Native (GS-less) texture path, active only with RECVX_GS_NATIVE=1 */
int pcGsNativeEnabled(void);
int pcGsNativeEnsureTim2Plane(const unsigned char* image, unsigned int image_size, unsigned int gindex,
                              uint64_t image_hash, int is_t4, int w, int h);
void pcGsNativeBindTbp(unsigned int tbp, unsigned int gindex, uint64_t image_hash, int is_t4, int w, int h);
void pcGsNativeBindClear(void);   /* direct A+D writes; 0 = GIF busy, use the packet path */
#endif
