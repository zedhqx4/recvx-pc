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
    int screen_space;          /* 0: espacio de vista; 1: pixeles (z = profundidad); 2: pixeles homogeneos, w = 1/q (recorte en GPU);
                                  3: igual que 2 pero los vertices llegan CRUDOS (registro de 12 floats de la VU1 sin el campo de
                                  flags: x=X y=Y z=Z, r..a = RGBA en 0..255, u=S v=T, q=Q, fog=F) y el vertex shader hace la
                                  conversion a GS (punto fijo 12.4, Z entero de 16 bits, FTOI0 de color y niebla, S/Q);
                                  4: registros crudos del empaquetador del EE (Ps2AddPrim3D): posicion en pantalla NO homogenea (w=1),
                                  Z de la GS = (int)(sz*16)/16 desde Q, XY = (int)(X*16)/16, color 0..255, S/Q en el fragmento */
    int cull_backfaces;
    int ot_z_valid;            /* 1: ot_z holds the GS Z (0..65534, bigger = nearer) used to order the OT */
    float ot_z;
    int ot_coarse;             /* 1: OT bucket = Z >> 12 instead of Z >> 4 (Ps2AddOT with Ps2_ice_flag) */
    int fog;                   /* PRIM.FGE: apply the global fog color using the per-vertex fog factor */
    int ot_queue;              /* 1: goes through the OT even without ABE (2D: the GS ordered it by Z bucket, then by submission) */
    int immediate;             /* blended primitive that the game draws at once instead of through the OT */
    float zbuff_a, zbuff_b;    /* screen_space 3 y 4: constantes de Z de la GS, sz = -a + q * -b (0 en los demas modos) */
} PcNativeDrawState;

int pcRendererInit(int width, int height);
void pcRendererShutdown(void);
void pcRendererBeginFrame(void);
void pcRendererPresent(void);
float pcRendererDepthFromViewZ(float view_z);
float pcRendererDepthFromReciprocalZ(float reciprocal_z);
/* GS Z (0..65534, bigger = nearer, as written by the VU0 packer) -> GL depth (0 = nearest). */
float pcRendererDepthFromGsZ(float gs_z);
void pcRendererSetProjection(float projection, float aspect_w, float aspect_h,
                             float offset_x, float offset_y, float near_clip, float far_clip);
void pcRendererSetRenderScale(float scale_x, float scale_y);
void pcRendererSetClearColor(float r, float g, float b, float a);
void pcRendererClear(float r, float g, float b, float a, int clear_depth);
void pcRendererSetDepthMode(int depth_test, int depth_write);
void pcRendererSetScissor(int enabled, int x, int y, int width, int height);
void pcRendererSetGlobalFog(int enabled, float r, float g, float b);
/* Ecuacion de mezcla de la GS: color = (A - B) * C / 128 + D. A, B y D son uno de {Cs, Cd, 0}; C es uno de {As, Ad, FIX}.
 * fix es ALPHA.FIX (0..255; 128 = 1.0). Los valores numericos de los enums coinciden con los campos del registro ALPHA. */
enum { PC_BLEND_CS = 0, PC_BLEND_CD = 1, PC_BLEND_ZERO = 2 };
enum { PC_BLEND_AS = 0, PC_BLEND_AD = 1, PC_BLEND_FIX = 2 };
typedef struct PcBlendEquation {
    unsigned char a, b, c, d;
    unsigned char fix;
} PcBlendEquation;
/* Mezcla del contexto 1 (el resto de las superficies) y del contexto 2 (mascara de sombra fuera de pantalla). */
void pcRendererSetBlend(const PcBlendEquation* equation);
void pcRendererSetBlend2(const PcBlendEquation* equation);
void pcRendererDrawTriangleStrip(const PcNativeVertex* vertices, int count, const PcNativeDrawState* state);
void pcRendererDrawTriangles(const PcNativeVertex* vertices, int count, const PcNativeDrawState* state);
void pcRendererDrawPoints(const PcNativeVertex* vertices, int count, const PcNativeDrawState* state);
void pcRendererDrawLines(const PcNativeVertex* vertices, int count, const PcNativeDrawState* state);
void pcRendererDrawLineStrip(const PcNativeVertex* vertices, int count, const PcNativeDrawState* state);
void pcRendererDrawScreenTriangles(const PcNativeVertex* vertices, int count, const PcNativeDrawState* state);
void pcRendererFlushTransparent(void);
void pcRendererFlushTransparentI(void);   /* Ps2DrawOTagI: dos pasadas (alfa >= 128 con Z, despues alfa < 128 sin Z) */
unsigned int pcRendererLoadTextureRGBA(const unsigned char* rgba, int width, int height, int linear);
int pcRendererUpdateTextureRGBA(unsigned int texture, const unsigned char* rgba, int width, int height);
void pcRendererDeleteTexture(unsigned int texture);
void pcRendererBindTexture(unsigned int texture);
unsigned int pcRendererCurrentTexture(void);
void pcRendererCurrentTextureSize(float* width, float* height);
/* Debug-only readback of a registered native texture. RGBA rows are returned top-to-bottom. */
int pcRendererReadTextureRGBA(unsigned int texture, unsigned char* rgba, size_t capacity,
                              int* width, int* height);
void pcRendererSetTextureCombine(int function, int texture_alpha);
void pcRendererCurrentTextureCombine(int* function, int* texture_alpha);
void pcRendererConfigureTexture(unsigned int texture, int linear, int wrap_u, int wrap_v);
int pcRendererGetOutputSize(int* width, int* height);
/* Superficie de render-to-texture (la copia reducida de la pantalla que el juego usa como textura de 256x256). */
void pcRendererCopyScreenToRenderTexture(int sx0, int sy0, int sx1, int sy1, int dx0, int dy0, int dx1, int dy1);
void pcRendererBindRenderTexture(void);
uint64_t pcRendererFrameNumber(void);
void pcRendererCaptureBurst(int frames);   /* guarda los N frames siguientes como recvx_burst_K.bmp (F8) */
int pcRendererReadScreenRect(int x, int y, int w, int h, unsigned char* rgba);
/* Copy a screen rectangle directly into a persistent GPU texture; no CPU readback. */
int pcRendererCopyScreenRectToTexture(unsigned int* texture, int x, int y, int w, int h, int image_width, int image_height);
/* Shadow volumes (Ps2ShadowStart..End). The GS drew the volume faces into a second 16-bit frame (FRAME_2, FBP 380)
 * that shares the scene Z buffer; here that frame is an offscreen target that shares the scene depth texture.
 *   Begin   : context 2 becomes the draw target and its mask is cleared
 *   Compose : darkens the scene by `shade` (0..255) wherever the mask got covered inside [x0,x1)x[y0,y1) (pixels of the
 *             640x480 frame), then clears that rectangle of the mask
 *   End     : back to the scene target */
void pcRendererShadowBegin(void);
void pcRendererShadowCompose(float x0, float y0, float x1, float y1, int shade);
void pcRendererShadowEnd(void);
