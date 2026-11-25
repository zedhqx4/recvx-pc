/* Correcciones de UV de las piezas 2D de la interfaz (menus, inventario, terminales...).
 *
 * Flujo: F8 (rafaga) en una pantalla -> recvx_uiuv.tsv con TODAS las piezas 2D texturizadas que se dibujan;
 * pc/tools/uiuv_dump_to_obj.py lo convierte en un OBJ (una pieza = un objeto) para editar sus UV en Blender;
 * pc/tools/uiuv_obj_to_table.py convierte el OBJ editado en ui_uv_table.txt; el juego lo carga al arrancar y sustituye las UV de las
 * piezas listadas. Solo tipos C simples: se incluye desde archivos del juego (donde `long` esta redefinido). */
#ifndef PC_UVTABLE_H
#define PC_UVTABLE_H

#define PC_UIPIECE_MAX 64

/* Una pieza 2D tal como llega a la ruta nativa, antes de cualquier ajuste de UV o de pantalla ancha.
 * x16/y16: posicion de pantalla del juego * 16 (relativa a 1728/1808). u16/v16: UV en texels * 16. */
typedef struct PcUiPiece {
    unsigned int gindex;      /* Gindex de la textura activa */
    int tex_w, tex_h;
    int topology;             /* PC_PRIM_POINT..PC_PRIM_SPRITE */
    int fst;                  /* 1: UV enteras (12.4); 0: ST en coma flotante */
    int n;                    /* vertices de origen (sprite: 2) */
    int x16[PC_UIPIECE_MAX], y16[PC_UIPIECE_MAX], u16[PC_UIPIECE_MAX], v16[PC_UIPIECE_MAX];
} PcUiPiece;

/* 1 si hay que construir la pieza: tabla cargada con entradas o rafaga F8 en curso */
int pcUiUvWanted(void);

/* 1 si la tabla tiene entrada para esta pieza; uv = 2*n floats (u0,v0,u1,v1...) en texels, UV FINALES de cada vertice de origen */
int pcUiUvTableLookup(const PcUiPiece* piece, float* uv);

/* Durante la rafaga F8: anota la pieza (clave + UV que el juego usaria sin tabla) en recvx_uiuv.tsv, una vez por pieza distinta */
void pcUiUvCapture(const PcUiPiece* piece, const float* default_uv);

#endif
