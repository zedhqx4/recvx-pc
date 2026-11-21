/*
 * Recorte de un poligono (lista de NODE) contra un plano de pantalla: ScissorTriangle sin registros de VU0.
 * Un NODE son 4 cuartetos (vertice, color, texUV, clipV) con el mismo diseno que `NODE` del juego; los cuatro
 * se interpolan igual. Independiente de la API grafica.
 */
#ifndef PC_TRI_SCISSOR_H
#define PC_TRI_SCISSOR_H

#include <stdint.h>

typedef struct { float vertex[4], color[4], texUV[4], clipV[4]; } PcTriNode;

/* Recorta `in` (nIn nodos, poligono cerrado) contra un plano. sgn = +-1, mask = clipmask del plano, axis = 0..2.
 * *clipHist es el historial de flags de clip de 24 bits (vi18): se actualiza igual que lo hacian los dos CLIPw por
 * arista. Devuelve el numero de nodos escritos en `out`. */
int pcTriScissorPlane(const PcTriNode* in, int nIn, PcTriNode* out, float sgn, int mask, int axis, uint32_t* clipHist);

#endif
