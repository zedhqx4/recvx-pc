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
#include "pc_calcpoint.h"
#include "pc_platform.h"


typedef __m128 V4;
static __inline V4 v4clamp(V4 x)
{
    const V4 sg = _mm_set1_ps(-0.0f);
    V4 a = _mm_andnot_ps(sg, x);
    V4 keep = _mm_cmpnlt_ps(a, _mm_set1_ps(FLT_MIN));
    a = _mm_and_ps(_mm_min_ps(a, _mm_set1_ps(FLT_MAX)), keep);
    return _mm_or_ps(a, _mm_and_ps(sg, x));
}
float pcCN[16];   /* matriz CN (antes vf28..31) */

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

/* ------------------------------------------------------------------------
 * CALCPOINT puro (sin registros ni memoria de VU0). Misma matematica que
 * la antigua version SoA, con los parametros explicitos en PcCalcPointCtx.
 * ---------------------------------------------------------------------- */
static float s_view[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };

void pcViewSetClipMatrix(const float* m16) { memcpy(s_view, m16, sizeof(s_view)); }
const float* pcViewClipMatrix(void) { return s_view; }

void pcCalcPoint(const PcCalcPointCtx* c, const float* in, void* output, int n, uint32_t* clip_and)
{
    const unsigned int csr = pcVuRoundEnter();
    uint8_t* out = (uint8_t*)output;
    V4 m1[4][4], m2[4][4], col[4][3], l24[4], l25[4], l26[4], l27[4], l12[4], l13[4];
    const float sx = vu_f(vu_f(0.0f + c->aspect[0]) * vu_f(0.0f + c->projection));   /* vf17.xy * vf23.z */
    const float sy = vu_f(vu_f(0.0f + c->aspect[1]) * vu_f(0.0f + c->projection));
    const V4 one = v4set1(1.0f), zero = v4set1(0.0f), v17x = v4set1(sx), v17y = v4set1(sy);
    const V4 v01z = v4set1(1.0f), w7 = v4set1(c->light[3][3]);
    const uint16_t mask = (uint16_t)*(const int*)&c->light[3][0];
    const uint16_t vi7 = 128, vi8 = 8, vi9 = 240;
    int on = 0, point = 0;
    uint32_t acc = 0x3F;   /* vi11: AND de los flags de clip de todos los vertices */
    int r, k;
    for (r = 0; r < 4; r++)
    {
        int cc;
        for (cc = 0; cc < 4; cc++) { m1[r][cc] = v4set1(c->cn[4 * r + cc]); m2[r][cc] = v4set1(c->view[4 * r + cc]); }
        for (cc = 0; cc < 3; cc++) col[r][cc] = v4set1(c->light[7 + r][cc]);
    }
    for (k = 0; k < 4; k++)
    {
        const uint16_t vi14 = (uint16_t)(mask << k);
        l24[k] = v4set1(c->light[0][k]); l25[k] = v4set1(c->light[1][k]); l26[k] = v4set1(c->light[2][k]);
        l27[k] = v4set1(c->light[6][k]); l12[k] = v4set1(c->light[4][k]); l13[k] = v4set1(c->light[5][k]);
        if (vi14 & vi7) { on |= 1 << k; if (vi14 & vi8) point |= 1 << k; }
    }
    for (; n > 0; n -= 4, in += 32, out += 256)
    {
        const int valid = n < 4 ? n : 4;
        float pad[32];
        uint8_t tmp[256];
        const float* src = in;
        uint8_t* dst = out;
        V4 px, py, pz, pw, nx, ny, nz, nw;
        V4 v18[4], v19[3], v5[4], v7[4], v6x, v6y, v6z, v6w, q, r8[4];
        int cidx, v;
        if (valid < 4)
        {
            for (v = 0; v < 4; v++) memcpy(pad + 8 * v, in + 8 * (v < valid ? v : valid - 1), 32);
            src = pad; dst = tmp;
        }
        px = v4ld((const VuVec*)src); py = v4ld((const VuVec*)(src + 8)); pz = v4ld((const VuVec*)(src + 16)); pw = v4ld((const VuVec*)(src + 24));
        nx = v4ld((const VuVec*)(src + 4)); ny = v4ld((const VuVec*)(src + 12)); nz = v4ld((const VuVec*)(src + 20)); nw = v4ld((const VuVec*)(src + 28));
        v4transpose(&px, &py, &pz, &pw);
        v4transpose(&nx, &ny, &nz, &nw);
        for (cidx = 0; cidx < 4; cidx++)
            v18[cidx] = v4madd(v4madd(v4madd(v4mul(m1[0][cidx], px), m1[1][cidx], py), m1[2][cidx], pz), m1[3][cidx], one);
        for (cidx = 0; cidx < 3; cidx++)
            v19[cidx] = v4madd(v4madd(v4mul(m1[0][cidx], nx), m1[1][cidx], ny), m1[2][cidx], nz);
        q = v4div(one, v18[2]);
        v6x = v4mul(v4mul(v18[0], v17x), q);
        v6y = v4mul(v4mul(v18[1], v17y), q);
        v6z = v4mul(v01z, q);
        v6w = v4mul(one, v19[2]);
        r8[0] = r8[1] = r8[2] = r8[3] = v4zero();
        if (point)
        {
            V4 d2[4], dot[4];
            int m4z = 0, m4n = 0, m6z = 0, m6n = 0, m7z = 0, m7n = 0;
            uint16_t vi13[4], vi12[4];
            for (k = 0; k < 4; k++)
            {
                const V4 d9 = v4sub(l24[k], v18[0]), d10 = v4sub(l25[k], v18[1]), d11 = v4sub(l26[k], v18[2]);
                V4 s6, s7;
                d2[k] = v4madd(v4madd(v4mul(d9, d9), d10, d10), d11, d11);
                dot[k] = v4madd(v4madd(v4mul(d9, v19[0]), d10, v19[1]), d11, v19[2]);
                s6 = v4sub(l12[k], d2[k]); s7 = v4sub(l13[k], d2[k]);
                m4z |= v4zero_bits(dot[k]) << 4 * k; m4n |= v4neg_bits(dot[k]) << 4 * k;
                m6z |= v4zero_bits(s6) << 4 * k;     m6n |= v4neg_bits(s6) << 4 * k;
                m7z |= v4zero_bits(s7) << 4 * k;     m7n |= v4neg_bits(s7) << 4 * k;
            }
            for (v = 0; v < 4; v++)
            {
                uint32_t a = 0, b = 0, cc2 = 0;
                for (k = 0; k < 4; k++)
                {
                    a   |= (m4z >> (4 * k + v) & 1u) << (3 - k) | (m4n >> (4 * k + v) & 1u) << (7 - k);
                    b   |= (m6z >> (4 * k + v) & 1u) << (3 - k) | (m6n >> (4 * k + v) & 1u) << (7 - k);
                    cc2 |= (m7z >> (4 * k + v) & 1u) << (3 - k) | (m7n >> (4 * k + v) & 1u) << (7 - k);
                }
                vi13[v] = (uint16_t)((a & vi9) | (cc2 & vi9));
                vi12[v] = (uint16_t)(b & vi9);
            }
            for (k = 0; k < 4; k++)
            {
                int facing = 0, atten = 0;
                V4 t;
                if (!(point >> k & 1) || !(on >> k & 1)) continue;
                for (v = 0; v < 4; v++)
                {
                    const uint16_t bit = (uint16_t)(128 >> k);   /* vi[7 - k]: 128, 64, 32, 16 */
                    facing |= !(vi13[v] & bit) << v;
                    atten |= ((vi12[v] & bit) != 0) << v;
                }
                if (!facing) continue;
                t = v4mul(dot[k], v4div(one, v4sqrt(v4fabs(d2[k]))));
                r8[k] = v4blend(r8[k], v4blend(t, v4mul(t, v4div(l12[k], d2[k])), atten), facing);
            }
        }
        for (k = 0; k < 4; k++)
        {
            V4 lx, ly, lz;
            if (!(on >> k & 1) || (point >> k & 1)) continue;
            lx = v4mul(v19[0], l27[k]); ly = v4mul(v19[1], l27[k]); lz = v4mul(v19[2], l27[k]);
            r8[k] = v4max(v4madd(v4madd(v4mul(l24[k], lx), l25[k], ly), l26[k], lz), zero);
        }
        for (cidx = 0; cidx < 4; cidx++)
            v5[cidx] = v4madd(v4madd(v4madd(v4mul(m2[0][cidx], v18[0]), m2[1][cidx], v18[1]), m2[2][cidx], v18[2]), m2[3][cidx], one);
        for (cidx = 0; cidx < 3; cidx++)
            v7[cidx] = v4madd(v4madd(v4madd(v4mul(col[0][cidx], r8[0]), col[1][cidx], r8[1]), col[2][cidx], r8[2]), col[3][cidx], r8[3]);
        v7[3] = w7;
        {
            const V4 w = v4fabs(v5[3]), nw4 = v4fneg(w);
            const int gx = v4gt_bits(v5[0], w), lx = v4lt_bits(v5[0], nw4), gy = v4gt_bits(v5[1], w), ly = v4lt_bits(v5[1], nw4);
            const int gz = v4gt_bits(v5[2], w), lz = v4lt_bits(v5[2], nw4);
            for (v = 0; v < valid; v++)
            {
                const uint32_t cb = (gx >> v & 1) | (lx >> v & 1) << 1 | (gy >> v & 1) << 2 | (ly >> v & 1) << 3 | (gz >> v & 1) << 4 | (lz >> v & 1) << 5;
                acc &= cb;
            }
        }
        v18[3] = v4add(zero, v6z);   /* fog */
        v4transpose(&v7[0], &v7[1], &v7[2], &v7[3]);
        v4transpose(&v18[0], &v18[1], &v18[2], &v18[3]);
        v4transpose(&v6x, &v6y, &v6z, &v6w);
        for (v = 0; v < 4; v++)
        {
            v4st((VuVec*)(dst + 64 * v + 16), v7[v]);
            v4st((VuVec*)(dst + 64 * v + 32), v18[v]);
        }
        v4st((VuVec*)(dst + 48), v6x); v4st((VuVec*)(dst + 112), v6y); v4st((VuVec*)(dst + 176), v6z); v4st((VuVec*)(dst + 240), v6w);
        if (valid < 4) for (v = 0; v < valid; v++) memcpy(out + 64 * v + 16, tmp + 64 * v + 16, 48);
    }
    if (clip_and) *clip_and = acc;
    pcVuRoundLeave(csr);
}
