/* GPU-resident static TIM2 asset registry for the PC GS renderer. */
#pragma once

#include <stdint.h>

#ifdef TARGET_PC
int pcGsTextureAssetCacheEnabled(void);
int pcGsTextureRendererReady(void);
int pcGsTextureAssetCacheContains(uint64_t asset_id, uint64_t tex0, int width, int height);
int pcGsPreloadTextureAsset(uint64_t asset_id, uint64_t tex0, int width, int height);
void pcGsInvalidateTextureAsset(uint64_t asset_id);
void pcGsSetTextureAssetIdentity(uint64_t asset_id, int valid);
void pcGsResetTextureAssetCache(void);
#endif
