/* PC port: aritmetica de la VU/EE en C.
 *
 * Ya no hay estado de VU0 (registros, flags, Q): lo que hacia el codigo COP2 traducido vive en pc_calcpoint.c, pc_clip.c,
 * pc_scissor.c y pc_tri_scissor.c. Aqui quedan el modo de redondeo (pcVuRoundEnter/Leave) y los ayudantes de coma flotante
 * con las reglas de la VU: sin infinitos ni NaN (los resultados saturan a +-FLT_MAX) y denormales a cero.
 */
#pragma once

#include <float.h>
#include <math.h>
#include <string.h>

typedef union {
    float f[4];
    unsigned int u[4];
    int i[4];
} VuVec;



/* ---- modo de redondeo de la VU/EE ----
 * La FPU del EE y las FMAC de las VU truncan hacia cero (no hay redondeo al mas cercano) y vacian los
 * denormales. En x86 eso es MXCSR con RC=11, FTZ y DAZ. pcVuRoundEnter/Leave lo activan solo mientras corre
 * matematica de VU (las funciones vu1Draw*), de modo que el driver de GL, SDL y el resto del proceso siguen con
 * el redondeo normal. Activo por defecto; vu_trunc=0 en recvx.ini (o RECVX_VU_TRUNC=0) vuelve al redondeo del host para comparar. */
#if defined(_M_IX86) || defined(_M_X64) || defined(__SSE__)
#include <xmmintrin.h>
int pcVuTruncEnabled(void);
static __inline unsigned int pcVuRoundEnter(void)
{
    if (pcVuTruncEnabled())
    {
        unsigned int old = _mm_getcsr();
        _mm_setcsr(old | 0x6000u | 0x8000u | 0x0040u);
        return old;
    }
    return 0xFFFFFFFFu;
}
static __inline void pcVuRoundLeave(unsigned int old)
{
    if (old != 0xFFFFFFFFu) _mm_setcsr(old);
}
#else
static __inline unsigned int pcVuRoundEnter(void) { return 0xFFFFFFFFu; }
static __inline void pcVuRoundLeave(unsigned int old) { (void)old; }
#endif

/* ---- float helpers ---- */

static __inline float vu_f(float x)
{
    union { float f; unsigned int u; } v;

    v.f = x;

    if ((v.u & 0x7F800000) == 0x7F800000)
    {
        v.u = (v.u & 0x80000000) | 0x7F7FFFFF;   /* inf/nan -> max */
    }
    else if ((v.u & 0x7F800000) == 0)
    {
        v.u &= 0x80000000;                       /* denormal -> 0 */
    }

    return v.f;
}

/* ---- flags ---- */

static __inline float vu_div(float a, float b)
{
    if (b == 0.0f)
    {
        union { float f; unsigned int u; } r, sa, sb;

        sa.f = a;
        sb.f = b;
        r.u = ((sa.u ^ sb.u) & 0x80000000) | 0x7F7FFFFF;
        return r.f;
    }

    return vu_f(a / b);
}

static __inline float vu_rsqrt(float a, float b)
{
    float s = sqrtf(fabsf(b));

    return vu_div(a, s);
}

/* ---- conversions ---- */

static __inline int vu_ftoi(float f, float scale)
{
    double d = (double)f * scale;

    if (d >= 2147483647.0)  return 0x7FFFFFFF;
    if (d <= -2147483648.0) return (int)0x80000000;

    return (int)d;   /* truncation toward zero */
}

/* integer registers are 16 bits */
static __inline int vu_vi(int x)
{
    return (short)(x & 0xFFFF);
}
