/* Opt-in PC texture reuse for static indexed TIM2 images. */
#pragma once

#ifdef TARGET_PC
int pcGsTextureSourceCacheEnabled(void);
int pcGsTextureSourceCacheContains(const void *source, unsigned long long tex0, int width, int height);
void pcGsSetTextureSourceIdentity(const void *source);
void pcGsResetTextureSourceCache(void);
#endif
