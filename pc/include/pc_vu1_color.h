/*
 * Calculo de color por vertice de vu1Draw* (ps2_Vu1Strip.c), escrito como C escalar directo.
 *
 * Reproduce, operacion por operacion y en el mismo orden, lo que hacen las 11 funciones vu1GetVertexColor*
 * (macro-codigo VU0 transpilado): vminiw / vmaxx / vadd / vsubw / vmul sobre xyz y alpha = 1.0 * AlphaRatio en w.
 * Cada resultado intermedio pasa por vu_f() (inf/NaN -> max, denormal -> 0), igual que la unidad vectorial.
 * No toca pcVu0 (ni registros vf, ni banderas MAC): es una funcion pura de sus argumentos.
 * El redondeo lo fija quien llama con pcVuRoundEnter(), igual que antes.
 *
 * Entradas: c = color del vertice (fIr,fIg,fIb,fA); amb/dif/spe = vu1Ambient/vu1Diffuse/vu1Specula (rgb);
 * ratio = fVu1AlphaRatio. Salida: out = (r,g,b,a) para VU1_PRIM_BUF.fR..fA.
 * Equivalencia comprobada bit a bit contra las funciones transpiladas (4.4M comparaciones; arnes en milestone/0.9.160:
 * pc/tools/test_vu1_color.py).
 */
#ifndef PC_VU1_COLOR_H
#define PC_VU1_COLOR_H

#include <math.h>
#include "pc_vu.h"

#define PCVC_ZERO 0.0f
#define PCVC_ONE 1.0f

/* vmaxx con 0: en la VU el cero con signo se ordena como -0 < +0, asi que max(-0, 0) = +0. fmaxf() de C deja ese caso sin
 * definir (cada libm/compilador elige), por eso se fija aqui: el resultado no depende del compilador ni de la API. */
static __inline float pcvc_max0(float x)
{
    float r = vu_f(fmaxf(x, PCVC_ZERO));
    if (r == 0.0f) r = 0.0f;
    return r;
}
static __inline float pcvc_min1(float x) { return vu_f(fminf(x, PCVC_ONE)); }
static __inline float pcvc_alpha(float ratio) { return vu_f(PCVC_ONE * ratio); }

/* 0: vu1GetVertexColor -> c * (128,128,128,256) */
static __inline void pcvc_0(const float* c, float* out)
{
    out[0] = vu_f(c[0] * 128.0f);
    out[1] = vu_f(c[1] * 128.0f);
    out[2] = vu_f(c[2] * 128.0f);
    out[3] = vu_f(c[3] * 256.0f);
}

/* 1: CM -> diffuse tal cual (rgb y su w), alpha = AlphaRatio */
static __inline void pcvc_1(const float* dif4, float ratio, float* out)
{
    out[0] = dif4[0]; out[1] = dif4[1]; out[2] = dif4[2];
    out[3] = pcvc_alpha(ratio);
}

/* 2: Ignore -> rgb = 0 + 128 */
static __inline void pcvc_2(float ratio, float* out)
{
    float k = vu_f(PCVC_ZERO + 128.0f);
    out[0] = k; out[1] = k; out[2] = k;
    out[3] = pcvc_alpha(ratio);
}

/* 3: Dif -> min(c,1), max(.,0), * diffuse */
static __inline void pcvc_3(const float* c, const float* dif, float ratio, float* out)
{
    int k;
    for (k = 0; k < 3; k++) {
        float t = pcvc_min1(c[k]);
        t = pcvc_max0(t);
        out[k] = vu_f(t * dif[k]);
    }
    out[3] = pcvc_alpha(ratio);
}

/* 4: DifAmb -> clamp01(c + amb) * diffuse */
static __inline void pcvc_4(const float* c, const float* amb, const float* dif, float ratio, float* out)
{
    int k;
    for (k = 0; k < 3; k++) {
        float t = vu_f(c[k] + amb[k]);
        t = pcvc_max0(t);
        t = pcvc_min1(t);
        out[k] = vu_f(t * vu_f(dif[k]));
    }
    out[3] = pcvc_alpha(ratio);
}

