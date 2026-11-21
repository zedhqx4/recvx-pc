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

/* Parametros de pantalla que usa la proyeccion (antes vf16 = offset, vf17 = aspecto, vf23.z = proyeccion). Los fijan
 * vu1SetScreenOffset / vu1SetScreenAspect / vu1SetScreenProjection con la misma normalizacion que tenia el registro. */
typedef struct { float offset[2]; float aspect[2]; float proj; } PcScreen;
void pcScreenSetOffset(float x, float y);
void pcScreenSetAspect(float w, float h);
void pcScreenSetProjection(float proj);
const PcScreen* pcScreen(void);

/* Proyecta `count` vertices a VU1_PRIM_BUF consecutivos (stride = outStride). Cada vertice de origen son cuatro cuartetos
 * separados por srcStride; offVertex/offTexUV/offColor dicen donde esta cada uno (en bytes). zFirst elige el orden de
 * las multiplicaciones (las dos rutas del juego lo hacen distinto y los redondeos difieren en 1 ULP). */
void pcScissorProject(void* out, size_t outStride, int count, const void* src, size_t srcStride,
                      size_t offVertex, size_t offTexUV, size_t offColor, int zFirst);

/* Proyecta los `count` vertices del buffer 0 a VU1_PRIM_BUF consecutivos (stride = sizeof(VU1_PRIM_BUF)). */
void pcScissorEmit(void* out, size_t stride, int count);

#endif
