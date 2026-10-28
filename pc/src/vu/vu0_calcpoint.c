/*
 * VU0 CALCPOINT SIMD kernel adapted from aikatore/recvx-enhanced-port,
 * pc/src/ps2/vu.c. Copyright (c) 2026 aikatore and contributors.
 *
 * MIT License
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>
#include <emmintrin.h>
#include "pc_vu.h"


typedef __m128 V4;
static __inline V4 v4clamp(V4 x)
{
    const V4 sg = _mm_set1_ps(-0.0f);
    V4 a = _mm_andnot_ps(sg, x);
    V4 keep = _mm_cmpnlt_ps(a, _mm_set1_ps(FLT_MIN));
    a = _mm_and_ps(_mm_min_ps(a, _mm_set1_ps(FLT_MAX)), keep);
    return _mm_or_ps(a, _mm_and_ps(sg, x));
}
static __inline V4 v4ld(const VuVec* v) { return _mm_loadu_ps(v->f); }
static __inline void v4st(VuVec* v, V4 x) { _mm_storeu_ps(v->f, x); }
static __inline V4 v4mul(V4 a, V4 b) { return v4clamp(_mm_mul_ps(a, b)); }
static __inline V4 v4add(V4 a, V4 b) { return v4clamp(_mm_add_ps(a, b)); }
static __inline V4 v4sub(V4 a, V4 b) { return v4clamp(_mm_sub_ps(a, b)); }
static __inline V4 v4max(V4 a, V4 b) { return v4clamp(_mm_max_ps(a, b)); }
static __inline V4 v4set1(float f) { return _mm_set1_ps(f); }
static __inline V4 v4zero(void) { return _mm_setzero_ps(); }
static __inline V4 v4blend(V4 a, V4 b, int bits)
{
    V4 k = _mm_castsi128_ps(_mm_set_epi32(-(bits >> 3 & 1), -(bits >> 2 & 1), -(bits >> 1 & 1), -(bits & 1)));
    return _mm_or_ps(_mm_andnot_ps(k, a), _mm_and_ps(k, b));
}
static __inline int v4neg_bits(V4 v) { return _mm_movemask_ps(v); }
static __inline int v4zero_bits(V4 v) { return _mm_movemask_ps(_mm_cmpeq_ps(v, _mm_setzero_ps())); }
static __inline int v4gt_bits(V4 a, V4 b) { return _mm_movemask_ps(_mm_cmpgt_ps(a, b)); }
static __inline int v4lt_bits(V4 a, V4 b) { return _mm_movemask_ps(_mm_cmplt_ps(a, b)); }
static __inline V4 v4fabs(V4 a) { return _mm_andnot_ps(_mm_set1_ps(-0.0f), a); }
static __inline V4 v4fneg(V4 a) { return _mm_xor_ps(_mm_set1_ps(-0.0f), a); }
static __inline V4 v4sqrt(V4 a) { return _mm_sqrt_ps(a); }
static __inline V4 v4div(V4 a, V4 b)
{
    V4 z = _mm_cmpeq_ps(b, _mm_setzero_ps());
    V4 big = _mm_or_ps(_mm_set1_ps(FLT_MAX), _mm_and_ps(_mm_xor_ps(a, b), _mm_set1_ps(-0.0f)));
    return _mm_or_ps(_mm_and_ps(z, big), _mm_andnot_ps(z, v4clamp(_mm_div_ps(a, b))));
}
static __inline void v4transpose(V4* a, V4* b, V4* c, V4* d) { _MM_TRANSPOSE4_PS(*a, *b, *c, *d); }
static __inline V4 v4madd(V4 acc, V4 a, V4 b) { return v4add(acc, v4mul(a, b)); }

static void pcVu0CalcPointSoA(PcVu0* s, const float* in, uint8_t* out, int n)
{
    V4 m1[4][4], m2[4][4], col[4][3], l24[4], l25[4], l26[4], l27[4], l12[4], l13[4];
    const V4 one = v4set1(s->vf[0].f[3]), zero = v4set1(s->vf[0].f[0]), v17x = v4set1(s->vf[17].f[0]), v17y = v4set1(s->vf[17].f[1]);
    const V4 v01z = v4set1(s->vf[1].f[2]), w7 = v4set1(s->vf[7].f[3]);
    int on = 0, point = 0;
    uint32_t clip = s->clip;
    uint16_t vi11 = s->vi[11];
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) { m1[r][c] = v4set1(s->vf[28 + r].f[c]); m2[r][c] = v4set1(s->mem[56 + r].f[c]); }
    for (int r = 0; r < 4; r++) for (int c = 0; c < 3; c++) col[r][c] = v4set1(s->mem[71 + r].f[c]);
    for (int k = 0; k < 4; k++) {
        const uint16_t vi14 = (uint16_t)(s->vi[10] << k);   /* the light loop's vi14 */
        l24[k] = v4set1(s->vf[24].f[k]); l25[k] = v4set1(s->vf[25].f[k]); l26[k] = v4set1(s->vf[26].f[k]);
        l27[k] = v4set1(s->vf[27].f[k]); l12[k] = v4set1(s->vf[12].f[k]); l13[k] = v4set1(s->vf[13].f[k]);
        if (vi14 & s->vi[7]) { on |= 1 << k; if (vi14 & s->vi[8]) point |= 1 << k; }
    }
    for (; n > 0; n -= 4, in += 32, out += 256) {
        const int valid = n < 4 ? n : 4;   /* a last, partial group: the missing lanes repeat its last vertex */
        float pad[32];
        uint8_t tmp[256];
        const float* src = in;
        uint8_t* dst = out;
        if (valid < 4) {
            for (int v = 0; v < 4; v++) memcpy(pad + 8 * v, in + 8 * (v < valid ? v : valid - 1), 32);
            src = pad; dst = tmp;
        }
        V4 px = v4ld((const VuVec*)src), py = v4ld((const VuVec*)(src + 8)), pz = v4ld((const VuVec*)(src + 16)), pw = v4ld((const VuVec*)(src + 24));
        V4 nx = v4ld((const VuVec*)(src + 4)), ny = v4ld((const VuVec*)(src + 12)), nz = v4ld((const VuVec*)(src + 20)), nw = v4ld((const VuVec*)(src + 28));
        V4 v18[4], v19[3], v5[4], v7[4], v6x, v6y, v6z, v6w, q, r8[4];
        v4transpose(&px, &py, &pz, &pw);
        v4transpose(&nx, &ny, &nz, &nw);
        for (int c = 0; c < 4; c++)   /* MULAx/MADDAy/MADDAz/MADDw vf18, vf28-31 */
            v18[c] = v4madd(v4madd(v4madd(v4mul(m1[0][c], px), m1[1][c], py), m1[2][c], pz), m1[3][c], one);
        for (int c = 0; c < 3; c++)
            v19[c] = v4madd(v4madd(v4mul(m1[0][c], nx), m1[1][c], ny), m1[2][c], nz);
        q = v4div(one, v18[2]);                                    /* DIV Q, vf00w, vf18z */
        v6x = v4mul(v4mul(v18[0], v17x), q);                       /* MUL.xy vf06, then MULq.xyz */
        v6y = v4mul(v4mul(v18[1], v17y), q);
        v6z = v4mul(v01z, q);                                      /* (MOVE.z vf06, vf01) */
        v6w = v4mul(one, v19[2]);                                  /* MULz.w vf06, vf00, vf19z */
        r8[0] = r8[1] = r8[2] = r8[3] = v4zero();
        if (point) {   /* the whole lead-in of the light loop: vf09-11, vf05, vf04 and the FMANDs */
            V4 d2[4], dot[4];
            int m4z = 0, m4n = 0, m6z = 0, m6n = 0, m7z = 0, m7n = 0;
            for (int k = 0; k < 4; k++) {
                const V4 d9 = v4sub(l24[k], v18[0]), d10 = v4sub(l25[k], v18[1]), d11 = v4sub(l26[k], v18[2]);
                d2[k] = v4madd(v4madd(v4mul(d9, d9), d10, d10), d11, d11);
                dot[k] = v4madd(v4madd(v4mul(d9, v19[0]), d10, v19[1]), d11, v19[2]);
                const V4 s6 = v4sub(l12[k], d2[k]), s7 = v4sub(l13[k], d2[k]);
                /* MAC bit (3 - k) zero, (7 - k) sign of light k; lane v of each mask: vertex v */
                m4z |= v4zero_bits(dot[k]) << 4 * k; m4n |= v4neg_bits(dot[k]) << 4 * k;
                m6z |= v4zero_bits(s6) << 4 * k;     m6n |= v4neg_bits(s6) << 4 * k;
                m7z |= v4zero_bits(s7) << 4 * k;     m7n |= v4neg_bits(s7) << 4 * k;
            }
            uint16_t vi13[4], vi12[4];
            for (int v = 0; v < 4; v++) {
                uint32_t a = 0, b = 0, c = 0;
                for (int k = 0; k < 4; k++) {
                    a |= (m4z >> (4 * k + v) & 1u) << (3 - k) | (m4n >> (4 * k + v) & 1u) << (7 - k);
                    b |= (m6z >> (4 * k + v) & 1u) << (3 - k) | (m6n >> (4 * k + v) & 1u) << (7 - k);
                    c |= (m7z >> (4 * k + v) & 1u) << (3 - k) | (m7n >> (4 * k + v) & 1u) << (7 - k);
                }
                vi13[v] = (uint16_t)((a & s->vi[9]) | (c & s->vi[9]));
                vi12[v] = (uint16_t)(b & s->vi[9]);
            }
            for (int k = 0; k < 4; k++) {
                if (!(point >> k & 1) || !(on >> k & 1)) continue;
                int facing = 0, atten = 0;   /* lanes: vi01 = 0 (lit), vi15 != 0 (second MULq) */
                for (int v = 0; v < 4; v++) {
                    facing |= !(vi13[v] & s->vi[7 - k]) << v;
                    atten |= ((vi12[v] & s->vi[7 - k]) != 0) << v;
                }
                if (!facing) continue;
                const V4 t = v4mul(dot[k], v4div(one, v4sqrt(v4fabs(d2[k]))));   /* MULq: the RSQRT */
                r8[k] = v4blend(r8[k], v4blend(t, v4mul(t, v4div(l12[k], d2[k])), atten), facing);
            }
        }
        for (int k = 0; k < 4; k++) {   /* LIGHTk_PARA */
            if (!(on >> k & 1) || (point >> k & 1)) continue;
            const V4 lx = v4mul(v19[0], l27[k]), ly = v4mul(v19[1], l27[k]), lz = v4mul(v19[2], l27[k]);
            r8[k] = v4max(v4madd(v4madd(v4mul(l24[k], lx), l25[k], ly), l26[k], lz), zero);
        }
        for (int c = 0; c < 4; c++)   /* LIGHT4: vf05 = vf18 by vf28-31 (mem 56-59) */
            v5[c] = v4madd(v4madd(v4madd(v4mul(m2[0][c], v18[0]), m2[1][c], v18[1]), m2[2][c], v18[2]), m2[3][c], one);
        for (int c = 0; c < 3; c++)   /* colour: vf07/09/10/11 (mem 71-74) by vf08 */
            v7[c] = v4madd(v4madd(v4madd(v4mul(col[0][c], r8[0]), col[1][c], r8[1]), col[2][c], r8[2]), col[3][c], r8[3]);
        v7[3] = w7;
        {   /* CLIPw.xyz vf05, vf05w; FCGET; IAND vi11 */
            const V4 w = v4fabs(v5[3]), nw4 = v4fneg(w);
            const int gx = v4gt_bits(v5[0], w), lx = v4lt_bits(v5[0], nw4), gy = v4gt_bits(v5[1], w), ly = v4lt_bits(v5[1], nw4);
            const int gz = v4gt_bits(v5[2], w), lz = v4lt_bits(v5[2], nw4);
            for (int v = 0; v < valid; v++) {
                const uint32_t cb = (gx >> v & 1) | (lx >> v & 1) << 1 | (gy >> v & 1) << 2 | (ly >> v & 1) << 3 | (gz >> v & 1) << 4 | (lz >> v & 1) << 5;
                clip = ((clip << 6) | cb) & 0xFFFFFF;
                vi11 &= (uint16_t)(clip & 0xFFF);
            }
        }
        v18[3] = v4add(zero, v6z);   /* the fog slot: ADDz.x vf11, vf00, vf06z */
        v4transpose(&v7[0], &v7[1], &v7[2], &v7[3]);
        v4transpose(&v18[0], &v18[1], &v18[2], &v18[3]);
        v4transpose(&v6x, &v6y, &v6z, &v6w);
        for (int v = 0; v < 4; v++) {
            v4st((VuVec*)(dst + 64 * v + 16), v7[v]);
            v4st((VuVec*)(dst + 64 * v + 32), v18[v]);
        }
        v4st((VuVec*)(dst + 48), v6x); v4st((VuVec*)(dst + 112), v6y); v4st((VuVec*)(dst + 176), v6z); v4st((VuVec*)(dst + 240), v6w);
        if (valid < 4) for (int v = 0; v < valid; v++) memcpy(out + 64 * v + 16, tmp + 64 * v + 16, 48);
    }
    s->clip = clip;
    s->vi[11] = vi11;
    memset(&s->vf[8], 0, sizeof(s->vf[8]));   /* any finite vf08: the next vertex starts at 0 */
}


