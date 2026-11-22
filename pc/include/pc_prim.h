#ifndef PC_PRIM_H
#define PC_PRIM_H

/* Descriptor de primitiva que el juego entrega al renderer nativo (Ps2AddPrim2D, Ps2AddPrim3D*, vu1Draw*).
 * Es el campo PRIM del hardware original (11 bits) colocado desde el bit 47 de un entero de 64 bits:
 *   bits 47..49 topologia, 50 sombreado Gouraud, 51 textura, 52 niebla, 53 mezcla alfa, 54 antialias, 55 UV fijo (sin Q).
 * El renderer solo lee topologia, textura, niebla, mezcla y UV fijo; el resto se conserva por compatibilidad con los productores. */
#define PC_PRIM_IIP  0x0004000000000000ULL   /* sombreado Gouraud */
#define PC_PRIM_TME  0x0008000000000000ULL   /* textura */
#define PC_PRIM_FGE  0x0010000000000000ULL   /* niebla por vertice */
#define PC_PRIM_ABE  0x0020000000000000ULL   /* mezcla alfa (el primitivo espera en la cola de transparencias) */
#define PC_PRIM_AA1  0x0040000000000000ULL   /* antialias (el renderer no lo lee) */
#define PC_PRIM_FST  0x0080000000000000ULL   /* UV en punto fijo 12.4 en lugar de ST/Q */
#define PC_PRIM_CTXT 0x0100000000000000ULL   /* contexto 2 (el renderer no lo lee) */
#define PC_PRIM_FIX  0x0200000000000000ULL   /* control de fragmentos (el renderer no lo lee) */
#define PC_PRIM_SET_TOPOLOGY(topology)  ((topology) * 0x0000800000000000ULL)   /* sin conversion de tipo: los archivos del juego redefinen `long` */

#define PC_PRIM_TOPOLOGY(prim)  ((unsigned int)(((prim) >> 47) & 7u))
enum { PC_PRIM_POINT = 0, PC_PRIM_LINE = 1, PC_PRIM_LINE_STRIP = 2, PC_PRIM_TRIANGLE = 3, PC_PRIM_TRIANGLE_STRIP = 4,
       PC_PRIM_TRIANGLE_FAN = 5, PC_PRIM_SPRITE = 6 };

#endif
