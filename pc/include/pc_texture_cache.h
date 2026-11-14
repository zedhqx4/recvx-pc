/* Opt-in PC texture reuse for static indexed TIM2 assets, keyed by the original texture-list pointer. */
#pragma once

#include <stdint.h>

#ifdef TARGET_PC
int pcGsTextureAssetCacheEnabled(void);
int pcGsTextureAssetCacheContains(const void* source, uint64_t content_hash, uint64_t tex0, int width, int height);
void pcGsSetTextureAssetIdentity(const void* source, int valid, uint64_t content_hash);
uint64_t pcGsGetTextureBindingGeneration(void);
void pcGsResetTextureAssetCache(void);
#endif
