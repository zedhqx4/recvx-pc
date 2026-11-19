/* Comprueba que pc/include/pc_vu1_color.h (C escalar) da exactamente la misma salida que las 11 funciones
 * vu1GetVertexColor* transpiladas de ps2_Vu1Strip.c, bit a bit, con entradas aleatorias y casos extremos.
 *
 * Uso (desde la raiz del repo):
 *   python3 pc/tools/test_vu1_color.py   # extrae los cuerpos transpilados a /tmp y compila y ejecuta este test
 * o a mano: gcc -O1 -no-pie -DTARGET_PC -Ipc/include -include <cuerpos.h> pc/tools/test_vu1_color.c -lm
 * (-no-pie: las funciones transpiladas truncan punteros a 32 bits, los buffers estaticos deben estar por debajo de 4 GB)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <math.h>
#include "pc_asm2c.h"
#include "pc_vu.h"
#include "pc_vu1_color.h"

typedef struct { float fU, fV, fPad0, fPad1, fIr, fIg, fIb, fA, fVx, fVy, fVz, fFog, fSx, fSy, fIz, fNz; } VU1_STRIP_BUF;
typedef struct { float fS, fT, fQ; unsigned int ulKick; float fR, fG, fB, fA, fX, fY, fZ, fF; } VU1_PRIM_BUF;
typedef struct { float fR, fG, fB, fA; } VU1_COLOR;

PcVu0 pcVu0 = { { { { 0, 0, 0, 1 } } } };
VU1_COLOR vu1Diffuse, vu1Specula, vu1Ambient;
float fVu1AlphaRatio = 128.0f;

#include "vu1_color_bodies.h" /* generado: las 11 funciones transpiladas, renombradas ref_vu1GetVertexColor* */

typedef void (*RefFn)(VU1_STRIP_BUF*, VU1_PRIM_BUF*);
static RefFn ref_tbl[11] = {
    ref_vu1GetVertexColor, ref_vu1GetVertexColorCM, ref_vu1GetVertexColorIgnore, ref_vu1GetVertexColorDif,
    ref_vu1GetVertexColorDifAmb, ref_vu1GetVertexColorDifSpe1, ref_vu1GetVertexColorDifSpe2, ref_vu1GetVertexColorDifSpe3,
    ref_vu1GetVertexColorDifSpe1Amb, ref_vu1GetVertexColorDifSpe2Amb, ref_vu1GetVertexColorDifSpe3Amb
};

static VU1_STRIP_BUF sb;
static VU1_PRIM_BUF pb;

static void native(int m, const float* c, float* out)
{
    const float amb[4] = { vu1Ambient.fR, vu1Ambient.fG, vu1Ambient.fB, vu1Ambient.fA };
    const float dif[4] = { vu1Diffuse.fR, vu1Diffuse.fG, vu1Diffuse.fB, vu1Diffuse.fA };
    const float spe[4] = { vu1Specula.fR, vu1Specula.fG, vu1Specula.fB, vu1Specula.fA };
    switch (m) {
    case 0: pcvc_0(c, out); break;
    case 1: pcvc_1(dif, fVu1AlphaRatio, out); break;
    case 2: pcvc_2(fVu1AlphaRatio, out); break;
    case 3: pcvc_3(c, dif, fVu1AlphaRatio, out); break;
    case 4: pcvc_4(c, amb, dif, fVu1AlphaRatio, out); break;
    case 5: pcvc_5(c, fVu1AlphaRatio, out); break;
    case 6: pcvc_6(c, amb, dif, spe, fVu1AlphaRatio, out); break;
    case 7: pcvc_7(c, dif, spe, fVu1AlphaRatio, out); break;
    case 8: pcvc_8(c, amb, dif, spe, fVu1AlphaRatio, out); break;
    case 9: pcvc_9(c, amb, dif, spe, fVu1AlphaRatio, out); break;
    default: pcvc_10(c, amb, dif, spe, fVu1AlphaRatio, out); break;
    }
}

