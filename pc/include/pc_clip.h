/*
 * Clip contra el volumen de vista (micros 269 y 237 de VU0), como funcion pura.
 * PcClipState es todo el estado que esas rutinas leian y escribian en registros:
 * historial de 3 vertices transformados (vf15/vf18/vf19), historial de clip de
 * 24 bits y los resultados vi01/vi02. Independiente de la API grafica.
 */
#ifndef PC_CLIP_H
#define PC_CLIP_H

#include <stdint.h>

typedef struct {
    float h15[4], h18[4], h19[4];   /* vertice actual, anterior y el anterior a ese (en espacio de clip) */
    uint32_t clip;                  /* historial de flags de clip: 6 bits por vertice, el ultimo abajo */
    float p14[4];                   /* ultimo vertice enviado a clip (vf14); 394 puede reutilizarlo sin recargarlo */
    int vi1;                        /* 237: el triangulo queda fuera de algun plano */
    int vi2;                        /* 237: 0 si algun plano lo descarta, 1 si no */
} PcClipState;

/* Registros de clip que en la PS2 eran vf14/15/18/19, vi1/vi2 y el registro de flags de VU0 (vi18). h18 lo comparte
 * pcVu0Last (ultimo punto transformado por la libreria de matrices), igual que en el hardware. */
extern PcClipState pcClipRegs;

/* kind = 0: micro 269 (historial y 3 flags); 1: micro 237 (ademas vi1/vi2); 2: micro 394 (solo el flag del vertice nuevo).
 * view = matriz de vista a clip (16 floats por filas). */
void pcClipStep(PcClipState* s, const float* p4, const float* view, int kind);

/* Materiales de color de los micros 237/394 (lo que eran vf20 diffuse, vf21 specular, vf22 ambient, vf23.w alpha).
 * Se calculan con las mismas reglas de la VU (truncado, denormales) y se alimentan desde los setters vu1Set*. */
void pcColorSetDiffuse(const float* rgb);    /* rgb del material, sin escalar */
void pcColorSetSpecula(const float* rgb);
void pcColorSetAmbient(const float* rgb);
void pcColorSetAlphaRatio(float ratio);      /* fVu1AlphaRatio ya multiplicado por 128 */
void pcClipSetColorMode(int mode);           /* usMode & 0xF de la tira en curso (INIT_CALC_COLOR) */

/* Vertice con color (micro 237: kind 1, micro 394: kind 2). `color` (rgba, 4 floats) entra con el color del vertice
 * y sale con el color final; p4 = NULL reutiliza el ultimo vertice enviado a clip. */
void pcClipColorVertex(const float* p4, int kind, float* color);

/* Puente con el codigo que aun usa registros VU0 (se retira en el paso final de 3d). */
void pcClipViewVolume(const float* p4, int all);

/* Registro de flags de clip (vi18, 24 bits): lo comparten pcClipStep, _Clip_ViewVolume y el scissor de triangulos. */
uint32_t pcClipFlagsGet(void);
void pcClipFlagsSet(uint32_t flags);

/* Vertice a espacio de clip con la matriz de vista y sus 6 flags (CLIPw): lo que hacia _Clip_ViewVolume.
 * Escribe out[4], empuja los flags al registro compartido y devuelve ese registro (24 bits). No toca el historial h15/h18/h19. */
int pcClipViewVertex(float* out, const float* in);

/* Flags de un punto en pantalla contra el area visible (_Clip_Screen): empuja y devuelve el registro de flags. */
int pcClipScreen(const float* p4);

/* Reinicia el historial de vertices de clip (vf15/vf18/vf19 = vf0): lo hacia el micro INIT_CALC_COLOR/PROCESS. */
void pcClipResetHistory(void);

#endif
