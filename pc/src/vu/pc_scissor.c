#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "pc_vu.h"
#include "pc_scissor.h"
#include "pc_platform.h"

typedef struct { float q[4][4]; } ScVert;
static ScVert g_buf[2][16];

static unsigned int flags6(const float* a)   /* CLIPw.xyz: 6 bits contra +-w */
{
    const float aw = a[3] < 0 ? -a[3] : a[3];
    unsigned int f = 0;
    if (a[0] > aw)  f |= 0x01;
    if (a[0] < -aw) f |= 0x02;
    if (a[1] > aw)  f |= 0x04;
    if (a[1] < -aw) f |= 0x08;
    if (a[2] > aw)  f |= 0x10;
    if (a[2] < -aw) f |= 0x20;
    return f;
}

void pcScissorSetNode(const void* pS, const void* pP, const float* h19, const float* h18, const float* h15)
{
    const uint8_t* s = (const uint8_t*)pS;
    const uint8_t* p = (const uint8_t*)pP;
    ScVert* d = g_buf[0];

    memcpy(d[0].q[0], h19, 16); memcpy(d[0].q[1], s - 0x60, 16); memcpy(d[0].q[2], s - 0x80, 16); memcpy(d[0].q[3], p - 0x50, 16);
    memcpy(d[1].q[0], h18, 16); memcpy(d[1].q[1], s - 0x20, 16); memcpy(d[1].q[2], s - 0x40, 16); memcpy(d[1].q[3], p - 0x20, 16);
    memcpy(d[2].q[0], h15, 16); memcpy(d[2].q[1], s + 0x20, 16); memcpy(d[2].q[2], s,        16); memcpy(d[2].q[3], p + 0x10, 16);
    d[3] = d[0];
}

int pcScissorClipPlane(int mask1, int mask2, int xyzflg, float sin, int work0, int work1, int count)
{
    const unsigned int csr = pcVuRoundEnter();
    const ScVert* src = g_buf[work0 == 80 ? 0 : 1];
    ScVert* dst = g_buf[work1 == 80 ? 0 : 1];
    int pin = 0, pout = 0, ret = 0, n = count;
    ScVert prev, cur;

    cur = src[pin++];
    while (n != 0)
    {
        unsigned int flags;
        int crossing = 0;

        prev = cur;
        cur = src[pin++];
        flags = (flags6(prev.q[0]) << 6) | flags6(cur.q[0]);

        if (flags & (unsigned int)mask1)
        {
            if (!(flags & (unsigned int)mask2)) crossing = 1;      /* anterior fuera, actual dentro: solo la interseccion */
        }
        else if (flags & (unsigned int)mask2)
        {
            dst[pout++] = prev; ret++;
            crossing = 1;                                          /* anterior dentro, actual fuera: el vertice y la interseccion */
        }
        else
        {
            dst[pout++] = prev; ret++;
        }

        if (crossing)
        {
            float a[4], c[4], t;
            int k, qd;
            for (k = 0; k < 4; k++)
            {
                a[k] = vu_f(prev.q[0][k] - prev.q[0][3] * sin);
                c[k] = vu_f(cur.q[0][k] - cur.q[0][3] * sin);
            }
            t = fabsf(vu_div(a[xyzflg], vu_f(c[xyzflg] - a[xyzflg])));
            for (qd = 0; qd < 4; qd++)
                for (k = 0; k < 4; k++)
                    prev.q[qd][k] = vu_f(prev.q[qd][k] + vu_f(cur.q[qd][k] - prev.q[qd][k]) * t);
            if (prev.q[1][2] < 1.0f) prev.q[1][2] = 1.0f;          /* vmaxw.z vf5, vf5, vf0w */
            dst[pout++] = prev; ret++;
        }
        n--;
    }
    dst[pout++] = dst[0];                                          /* cierre del poligono (LOAD_WORKb + STORE) */
    pcVuRoundLeave(csr);
    return ret;
}

void pcScissorEmit(void* out, size_t stride, int count, const float* aspect, const float* offset, float proj)
{
    const unsigned int csr = pcVuRoundEnter();
    uint8_t* o = (uint8_t*)out;
    const ScVert* src = g_buf[0];
    int i, k;

    for (i = 0; i < count; i++)
    {
        float v9[4], v10[4], q, z4;
        memcpy(v9, src[i].q[1], 16);
        memcpy(v10, src[i].q[2], 16);
        q = vu_div(1.0f, v9[2]);
        v10[3] = vu_f(0.0f * 0.0f);
        v9[0] = vu_f(v9[0] * aspect[0]);
        v9[1] = vu_f(v9[1] * aspect[1]);
        v10[2] = vu_f(0.0f + 1.0f);
        z4 = vu_f(proj * q);
        for (k = 0; k < 3; k++) v10[k] = vu_f(v10[k] * q);
        v9[0] = vu_f(v9[0] * z4);
        v9[1] = vu_f(v9[1] * z4);
        v9[0] = vu_f(v9[0] + offset[0]);
        v9[1] = vu_f(v9[1] + offset[1]);
        o += stride;
        memcpy(o - 0x30, v10, 16);
        memcpy(o - 0x20, src[i].q[3], 16);
        memcpy(o - 0x10, v9, 16);
    }
    pcVuRoundLeave(csr);
}
