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
    int vi1;                        /* 237: el triangulo queda fuera de algun plano */
    int vi2;                        /* 237: 0 si algun plano lo descarta, 1 si no */
} PcClipState;

/* all = 0: micro 269 (solo actualiza historial y flags); all = 1: micro 237 (ademas calcula vi1/vi2).
 * view = matriz de vista a clip (16 floats por filas). */
void pcClipStep(PcClipState* s, const float* p4, const float* view, int all);

/* Puente con el codigo que aun usa registros VU0 (se retira junto con el scissor).
 * clip_native: 0 = micro, 1 = nativa, 2 = ambas y compara (INI clip_native o RECVX_CLIP_NATIVE). */
void pcClipViewVolume(const float* p4, int all);

#endif
