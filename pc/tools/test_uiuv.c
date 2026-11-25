/* Arnes de prueba de pc/src/pc_uvtable.c. Modos:  dump  (escribe recvx_uiuv.tsv con piezas sinteticas) | lookup (carga ui_uv_table.txt y consulta) */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "pc_uvtable.h"

static int burst = 1;
int pcRendererBurstDiag(void) { return burst; }
int pcIniGetInt(const char* k, int d) { (void)k; return d; }
void pcLog(const char* f, ...) { (void)f; }
uint64_t pcUiPieceHash(const PcUiPiece* p);

static PcUiPiece P[6];
static float DEF[6][2 * PC_UIPIECE_MAX];

static void add(int i, int topo, int fst, int n, const int* xyuv)
{
    int k;
    P[i].gindex = 2222001u + (unsigned)(i % 2); P[i].tex_w = 512; P[i].tex_h = 256;
    P[i].topology = topo; P[i].fst = fst; P[i].n = n;
    for (k = 0; k < n; k++) {
        P[i].x16[k] = xyuv[4 * k]; P[i].y16[k] = xyuv[4 * k + 1]; P[i].u16[k] = xyuv[4 * k + 2]; P[i].v16[k] = xyuv[4 * k + 3];
        DEF[i][2 * k] = (float)P[i].u16[k] / 16.0f - 0.5f; DEF[i][2 * k + 1] = (float)P[i].v16[k] / 16.0f - 0.5f;
    }
}

static void build(void)
{
    static const int quad[] = { 640, 832, 72, 200, 1664, 832, 1096, 200, 640, 1344, 72, 712, 1664, 1344, 1096, 712 };
    static const int spr[] = { -160, 32, 8, 8, 480, 288, 1032, 520 };
    static const int stq[] = { 0, 0, 8, 8, 1024, 0, 8200, 8, 0, 512, 8, 4104, 1024, 512, 8200, 4104 };
    static const int tri[] = { 0, 0, 8, 8, 160, 0, 168, 8, 0, 160, 8, 168, 160, 0, 168, 8, 160, 160, 168, 168, 0, 160, 8, 168 };
    static const int fan[] = { 80, 80, 40, 40, 160, 80, 120, 40, 160, 160, 120, 120, 80, 160, 40, 120, 0, 80, 0, 40 };
    static const int strip[] = { 0, 0, 8, 8, 0, 160, 8, 168, 160, 0, 168, 8, 160, 160, 168, 168, 320, 0, 328, 8, 320, 160, 328, 168 };
    add(0, 4, 1, 4, quad); add(1, 6, 1, 2, spr); add(2, 4, 0, 4, stq); add(3, 3, 1, 6, tri); add(4, 5, 1, 5, fan); add(5, 4, 1, 6, strip);
}

int main(int argc, char** argv)
{
    int i, j;
    build();
    if (argc > 1 && !strcmp(argv[1], "dump")) {
        for (i = 0; i < 6; i++) { pcUiUvCapture(&P[i], DEF[i]); pcUiUvCapture(&P[i], DEF[i]); }   /* la segunda no debe duplicar */
        for (i = 0; i < 6; i++) printf("hash %d %016llx\n", i, (unsigned long long)pcUiPieceHash(&P[i]));
        return 0;
    }
    burst = 0;
    printf("wanted %d\n", pcUiUvWanted());
    for (i = 0; i < 6; i++) {
        float uv[2 * PC_UIPIECE_MAX];
        int hit = pcUiUvTableLookup(&P[i], uv);
        printf("lookup %d %d", i, hit);
        if (hit) for (j = 0; j < 2 * P[i].n; j++) printf(" %.4f", uv[j]);
        printf("\n");
    }
    return 0;
}
