/* Opt-in PC texture reuse for static indexed TIM2 assets, keyed by TIM2 Gindex. */
#pragma once

#include <stdint.h>

#ifdef TARGET_PC
int pcGsTextureAssetCacheEnabled(void);
int pcGsTextureAssetCacheContains(unsigned int asset_id, uint64_t content_hash, uint64_t tex0, int width, int height);
void pcGsSetTextureAssetIdentity(unsigned int asset_id, int valid, uint64_t content_hash);
uint64_t pcGsGetTextureBindingGeneration(void);
void pcGsResetTextureAssetCache(void);
#endif