/* One scalar vertex through the original VU microprogram. Keep this path for
 * short chunks and the first/last vertices, whose VU state must remain exact. */
static void pcVu0CalcPointOne(const float* in, uint8_t* out)
{
    memcpy(&pcVu0.vf[4], in, sizeof(VuVec));
    memcpy(&pcVu0.vf[5], in + 4, sizeof(VuVec));
    pcVu0Micro(49); /* VU0_CALCPOINT */

    memcpy(out + 16, &pcVu0.vf[7], 16);
    memcpy(out + 32, &pcVu0.vf[18], 12);
    memcpy(out + 44, &pcVu0.vf[11], 4);
    memcpy(out + 48, &pcVu0.vf[6], 16);
}

/* Processes the vertices remaining after CALCPOINT_INIT + the first vertex.
 * Four vertices are evaluated side-by-side with SSE2. The final vertex is
 * executed by the original microcode so all the live VU state is left intact.
 * Set RECVX_VU0_BATCH=0 to compare against the old per-vertex route. */

/* Clip an indexed triangle strip using the same EE VU0 operation sequence as
 * njCnkCsUvh/Uvn, but keep the loop in one native C function. This removes the
 * per-vertex PcGpr/asm2c dispatch overhead while preserving the VU registers,
 * ACC, MAC flags and 24-bit clip-history side effects used by the old path. */
