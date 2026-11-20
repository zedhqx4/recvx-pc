#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "pc_vu.h"
#include "pc_clip.h"
#include "pc_calcpoint.h"
#include "pc_platform.h"

static void clip_push(PcClipState* s, const float* a)
{
    const float aw = a[3] < 0 ? -a[3] : a[3];
    unsigned int f = 0;

    if (a[0] > aw)  f |= 0x01;
    if (a[0] < -aw) f |= 0x02;
    if (a[1] > aw)  f |= 0x04;
    if (a[1] < -aw) f |= 0x08;
    if (a[2] > aw)  f |= 0x10;
    if (a[2] < -aw) f |= 0x20;
    s->clip = ((s->clip << 6) | f) & 0xFFFFFF;
}

void pcClipStep(PcClipState* s, const float* p, const float* m, int all)
{
    static const unsigned int fcor[6] = { 0xDF7DF, 0xEFBEF, 0x7DF7, 0xBEFB, 0xDF7D, 0xEFBE };
    const unsigned int csr = pcVuRoundEnter();
    float r[4], acc[4];
    int k;

    memcpy(s->h19, s->h18, sizeof(s->h19));
    memcpy(s->h18, s->h15, sizeof(s->h18));

    /* ACC por etapas, como MULAx / MADDAy / MADDAz / MADDw */
    for (k = 0; k < 4; k++) acc[k] = vu_f(m[k] * p[0]);
    for (k = 0; k < 4; k++) acc[k] = vu_f(acc[k] + m[4 + k] * p[1]);
    for (k = 0; k < 4; k++) acc[k] = vu_f(acc[k] + m[8 + k] * p[2]);
    for (k = 0; k < 4; k++) r[k] = vu_f(acc[k] + m[12 + k] * 1.0f);
    memcpy(s->h15, r, sizeof(s->h15));

    clip_push(s, s->h19);
    clip_push(s, s->h18);
    clip_push(s, s->h15);

    if (all)
    {
        int i, hit = 0, last = 0;
        for (i = 0; i < 6; i++)
        {
            last = (((s->clip | fcor[i]) & 0xFFFFFF) == 0xFFFFFF);
            if (last) { hit = 1; break; }
        }
        s->vi1 = last;
        s->vi2 = hit ? 0 : 1;
    }
    pcVuRoundLeave(csr);
}

static int clip_mode(void)
{
    static int mode = -1;
    if (mode < 0)
    {
        const char* e = getenv("RECVX_CLIP_NATIVE");
        mode = (e != NULL) ? atoi(e) : pcIniGetInt("clip_native", 0);
    }
    return mode;
}

static void state_load(PcClipState* s)
{
    memcpy(s->h15, pcVu0.vf[15].f, 16);
    memcpy(s->h18, pcVu0.vf[18].f, 16);
    memcpy(s->h19, pcVu0.vf[19].f, 16);
    s->clip = pcVu0.clip;
    s->vi1 = pcVu0.vi[1];
    s->vi2 = pcVu0.vi[2];
}

static void state_store(const PcClipState* s, int all)
{
    memcpy(pcVu0.vf[15].f, s->h15, 16);
    memcpy(pcVu0.vf[18].f, s->h18, 16);
    memcpy(pcVu0.vf[19].f, s->h19, 16);
    pcVu0.clip = s->clip;
    if (all) { pcVu0.vi[1] = s->vi1; pcVu0.vi[2] = s->vi2; }
}

static void verify(const PcClipState* nat, int all)
{
    static unsigned int calls = 0, bad = 0;
    PcClipState ref;
    int diff = 0;
    calls++;
    state_load(&ref);
    if (memcmp(ref.h15, nat->h15, 16) || memcmp(ref.h18, nat->h18, 16) || memcmp(ref.h19, nat->h19, 16)) diff |= 1;
    if (ref.clip != nat->clip) diff |= 2;
    if (all && (ref.vi1 != nat->vi1 || ref.vi2 != nat->vi2)) diff |= 4;
    if (diff)
    {
        if (bad < 12)
            pcLog("clip MISMATCH (%s) dif=%d: micro h15 %08x %08x %08x %08x clip %06x vi1 %d vi2 %d | nativa h15 %08x %08x %08x %08x clip %06x vi1 %d vi2 %d",
                  all ? "237" : "269", diff,
                  ((const unsigned*)ref.h15)[0], ((const unsigned*)ref.h15)[1], ((const unsigned*)ref.h15)[2], ((const unsigned*)ref.h15)[3], ref.clip, ref.vi1, ref.vi2,
                  ((const unsigned*)nat->h15)[0], ((const unsigned*)nat->h15)[1], ((const unsigned*)nat->h15)[2], ((const unsigned*)nat->h15)[3], nat->clip, nat->vi1, nat->vi2);
        bad++;
    }
    if ((calls & 0x3FFF) == 1) pcLog("clip verify: %u llamadas, %u con diferencias", calls, bad);
}

void pcClipViewVolume(const float* p4, int all)
{
    const int mode = clip_mode();
    PcClipState s;

    if (mode != 0)
    {
        state_load(&s);
        pcClipStep(&s, p4, pcViewClipMatrix(), all);
        if (mode == 1)
        {
            state_store(&s, all);
            return;
        }
    }
    memcpy(&pcVu0.vf[14], p4, 16);
    pcVu0Micro(all ? 237 : 269);
    if (mode == 2) verify(&s, all);
}
