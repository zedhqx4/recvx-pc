/*
 * CALCPOINT nativo: transformacion de vertices de Ninja (posicion + normal) a
 * espacio de vista y de pantalla, iluminacion de hasta 4 luces y flags de clip.
 * Funcion pura: todo lo que antes vivia en los registros de VU0 (vf17/vf23/vf24-31,
 * vi7-vi10, vf4-vf14) llega en PcCalcPointCtx. Independiente de la API grafica.
 */
#ifndef PC_CALCPOINT_H
#define PC_CALCPOINT_H

#include <stdint.h>

typedef struct {
    const float* cn;            /* matriz de Ninja (CN), 16 floats por filas */
    const float* view;          /* matriz de vista a clip (ClipMatrix2), 16 floats por filas */
    float aspect[2];            /* fVu1AspectW/H */
    float projection;           /* fVu1Projection */
    const float (*light)[4];    /* bloque v[11][4] de njCnkCvVnPs2 */
} PcCalcPointCtx;

/* Estado global alimentado por los setters del juego (NaView / Vu1Strip) */
void pcViewSetClipMatrix(const float* m16);
const float* pcViewClipMatrix(void);

/* n vertices de 8 floats (pos xyz+w, normal xyz+w) -> n VU1_STRIP_BUF (64 bytes).
 * Escribe color (+16), posicion de vista (+32, w = fog sin convertir), proyeccion (+48).
 * clip_and recibe el AND de los 6 bits de clip de todos los vertices. */
void pcCalcPoint(const PcCalcPointCtx* c, const float* in, void* out, int n, uint32_t* clip_and);

#endif