static unsigned int rng = 12345u;
static unsigned int rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }
static float rf(float lo, float hi) { return lo + (hi - lo) * ((float)(rnd() & 0xFFFF) / 65535.0f); }
static const unsigned int special[] = { 0x00000000, 0x80000000, 0x3F800000, 0xBF800000, 0x00000001, 0x007FFFFF, 0x7F800000, 0xFF800000, 0x7FC00000, 0x7F7FFFFF, 0xFF7FFFFF, 0x43000000, 0x437F0000 };
static float pick(void)
{
    unsigned int r = rnd() % 10;
    if (r == 0) { unsigned int u = special[rnd() % (sizeof(special) / sizeof(special[0]))]; float f; memcpy(&f, &u, 4); return f; }
    if (r < 3) return rf(-2.0f, 3.0f);
    if (r < 4) return rf(0.0f, 300.0f);
    return rf(0.0f, 1.0f);
}

int main(void)
{
    long bad = 0, n = 0, signo = 0;
    int m, i;
    for (i = 0; i < 400000; i++) {
        float c[4], out[4];
        c[0] = pick(); c[1] = pick(); c[2] = pick(); c[3] = pick();
        vu1Diffuse.fR = pick(); vu1Diffuse.fG = pick(); vu1Diffuse.fB = pick(); vu1Diffuse.fA = pick();
        vu1Specula.fR = pick(); vu1Specula.fG = pick(); vu1Specula.fB = pick(); vu1Specula.fA = pick();
        vu1Ambient.fR = pick(); vu1Ambient.fG = pick(); vu1Ambient.fB = pick(); vu1Ambient.fA = pick();
        fVu1AlphaRatio = (rnd() % 4 == 0) ? pick() : 128.0f;
        /* los registros que el juego fija con vu1Set*Material (vf20 = diffuse*128 sin w extra, vf22 = ambient) */
        memcpy(&pcVu0.vf[20], &vu1Diffuse, 16);
        pcVu0.vf[20].f[0] = vu_f(pcVu0.vf[20].f[0]); pcVu0.vf[20].f[1] = vu_f(pcVu0.vf[20].f[1]); pcVu0.vf[20].f[2] = vu_f(pcVu0.vf[20].f[2]);
        memcpy(&pcVu0.vf[22], &vu1Ambient, 16);
        sb.fIr = c[0]; sb.fIg = c[1]; sb.fIb = c[2]; sb.fA = c[3];
        for (m = 0; m < 11; m++) {
            memset(&pb, 0, sizeof(pb));
            ref_tbl[m](&sb, &pb);
            native(m, c, out);
            n++;
            if (memcmp(out, &pb.fR, 16) != 0) {
                /* diferencia solo en el signo de un cero (-0 / +0): la ruta nueva fija max(-0,0)=+0 como la VU; el
                 * cero con signo no cambia el entero que sale despues (FTOI/GS). Se cuenta aparte. */
                int k, only_sign = 1;
                for (k = 0; k < 4; k++) {
                    float a = out[k], b = (&pb.fR)[k];
                    if (memcmp(&a, &b, 4) != 0 && !(a == 0.0f && b == 0.0f)) only_sign = 0;
                }
                if (only_sign) { signo++; continue; }
                /* NaN con payload distinto o -0 contra +0 cuentan como diferencia: comparacion exacta de bits */
                if (bad < 12) printf("DIFF modo %d c=(%g %g %g %g) ref=(%08x %08x %08x %08x) nat=(%08x %08x %08x %08x)\n", m,
                    c[0], c[1], c[2], c[3], *(unsigned*)&pb.fR, *(unsigned*)&pb.fG, *(unsigned*)&pb.fB, *(unsigned*)&pb.fA,
                    *(unsigned*)&out[0], *(unsigned*)&out[1], *(unsigned*)&out[2], *(unsigned*)&out[3]);
                bad++;
            }
        }
    }
    printf("%ld comparaciones, %ld diferencias reales, %ld solo de signo de cero\n", n, bad, signo);
    return bad != 0;
}
