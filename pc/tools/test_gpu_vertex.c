/* Equivalencia de la conversion de vertices 3D: ruta CPU (pcSubmitStrip3D, gpu_vertex=0) frente a la misma conversion hecha como
 * la hace el vertex shader (gpu_vertex=1, screen_space 3 en pc_renderer.c). No puede ejecutar la GPU: transcribe el GLSL a C con
 * las mismas operaciones y, ademas, simula las libertades que una GPU puede tomarse (multiplicar-sumar fusionado, division como
 * multiplicacion por el reciproco) para medir si cambian lo que acaba en pantalla. gcc -O1 test_gpu_vertex.c -lm -o t && ./t */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef struct { float x, y, z, r, g, b, a, u, v, q, fog; } V;

static float ftoi0sat(float x) { return floorf(fminf(fmaxf(x, 0.0f), 255.0f)); }
static float clampf(float x, float lo, float hi) { return fminf(fmaxf(x, lo), hi); }

/* ---- ruta CPU, tal como esta en ps2_dummy.c ---- */
static void cpu_convert(const float* rec, float za, float zb, float tw, float th, V* o, float* gs_z)
{
    float q = rec[2], sz, zc;
    if (q > -1e-9f && q < 1e-9f) q = 1e-9f;
    sz = -za + q * -zb;
    zc = sz < 0.0f ? 0.0f : sz > 65534.0f ? 65534.0f : sz;
    zc = floorf(zc);
    *gs_z = zc;
    o->x = floorf(rec[8] * 16.0f) / 16.0f - 1728.0f;
    o->y = floorf(rec[9] * 16.0f) / 16.0f - 1808.0f;
    { float d = 1.0f - zc / 65535.0f; o->z = d < 0.0f ? 0.0f : d > 1.0f ? 1.0f : d; }
    o->q = q;
    o->u = rec[0] / q * tw;
    o->v = rec[1] / q * th;
    o->r = ftoi0sat(rec[4]) / 255.0f; o->g = ftoi0sat(rec[5]) / 255.0f; o->b = ftoi0sat(rec[6]) / 255.0f;
    o->a = fminf(ftoi0sat(rec[7]) / 128.0f, 1.0f);
    o->fog = ftoi0sat(rec[11]) / 255.0f;
}

/* ---- vertex shader (modo 3). variant 0: IEEE estricto; 1: FMA + reciproco (lo que puede hacer una GPU) ---- */
static void gpu_convert(const float* rec, float za, float zb, float tw, float th, int variant, V* o)
{
    float q = rec[2], sz, zc, depth, inv255 = 1.0f / 255.0f, inv128 = 1.0f / 128.0f, inv65535 = 1.0f / 65535.0f;
    if (q > -1e-9f && q < 1e-9f) q = 1e-9f;
    sz = variant ? fmaf(q, -zb, -za) : -za + q * -zb;
    zc = floorf(clampf(sz, 0.0f, 65534.0f));
    depth = clampf(variant ? fmaf(-zc, inv65535, 1.0f) : 1.0f - zc / 65535.0f, 0.0f, 1.0f);
    o->x = floorf(rec[8] * 16.0f) / 16.0f - 1728.0f;
    o->y = floorf(rec[9] * 16.0f) / 16.0f - 1808.0f;
    o->z = depth;
    o->q = q;
    if (variant) { float rq = 1.0f / q; o->u = rec[0] * rq * tw; o->v = rec[1] * rq * th; }
    else         { o->u = rec[0] / q * tw; o->v = rec[1] / q * th; }
    if (variant) {
        o->r = floorf(clampf(rec[4], 0, 255)) * inv255; o->g = floorf(clampf(rec[5], 0, 255)) * inv255;
        o->b = floorf(clampf(rec[6], 0, 255)) * inv255; o->a = fminf(floorf(clampf(rec[7], 0, 255)) * inv128, 1.0f);
        o->fog = floorf(clampf(rec[11], 0, 255)) * inv255;
    } else {
        o->r = floorf(clampf(rec[4], 0, 255)) / 255.0f; o->g = floorf(clampf(rec[5], 0, 255)) / 255.0f;
        o->b = floorf(clampf(rec[6], 0, 255)) / 255.0f; o->a = fminf(floorf(clampf(rec[7], 0, 255)) / 128.0f, 1.0f);
        o->fog = floorf(clampf(rec[11], 0, 255)) / 255.0f;
    }
}

static int bits_eq(float a, float b) { return memcmp(&a, &b, 4) == 0 || (a == b); }   /* +0 == -0 */
static int ulps(float a, float b) { int32_t x, y; memcpy(&x, &a, 4); memcpy(&y, &b, 4); return abs(x - y); }
static float rnd(float lo, float hi) { return lo + (hi - lo) * ((float)rand() / (float)RAND_MAX); }

