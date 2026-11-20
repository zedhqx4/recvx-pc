#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "pc_vu.h"
#include "pc_clip.h"
#include "pc_calcpoint.h"

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

void pcClipStep(PcClipState* s, const float* p, const float* m, int kind)
{
    const int all = (kind == 1);
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
    memcpy(s->p14, p, sizeof(s->p14));

    if (kind != 2)
    {
        clip_push(s, s->h19);
        clip_push(s, s->h18);
    }
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

static void state_load(PcClipState* s)
{
    memcpy(s->h15, pcVu0.vf[15].f, 16);
    memcpy(s->h18, pcVu0.vf[18].f, 16);
    memcpy(s->h19, pcVu0.vf[19].f, 16);
    memcpy(s->p14, pcVu0.vf[14].f, 16);
    s->clip = pcVu0.clip;
    s->vi1 = pcVu0.vi[1];
    s->vi2 = pcVu0.vi[2];
}

static void state_store(const PcClipState* s, int all)
{
    memcpy(pcVu0.vf[15].f, s->h15, 16);
    memcpy(pcVu0.vf[18].f, s->h18, 16);
    memcpy(pcVu0.vf[19].f, s->h19, 16);
    memcpy(pcVu0.vf[14].f, s->p14, 16);
    pcVu0.clip = s->clip;
    if (all) { pcVu0.vi[1] = s->vi1; pcVu0.vi[2] = s->vi2; }
}

void pcClipViewVolume(const float* p4, int all)
{
    PcClipState s;

    state_load(&s);
    pcClipStep(&s, p4, pcViewClipMatrix(), all);   /* all: 0 = 269, 1 = 237 */
    state_store(&s, all);
}

void pcClipResetHistory(void)
{
    static const float zero4[4] = { 0.0f, 0.0f, 0.0f, 1.0f };   /* vf0 */
    memcpy(pcVu0.vf[15].f, zero4, 16);
    memcpy(pcVu0.vf[18].f, zero4, 16);
    memcpy(pcVu0.vf[19].f, zero4, 16);
}

/* ---- color de los micros 237/394 ---- */
static struct { float dif[3], spe[3], amb[3], alpha; int mode; } mat;

void pcColorSetDiffuse(const float* rgb)
{
    const unsigned int csr = pcVuRoundEnter();
    int k;
    for (k = 0; k < 3; k++) mat.dif[k] = vu_f(vu_f(rgb[k] * 128.0f) + 0.0f);
    pcVuRoundLeave(csr);
}
void pcColorSetSpecula(const float* rgb)
{
    const unsigned int csr = pcVuRoundEnter();
    int k;
    for (k = 0; k < 3; k++) mat.spe[k] = vu_f(vu_f(rgb[k] * 31.0f) + 0.0f);
    pcVuRoundLeave(csr);
}
void pcColorSetAmbient(const float* rgb)
{
    int k;
    for (k = 0; k < 3; k++) mat.amb[k] = rgb[k];
}
void pcColorSetAlphaRatio(float ratio)
{
    const unsigned int csr = pcVuRoundEnter();
    mat.alpha = vu_f(1.0f * ratio);
    pcVuRoundLeave(csr);
}
void pcClipSetColorMode(int mode) { mat.mode = mode; }

static float cmin(float a, float b) { return vu_f((a < b) ? a : b); }   /* MINI/MAX de la VU: max(-0,+0) = +0 */
static float cmax(float a, float b) { return vu_f((a > b) ? a : b); }
static void clamp01(float* c) { int k; for (k = 0; k < 3; k++) c[k] = cmin(c[k], 1.0f); for (k = 0; k < 3; k++) c[k] = cmax(c[k], 0.0f); }

/* Las rutinas vu0GetVertexColor* de los micros 237/394 (no las vu1GetVertexColor* de pc_vu1_color.h: otro juego de
 * registros y orden de operaciones). c = vf4 (entrada y salida); h18 = historial de clip (el modo 8 lo suma, como el micro). */
static void color_mode(int mode, float* c, const float* h18)
{
    float s[3];
    const float* d = mat.dif;
    const float* sp = mat.spe;
    const float* a = mat.amb;
    int k, it;

    switch (mode)
    {
    case 0:
        for (k = 0; k < 3; k++) c[k] = vu_f(c[k] * 128.0f);
        c[3] = vu_f(c[3] * 256.0f);
        return;
    case 1:
        for (k = 0; k < 3; k++) c[k] = vu_f(0.0f + d[k]);
        c[3] = vu_f(1.0f * mat.alpha); return;
    case 2:
        for (k = 0; k < 3; k++) c[k] = vu_f(0.0f + 128.0f);
        c[3] = vu_f(1.0f * mat.alpha); return;
    case 3:
        clamp01(c);
        for (k = 0; k < 3; k++) c[k] = vu_f(c[k] * d[k]);
        c[3] = vu_f(1.0f * mat.alpha); return;
    case 4:
        for (k = 0; k < 3; k++) c[k] = vu_f(c[k] + a[k]);
        for (k = 0; k < 3; k++) c[k] = cmax(c[k], 0.0f);
        for (k = 0; k < 3; k++) c[k] = cmin(c[k], 1.0f);
        for (k = 0; k < 3; k++) c[k] = vu_f(c[k] * d[k]);
        c[3] = vu_f(1.0f * mat.alpha); return;
    case 5:
        for (k = 0; k < 3; k++) s[k] = vu_f(c[k] - 1.0f);
        break;
    case 6:
        for (k = 0; k < 3; k++) s[k] = vu_f(c[k] + a[k]);
        for (k = 0; k < 3; k++) s[k] = vu_f(s[k] - 1.0f);
        break;
    case 7:
        for (k = 0; k < 3; k++) s[k] = vu_f(0.0f + 1.0f);
        for (it = 0; it < 17; it++) for (k = 0; k < 3; k++) s[k] = vu_f(s[k] * 1.0f);
        break;
    case 8:
        for (k = 0; k < 3; k++) s[k] = vu_f(c[k] - 1.0f);
        for (k = 0; k < 3; k++) s[k] = cmin(s[k], 1.0f);
        for (k = 0; k < 3; k++) s[k] = cmax(s[k], 0.0f);
        for (k = 0; k < 3; k++) c[k] = vu_f(c[k] + h18[k]);
        clamp01(c);
        goto mulsum;
    case 9:
        for (k = 0; k < 3; k++) c[k] = vu_f(c[k] + a[k]);
        for (k = 0; k < 3; k++) s[k] = vu_f(c[k] - 1.0f);
        break;
    case 10:
        for (k = 0; k < 3; k++) c[k] = vu_f(c[k] + a[k]);
        for (k = 0; k < 3; k++) s[k] = vu_f(c[k] + 1.0f);
        for (it = 0; it < 17; it++) for (k = 0; k < 3; k++) s[k] = vu_f(s[k] * c[k]);
        break;
    default:
        return;
    }
    clamp01(c);
    clamp01(s);
mulsum:
    for (k = 0; k < 3; k++) c[k] = vu_f(c[k] * d[k]);
    for (k = 0; k < 3; k++) s[k] = vu_f(s[k] * sp[k]);
    for (k = 0; k < 3; k++) c[k] = vu_f(c[k] + s[k]);
    c[3] = vu_f(1.0f * mat.alpha);
}

void pcClipColorVertex(const float* p4, int kind)
{
    const unsigned int csr = pcVuRoundEnter();
    PcClipState s;
    float color[4];

    state_load(&s);
    pcClipStep(&s, p4 ? p4 : s.p14, pcViewClipMatrix(), kind);
    memcpy(color, pcVu0.vf[4].f, 16);
    color_mode(mat.mode, color, s.h18);
    pcVuRoundLeave(csr);
    state_store(&s, kind == 1);
    memcpy(pcVu0.vf[4].f, color, 16);
}
