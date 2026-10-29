/* Opt-in PC texture reuse for static indexed TIM2 images. */
#pragma once

#ifdef TARGET_PC
int pcGsTextureSourceCacheEnabled(void);
int pcGsTextureSourceCacheContains(const void *source);
void pcGsSetTextureSourceIdentity(const void *source);
void pcGsResetTextureSourceCache(void);
#endif
