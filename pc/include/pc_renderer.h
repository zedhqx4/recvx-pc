/* Native OpenGL renderer interface. No PS2 command streams or emulated GPU state. */
#pragma once

#include <stdint.h>
#include <stddef.h>

typedef struct {
    float x, y, z;             /* view-space position; z is positive in front of the camera */
    float r, g, b, a;          /* linear vertex color, 0..1 */
    float u, v;                /* source texture coordinates, in texels */
    float q;                   /* source Q for screen-space perspective interpolation */
    float fog;                 /* fog visibility, 0..1 */
} PcNativeVertex;

typedef struct {
    unsigned int texture;
    float texture_width, texture_height;
    int texture_function, texture_alpha;
    int texture_perspective;
    int premultiplied_alpha;
    int textured;
    int blend;
    int depth_test;
    int depth_write;
    int alpha_test;
    float alpha_ref;
    int linear_filter;
    int screen_space;
    int cull_backfaces;
    int ot_z_valid;            /* 1: ot_z holds the GS Z (0..65534, bigger = nearer) used to order the OT */
    float ot_z;
} PcNativeDrawState;

int pcRendererInit(int width, int height);
void pcRendererShutdown(void);
void pcRendererBeginFrame(void);
void pcRendererPresent(void);
float pcRendererDepthFromViewZ(float view_z);
float pcRendererDepthFromReciprocalZ(float reciprocal_z);
void pcRendererSetProjection(float projection, float aspect_w, float aspect_h,
                             float offset_x, float offset_y, float near_clip, float far_clip);
void pcRendererSetRenderScale(float scale_x, float scale_y);
void pcRendererSetClearColor(float r, float g, float b, float a);
void pcRendererClear(float r, float g, float b, float a, int clear_depth);
void pcRendererSetDepthMode(int depth_test, int depth_write);
void pcRendererSetScissor(int enabled, int x, int y, int width, int height);
void pcRendererSetGlobalFog(int enabled, float r, float g, float b);
void pcRendererSetBlendFactors(int src_rgb, int dst_rgb, int src_alpha, int dst_alpha);
void pcRendererDrawTriangleStrip(const PcNativeVertex* vertices, int count, const PcNativeDrawState* state);
void pcRendererDrawTriangles(const PcNativeVertex* vertices, int count, const PcNativeDrawState* state);
void pcRendererDrawPoints(const PcNativeVertex* vertices, int count, const PcNativeDrawState* state);
void pcRendererDrawLines(const PcNativeVertex* vertices, int count, const PcNativeDrawState* state);
void pcRendererDrawLineStrip(const PcNativeVertex* vertices, int count, const PcNativeDrawState* state);
void pcRendererDrawScreenTriangles(const PcNativeVertex* vertices, int count, const PcNativeDrawState* state);
void pcRendererFlushTransparent(void);
unsigned int pcRendererLoadTextureRGBA(const unsigned char* rgba, int width, int height, int linear);
int pcRendererUpdateTextureRGBA(unsigned int texture, const unsigned char* rgba, int width, int height);
void pcRendererDeleteTexture(unsigned int texture);
void pcRendererBindTexture(unsigned int texture);
unsigned int pcRendererCurrentTexture(void);
void pcRendererCurrentTextureSize(float* width, float* height);
void pcRendererSetTextureCombine(int function, int texture_alpha);
void pcRendererCurrentTextureCombine(int* function, int* texture_alpha);
void pcRendererConfigureTexture(unsigned int texture, int linear, int wrap_u, int wrap_v);
int pcRendererGetOutputSize(int* width, int* height);