/* 5: DifSpe1 -> clamp01(c)*c + clamp01(c-1)*c */
static __inline void pcvc_5(const float* c, float ratio, float* out)
{
    int k;
    for (k = 0; k < 3; k++) {
        float s = vu_f(c[k] - PCVC_ONE);
        float d = pcvc_max0(c[k]);
        d = pcvc_min1(d);
        s = pcvc_max0(s);
        s = pcvc_min1(s);
        d = vu_f(d * c[k]);
        s = vu_f(s * c[k]);
        out[k] = vu_f(d + s);
    }
    out[3] = pcvc_alpha(ratio);
}

/* 6: DifSpe2 -> clamp01(c)*dif + clamp01(c+amb-1)*spe */
static __inline void pcvc_6(const float* c, const float* amb, const float* dif, const float* spe, float ratio, float* out)
{
    int k;
    for (k = 0; k < 3; k++) {
        float a = vu_f(c[k] + amb[k]);
        float s;
        float d;
        a = vu_f(a - PCVC_ONE);
        d = pcvc_max0(c[k]);
        d = pcvc_min1(d);
        a = pcvc_max0(a);
        a = pcvc_min1(a);
        d = vu_f(d * dif[k]);
        s = vu_f(a * spe[k]);
        out[k] = vu_f(d + s);
    }
    out[3] = pcvc_alpha(ratio);
}

/* 7/10: potencia 17 -> (1 * c) multiplicado 17 veces por c (cada paso redondeado como en la unidad vectorial) */
static __inline float pcvc_pow17(float c)
{
    float p = vu_f(PCVC_ZERO + PCVC_ONE);
    int n;
    for (n = 0; n < 17; n++) p = vu_f(p * c);
    return p;
}

/* 7: DifSpe3 -> clamp01(c)*dif + clamp01(c^17)*spe */
static __inline void pcvc_7(const float* c, const float* dif, const float* spe, float ratio, float* out)
{
    int k;
    for (k = 0; k < 3; k++) {
        float p = pcvc_pow17(c[k]);
        float d = pcvc_max0(c[k]);
        d = pcvc_min1(d);
        p = pcvc_max0(p);
        p = pcvc_min1(p);
        d = vu_f(d * dif[k]);
        p = vu_f(p * spe[k]);
        out[k] = vu_f(d + p);
    }
    out[3] = pcvc_alpha(ratio);
}

/* 8: DifSpe1Amb -> clamp01(c-1)*spe + clamp01(c+amb)*dif */
static __inline void pcvc_8(const float* c, const float* amb, const float* dif, const float* spe, float ratio, float* out)
{
    int k;
    for (k = 0; k < 3; k++) {
        float s = vu_f(c[k] - PCVC_ONE);
        float d;
        s = pcvc_max0(s);
        s = pcvc_min1(s);
        d = vu_f(c[k] + amb[k]);
        d = pcvc_max0(d);
        d = pcvc_min1(d);
        s = vu_f(s * spe[k]);
        d = vu_f(d * dif[k]);
        out[k] = vu_f(d + s);
    }
    out[3] = pcvc_alpha(ratio);
}

/* 9: DifSpe2Amb -> d = c+amb; s = clamp01(d-1)*spe; d = clamp01(d)*dif */
static __inline void pcvc_9(const float* c, const float* amb, const float* dif, const float* spe, float ratio, float* out)
{
    int k;
    for (k = 0; k < 3; k++) {
        float d = vu_f(c[k] + amb[k]);
        float s = vu_f(d - PCVC_ONE);
        s = pcvc_max0(s);
        s = pcvc_min1(s);
        d = pcvc_max0(d);
        d = pcvc_min1(d);
        s = vu_f(s * spe[k]);
        d = vu_f(d * dif[k]);
        out[k] = vu_f(d + s);
    }
    out[3] = pcvc_alpha(ratio);
}

/* 10: DifSpe3Amb -> d = c+amb; s = d^17; clamp01 ambos; d*dif + s*spe */
static __inline void pcvc_10(const float* c, const float* amb, const float* dif, const float* spe, float ratio, float* out)
{
    int k;
    for (k = 0; k < 3; k++) {
        float d = vu_f(c[k] + amb[k]);
        float s = pcvc_pow17(d);
        d = pcvc_max0(d);
        d = pcvc_min1(d);
        s = pcvc_max0(s);
        s = pcvc_min1(s);
        d = vu_f(d * dif[k]);
        s = vu_f(s * spe[k]);
        out[k] = vu_f(d + s);
    }
    out[3] = pcvc_alpha(ratio);
}

#endif