static void pcVu0StripClipTransform(const uint8_t* position)
{
    VuVec r_;
    int k_;

    memcpy(&pcVu0.vf[4], position, sizeof(VuVec));

    for (k_ = 0; k_ < 4; k_++)
    {
        r_.f[k_] = vu_f(pcVu0.vf[24].f[k_] * pcVu0.vf[4].f[0]);
    }
    pcVu0.acc = r_;
    vu_set_mac(&pcVu0, &r_, 15);

    for (k_ = 0; k_ < 4; k_++)
    {
        r_.f[k_] = vu_f(pcVu0.acc.f[k_] + pcVu0.vf[25].f[k_] * pcVu0.vf[4].f[1]);
    }
    pcVu0.acc = r_;
    vu_set_mac(&pcVu0, &r_, 15);

    for (k_ = 0; k_ < 4; k_++)
    {
        r_.f[k_] = vu_f(pcVu0.acc.f[k_] + pcVu0.vf[26].f[k_] * pcVu0.vf[4].f[2]);
    }
    pcVu0.acc = r_;
    vu_set_mac(&pcVu0, &r_, 15);

    for (k_ = 0; k_ < 4; k_++)
    {
        r_.f[k_] = vu_f(pcVu0.acc.f[k_] + pcVu0.vf[27].f[k_] * pcVu0.vf[0].f[3]);
    }
    memcpy(&pcVu0.vf[5], &r_, sizeof(VuVec));
    vu_set_mac(&pcVu0, &r_, 15);
}

