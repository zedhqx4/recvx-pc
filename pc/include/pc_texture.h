/* Native texture services shared between the game sources and the PC renderer layer.
 * Only plain C types: safe to include from files compiled with the long redefinition. */
#ifndef PC_TEXTURE_H
#define PC_TEXTURE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PcTextureDebugInfo {
    unsigned int texture;
    unsigned int gindex;
    unsigned int bank;
    unsigned short width;
    unsigned short height;
    unsigned char image_type;
    unsigned char pict_format;
} PcTextureDebugInfo;

/* Snapshot-style enumeration of decoded TIM2 textures currently held by the native cache. */
int pcTextureDebugCount(void);
int pcTextureDebugGet(int index, PcTextureDebugInfo* out);

/* Capture the current 512x512 render area into the render texture bound to `image` (the
 * TIM2 payload of a render texture). GPU->GPU copy; falls back to a full CPU readback
 * written into `image` (RGBA, alpha 0x80) when the copy fails or INI rtt_gpu_copy=0. */
void pcTextureCaptureScreen(void* image);

/* Drop cached decoded sources / the captured state of one render texture. */
void pcTextureInvalidateSources(void);
void pcTextureInvalidateRenderTarget(const void* image);

#ifdef __cplusplus
}
#endif

#endif