int main(void)
{
    static const float cams[][2] = { { 10.0f, 20000.0f }, { 2.0f, 20000.0f }, { 10.0f, 1000.0f }, { 1.0f, 65535.0f }, { 100.0f, 30000.0f } };
    const int N = 2000000;
    int c, i, diff_strict = 0, zdiff = 0, zmax_lsb = 0, cdiff = 0, fdiff = 0, maxulp_uv = 0, qdiff = 0;
    long total = 0;

    srand(7);
    for (c = 0; c < 5; c++) {
        const float n = cams[c][0], f = cams[c][1];
        const float za = (-65536.0f * n) / (f - n), zb = (65536.0f * n * f) / (f - n);
        for (i = 0; i < N; i++) {
            float rec[12], tw = (float)(1 << (rand() % 5 + 4)), th = (float)(1 << (rand() % 5 + 4));
            V a, b, d;
            float zc_cpu, zc_gpu;
            int k;
            rec[0] = rnd(-4.0f, 4.0f); rec[1] = rnd(-4.0f, 4.0f);
            rec[2] = (rand() % 50 == 0) ? rnd(-1e-8f, 1e-8f) : 1.0f / rnd(n, f);     /* Q = 1/z */
            rec[3] = 0.0f;
            for (k = 4; k < 8; k++) rec[k] = (rand() % 6 == 0) ? rnd(-20.0f, 300.0f) : rnd(0.0f, 255.0f);
            rec[8] = rnd(1500.0f, 2600.0f); rec[9] = rnd(1500.0f, 2600.0f); rec[10] = rnd(0.0f, 65535.0f);
            rec[11] = (rand() % 6 == 0) ? rnd(-20.0f, 300.0f) : rnd(0.0f, 255.0f);
            cpu_convert(rec, za, zb, tw, th, &a, &zc_cpu);
            gpu_convert(rec, za, zb, tw, th, 0, &b);
            gpu_convert(rec, za, zb, tw, th, 1, &d);
            total++;
            if (!bits_eq(a.x, b.x) || !bits_eq(a.y, b.y) || !bits_eq(a.z, b.z) || !bits_eq(a.r, b.r) || !bits_eq(a.g, b.g) ||
                !bits_eq(a.b, b.b) || !bits_eq(a.a, b.a) || !bits_eq(a.u, b.u) || !bits_eq(a.v, b.v) || !bits_eq(a.q, b.q) || !bits_eq(a.fog, b.fog))
                diff_strict++;
            /* variante de GPU: Z entera de 16 bits que veria el rasterizador (gl_FragDepth = floor((1-depth)*65535+0.01)) */
            zc_gpu = floorf((1.0f - d.z) * 65535.0f + 0.01f);
            zc_cpu = floorf((1.0f - a.z) * 65535.0f + 0.01f);
            if (zc_gpu != zc_cpu) { zdiff++; if ((int)fabsf(zc_gpu - zc_cpu) > zmax_lsb) zmax_lsb = (int)fabsf(zc_gpu - zc_cpu); }
            /* color/niebla tal como los cuantiza el fragment shader: floor(x*255+0.001) */
            if (floorf(a.r * 255.0f + 0.001f) != floorf(d.r * 255.0f + 0.001f) || floorf(a.g * 255.0f + 0.001f) != floorf(d.g * 255.0f + 0.001f) ||
                floorf(a.b * 255.0f + 0.001f) != floorf(d.b * 255.0f + 0.001f) || floorf(a.a * 128.0f + 0.001f) != floorf(d.a * 128.0f + 0.001f)) cdiff++;
            if (floorf(a.fog * 255.0f + 0.001f) != floorf(d.fog * 255.0f + 0.001f)) fdiff++;
            { int u1 = ulps(a.u, d.u), u2 = ulps(a.v, d.v); if (u1 > maxulp_uv) maxulp_uv = u1; if (u2 > maxulp_uv) maxulp_uv = u2; }
            if (a.x != d.x || a.y != d.y || a.q != d.q) qdiff++;
        }
    }
    printf("vertices probados: %ld\n", total);
    printf("GLSL transcrito (IEEE) vs ruta CPU: %d diferencias (esperado 0)\n", diff_strict);
    printf("variante GPU (FMA + reciproco) vs CPU, tras la cuantizacion del fragment shader:\n");
    printf("  Z de 16 bits distinta : %d (%.5f%%), maxima diferencia %d LSB\n", zdiff, 100.0 * zdiff / total, zmax_lsb);
    printf("  color distinto        : %d\n  niebla distinta       : %d\n", cdiff, fdiff);
    printf("  x,y,q distintos       : %d\n  U,V: maxima diferencia %d ulp\n", qdiff, maxulp_uv);
    return diff_strict != 0;
}