uint32_t pcVu0StripClip(const unsigned short* idx, int n, const void* vbuf)
{
    const uint8_t* const vb = (const uint8_t*)vbuf;
    uint32_t coflag = 0x3F;
    int k;

    if (n <= 0)
    {
        return 0;
    }

    pcVu0StripClipTransform(vb + ((uint32_t)idx[0] << 6) + 32);

    for (k = 1; k < n; k++)
    {
        const uint8_t* const position = vb + ((uint32_t)idx[k * 3] << 6) + 32;

        /* A zero AND is already sufficient to accept the strip, exactly like
         * the branch around the rest of the original EE clipping loop. */
        if (coflag == 0)
        {
            return 0;
        }

        vu_clip(&pcVu0, &pcVu0.vf[5], pcVu0.vf[5].f[3]);
        pcVu0StripClipTransform(position);
        coflag &= pcVu0.clip;
    }

    vu_clip(&pcVu0, &pcVu0.vf[5], pcVu0.vf[5].f[3]);
    return coflag & pcVu0.clip;
}

int pcVu0CalcPointBatchEnabled(void)
{
    static int enabled = -1;
    if (enabled < 0)
    {
        const char* value = getenv("RECVX_VU0_BATCH");
        enabled = (value == NULL) || (atoi(value) != 0);
    }
    return enabled;
}

void pcVu0CalcPointN(const float* in, void* output, int n)
{
    PcVu0* const s = &pcVu0;
    uint8_t* out = (uint8_t*)output;

    if (n <= 0)
    {
        return;
    }

    if (n > 4)
    {
        int m;

        /* CALCPOINT_END restores vf28-vf31 from mem[60..63]. If that state
         * isn't present yet, let the first vertex run on the reference path. */
        if (memcmp(&s->vf[28], &s->mem[60], 64) != 0)
        {
            pcVu0CalcPointOne(in, out);
            in += 8;
            out += 64;
            n--;
        }

        m = n - 1; /* Leave the last vertex to the reference microprogram. */
        if (m > 0)
        {
            pcVu0CalcPointSoA(s, in, out, m);
            in += 8 * m;
            out += 64 * m;
            n -= m;
        }
    }

    for (; n > 0; n--, in += 8, out += 64)
    {
        pcVu0CalcPointOne(in, out);
    }
}
