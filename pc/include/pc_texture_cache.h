/* GPU-resident static TIM2 asset registry for the PC GS renderer. */
#pragma once
#include <stdint.h>
#ifdef TARGET_PC
typedef struct PcGsTextureAssetState {
    uint64_t tex0, tex1, clamp, test, texa, texclut;
} PcGsTextureAssetState;
int pcGsTextureAssetCacheEnabled(void);
int pcGsTextureRendererReady(void);
int pcGsGetTextureAssetState(uint64_t asset_id, PcGsTextureAssetState* state);
int pcGsPreloadTextureAsset(uint64_t asset_id, const PcGsTextureAssetState* state, int width, int height);
void pcGsApplyTextureAssetState(uint64_t asset_id, const PcGsTextureAssetState* state);
void pcGsInvalidateTextureAsset(uint64_t asset_id);
void pcGsSetTextureAssetIdentity(uint64_t asset_id, int valid);
void pcGsResetTextureAssetCache(void);
#endif
