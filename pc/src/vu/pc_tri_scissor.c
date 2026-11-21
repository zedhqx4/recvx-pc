#include <math.h>
#include <stdint.h>
#include "pc_vu.h"
#include "pc_tri_scissor.h"
#include "pc_platform.h"

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

/* Interseccion de la arista a->b con el plano: a es el extremo que se conserva como base. */
static void intersect(PcTriNode* r, const PcTriNode* a, const PcTriNode* b, float sgn, int axis)
{
    float da[3], db[3], t;
    float aw = vu_f(a->clipV[3] * sgn), bw = vu_f(b->clipV[3] * sgn);
    const float* pa = (const float*)a;
    const float* pb = (const float*)b;
    int k, q;

    for (k = 0; k < 3; k++)
    {
        da[k] = fabsf(vu_f(a->clipV[k] - aw));
        db[k] = fabsf(vu_f(b->clipV[k] - bw));
    }
    t = fabsf(vu_div(da[axis], vu_f(db[axis] + da[axis])));

    for (q = 0; q < 4; q++)
    {
        float* d = (float*)r + q * 4;
        for (k = 0; k < 4; k++)
            d[k] = vu_f(vu_f(vu_f(pb[q * 4 + k] - pa[q * 4 + k]) * t) + pa[q * 4 + k]);
    }
    if (r->vertex[2] < 1.0f) r->vertex[2] = 1.0f;     /* vmaxw.z */
}

int pcTriScissorPlane(const PcTriNode* in, int nIn, PcTriNode* out, float sgn, int mask, int axis, uint32_t* clipHist)
{
    const unsigned int csr = pcVuRoundEnter();
    uint32_t h = *clipHist;
    int j, n = 0;

    for (j = 0; j < nIn; j++)
    {
        const PcTriNode* cur = &in[j];
        const PcTriNode* nxt = &in[(j + 1 < nIn) ? j + 1 : 0];
        unsigned int clip;
        PcTriNode r;

        h = ((h << 6) | flags6(cur->clipV)) & 0xFFFFFF;
        h = ((h << 6) | flags6(nxt->clipV)) & 0xFFFFFF;
        clip = h & (unsigned int)mask;

        if (clip == 0)
        {
            out[n++] = *cur;
        }
        else if ((clip & 0x3F) && (clip & 0xFC0))
        {
            /* los dos fuera: nada */
        }
        else if (clip & 0x3F)       /* el siguiente fuera: el actual y la interseccion */
        {
            intersect(&r, cur, nxt, sgn, axis);
            out[n++] = *cur;
            out[n++] = r;
        }
        else                        /* el actual fuera: solo la interseccion (base = siguiente) */
        {
            intersect(&r, nxt, cur, sgn, axis);
            out[n++] = r;
        }
    }
    *clipHist = h;
    pcVuRoundLeave(csr);
    return n;
}
