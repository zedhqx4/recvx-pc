/*
 * Recorte de poligonos contra los planos de pantalla (scissor), sin memoria ni registros de VU0.
 * Reemplaza los micros 302 (SET_NODE_ARRAY), 327/333 (LOAD_SCISSOR_WORK*) y 339 (STORE_SCISSOR_WORK).
 * Un vertice son 4 cuartetos: posicion en espacio de clip, y tres cuartetos de atributos que se interpolan igual.
 * Independiente de la API grafica.
 */
#ifndef PC_SCISSOR_H
#define PC_SCISSOR_H

#include <stddef.h>
#include <stdint.h>

/* Carga el triangulo en el buffer 0 (4 vertices: el primero repetido al final).
 * h19/h18/h15 = posiciones de clip de los 3 vertices (historial de pcClipStep); pS/pP = VU1_STRIP_BUF / VU1_PRIM_BUF
 * del vertice actual (los desplazamientos son los de _Set_NodeArray). */
void pcScissorSetNode(const void* pS, const void* pP, const float* h19, const float* h18, const float* h15);

/* Recorta contra un plano: work0/work1 = 80 o 128 (buffer de origen y destino). Devuelve el numero de vertices. */
int pcScissorClipPlane(int mask1, int mask2, int xyzflg, float sin, int work0, int work1, int count);

/* Proyecta los `count` vertices del buffer 0 a VU1_PRIM_BUF consecutivos (stride = sizeof(VU1_PRIM_BUF)).
 * aspect = vf17.xy, offset = vf16.xy, proj = vf23.z. */
void pcScissorEmit(void* out, size_t stride, int count, const float* aspect, const float* offset, float proj);

#endif
