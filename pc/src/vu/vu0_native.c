/* PC port: microprogramas VU0 de recorte + color como funciones C (sin el interprete generado).
 *
 * Sustituyen a pcVu0Micro() para las tres entradas que corre el juego POR VERTICE de cada tira:
 *   237 VU0_CLIP_VIEW_VOLUME_ALL  (0x780)  mueve vf19<-vf18<-vf15, vf15 = M*vf14, 3 CLIPw, rutina de color
 *                                          vu0GetVertexColor* elegida en mem[187], y la cadena de FCOR -> vi02
 *   269 VU0_CLIP_VIEW_VOLUME      (0x888)  lo mismo sin rutina de color ni FCOR
 *   394 VU0_CALC_COLOR            (0xC70)  como 269 pero con un solo CLIPw (vf15) y rutina de color
 *
 * Cada operacion usa exactamente las mismas reglas de coma flotante de la VU que el codigo generado
 * (vu_f: denormal -> 0, inf/NaN -> max; fminf/fmaxf; truncado de cada resultado a float, un
 * redondeo por operacion; sin fusionar multiplicaciones y sumas). El orden de instrucciones, incluidos
 * los huecos de retardo de JR/JALR, es el del microcodigo (vsm/ps2_vu0.vsm).
 * Estado que se conserva igual: vf04/vf05/vf15/vf18/vf19, ACC, I, vi01/vi02/vi15, CLIP (24 bits).
 * No se mantienen MAC/STATUS (nadie los lee en estas rutas); el historial de banderas recibe una entrada.
 *
 * RECVX_VU0_NATIVE=0 vuelve al interprete; =2 ejecuta las dos versiones y compara el estado (diagnostico:
 * escribe "vu0 native MISMATCH" en recvx.log con el primer registro distinto). */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "pc_vu.h"

#include "pc_platform.h"
void pcVu0MicroInterp(int addr);

/* MINI/MAX de la VU comparan los patrones de bits como enteros con signo: max(-0, +0) = +0.
 * Comparar explicitamente evita que fminf/fmaxf devuelvan un cero de signo distinto segun el compilador. */
static __inline float f_min(float a, float b) { return vu_f((a < b) ? a : b); }
static __inline float f_max(float a, float b) { return vu_f((a > b) ? a : b); }

static void clamp01(VuVec* c)   /* MINIw.xyz c, c, vf00w ; MAXx.xyz c, c, vf00x */
{
    const float one = pcVu0.vf[0].f[3], zero = pcVu0.vf[0].f[0];
    int k;
    for (k = 0; k < 3; k++) c->f[k] = f_min(c->f[k], one);
    for (k = 0; k < 3; k++) c->f[k] = f_max(c->f[k], zero);
}

static void sub1(VuVec* d, const VuVec* a)   /* SUBw.xyz d, a, vf00w */
{
    int k;
    for (k = 0; k < 3; k++) d->f[k] = vu_f(a->f[k] - pcVu0.vf[0].f[3]);
}

/* ADD.xyz vf04, vf04, vf05 + MUL.w vf04, vf00, vf23 (hueco de retardo del JR) */
static void finish_sum(VuVec* c, VuVec* s)
{
    int k;
    for (k = 0; k < 3; k++) c->f[k] = vu_f(c->f[k] + s->f[k]);
    c->f[3] = vu_f(pcVu0.vf[0].f[3] * pcVu0.vf[23].f[3]);
}

static void finish_mul_alpha(VuVec* c)       /* MUL.w vf04, vf00, vf23 */
{
    c->f[3] = vu_f(pcVu0.vf[0].f[3] * pcVu0.vf[23].f[3]);
}

/* mode = indice de la tabla mem[176..186]: 0 Color, 1 CM, 2 Ignore, 3 Dif, 4 DifAmb, 5 DifSpe1, 6 DifSpe2,
 * 7 DifSpe3, 8 DifSpe1Amb, 9 DifSpe2Amb, 10 DifSpe3Amb. Devuelve el valor que deja en vi01. */
static int color_routine(int mode, int vi01_in)
{
    PcVu0* v = &pcVu0;
    VuVec* c = &v->vf[4];
    VuVec* s = &v->vf[5];
    const VuVec* d20 = &v->vf[20];
    const VuVec* d21 = &v->vf[21];
    int k, it;

    switch (mode)
    {
    case 0:   /* vu0GetVertexColor */
        for (k = 0; k < 3; k++) c->f[k] = vu_f(c->f[k] * v->vf[3].f[0]);
        c->f[3] = vu_f(c->f[3] * v->vf[3].f[1]);
        v->i = 255.0f;
        return vi01_in;
    case 1:   /* CM */
        for (k = 0; k < 3; k++) c->f[k] = vu_f(v->vf[0].f[k] + d20->f[k]);
        finish_mul_alpha(c);
        return vi01_in;
    case 2:   /* Ignore */
        for (k = 0; k < 3; k++) c->f[k] = vu_f(v->vf[0].f[k] + v->vf[3].f[0]);
        finish_mul_alpha(c);
        return vi01_in;
    case 3:   /* Dif */
        clamp01(c);
        for (k = 0; k < 3; k++) c->f[k] = vu_f(c->f[k] * d20->f[k]);
        finish_mul_alpha(c);
        return vi01_in;
    case 4:   /* DifAmb: ADD amb; MAXx; MINIw (en este orden); MUL dif */
        for (k = 0; k < 3; k++) c->f[k] = vu_f(c->f[k] + v->vf[22].f[k]);
        for (k = 0; k < 3; k++) c->f[k] = f_max(c->f[k], v->vf[0].f[0]);
        for (k = 0; k < 3; k++) c->f[k] = f_min(c->f[k], v->vf[0].f[3]);
        for (k = 0; k < 3; k++) c->f[k] = vu_f(c->f[k] * d20->f[k]);
        finish_mul_alpha(c);
        return vi01_in;
    case 5:   /* DifSpe1 */
        sub1(s, c);
        clamp01(c);
        clamp01(s);
        for (k = 0; k < 3; k++) c->f[k] = vu_f(c->f[k] * d20->f[k]);
        for (k = 0; k < 3; k++) s->f[k] = vu_f(s->f[k] * d21->f[k]);
        finish_sum(c, s);
        return vi01_in;
    case 6:   /* DifSpe2: s = (c + amb) - 1 */
        for (k = 0; k < 3; k++) s->f[k] = vu_f(c->f[k] + v->vf[22].f[k]);
        sub1(s, s);
        clamp01(c);
        clamp01(s);
        for (k = 0; k < 3; k++) c->f[k] = vu_f(c->f[k] * d20->f[k]);
        for (k = 0; k < 3; k++) s->f[k] = vu_f(s->f[k] * d21->f[k]);
        finish_sum(c, s);
        return vi01_in;
    case 7:   /* DifSpe3: s = 1 * vf01^17 */
        for (k = 0; k < 3; k++) s->f[k] = vu_f(v->vf[0].f[k] + v->vf[0].f[3]);   /* ADDw.xyz vf05, vf00, vf00w */
        for (it = 0; it < 17; it++)
            for (k = 0; k < 3; k++) s->f[k] = vu_f(s->f[k] * v->vf[1].f[k]);
        clamp01(c);
        clamp01(s);
        for (k = 0; k < 3; k++) c->f[k] = vu_f(c->f[k] * d20->f[k]);
        for (k = 0; k < 3; k++) s->f[k] = vu_f(s->f[k] * d21->f[k]);
        finish_sum(c, s);
        return 0;
    case 8:   /* DifSpe1Amb (suma vf18, como el microcodigo) */
        sub1(s, c);
        for (k = 0; k < 3; k++) s->f[k] = f_min(s->f[k], v->vf[0].f[3]);
        for (k = 0; k < 3; k++) s->f[k] = f_max(s->f[k], v->vf[0].f[0]);
        for (k = 0; k < 3; k++) c->f[k] = vu_f(c->f[k] + v->vf[18].f[k]);
        clamp01(c);
        for (k = 0; k < 3; k++) c->f[k] = vu_f(c->f[k] * d20->f[k]);
        for (k = 0; k < 3; k++) s->f[k] = vu_f(s->f[k] * d21->f[k]);
        finish_sum(c, s);
        return vi01_in;
    case 9:   /* DifSpe2Amb: c += amb; s = c - 1 */
        for (k = 0; k < 3; k++) c->f[k] = vu_f(c->f[k] + v->vf[22].f[k]);
        sub1(s, c);
        clamp01(c);
        clamp01(s);
        for (k = 0; k < 3; k++) c->f[k] = vu_f(c->f[k] * d20->f[k]);
        for (k = 0; k < 3; k++) s->f[k] = vu_f(s->f[k] * d21->f[k]);
        finish_sum(c, s);
        return vi01_in;
    case 10:  /* DifSpe3Amb: c += amb; s = (c + 1) * c^17 */
        for (k = 0; k < 3; k++) c->f[k] = vu_f(c->f[k] + v->vf[22].f[k]);
        for (k = 0; k < 3; k++) s->f[k] = vu_f(c->f[k] + v->vf[0].f[3]);        /* ADDw.xyz vf05, vf04, vf00w */
        for (it = 0; it < 17; it++)
            for (k = 0; k < 3; k++) s->f[k] = vu_f(s->f[k] * c->f[k]);
        clamp01(c);
        clamp01(s);
        for (k = 0; k < 3; k++) c->f[k] = vu_f(c->f[k] * d20->f[k]);
        for (k = 0; k < 3; k++) s->f[k] = vu_f(s->f[k] * d21->f[k]);
        finish_sum(c, s);
        return 0;
    }
    return vi01_in;
}

/* MOVE vf19<-vf18, MOVE vf18<-vf15, vf15 = ((vf24*x + vf25*y) + vf26*z) + vf27*1 con ACC por etapas */
static void transform_history(void)
{
    PcVu0* v = &pcVu0;
    const VuVec p = v->vf[14];
    VuVec r;
    int k;

    v->vf[19] = v->vf[18];
    v->vf[18] = v->vf[15];

    for (k = 0; k < 4; k++) r.f[k] = vu_f(v->vf[24].f[k] * p.f[0]);
    v->acc = r;
    for (k = 0; k < 4; k++) r.f[k] = vu_f(v->acc.f[k] + v->vf[25].f[k] * p.f[1]);
    v->acc = r;
    for (k = 0; k < 4; k++) r.f[k] = vu_f(v->acc.f[k] + v->vf[26].f[k] * p.f[2]);
    v->acc = r;
    for (k = 0; k < 4; k++) r.f[k] = vu_f(v->acc.f[k] + v->vf[27].f[k] * v->vf[0].f[3]);
    v->vf[15] = r;
}

static int mode_from_target(void)
{
    static int last_target = -1, last_mode = -1;
    const int target = (int)(pcVu0.mem[187 & 255].u[0] & 0xFFFF);
    int m;

    if (target == last_target && last_mode >= 0 && (int)(pcVu0.mem[(176 + last_mode) & 255].u[0] & 0xFFFF) == target)
        return last_mode;
    for (m = 0; m < 11; m++)
    {
        if ((int)(pcVu0.mem[(176 + m) & 255].u[0] & 0xFFFF) == target)
        {
            last_target = target;
            last_mode = m;
            return m;
        }
    }
    return -1;
}

static int native_run(int addr)
{
    PcVu0* v = &pcVu0;
    int mode = 0;

    if (addr == 237 || addr == 394)
    {
        mode = mode_from_target();
        if (mode < 0) return 0;   /* tabla sin inicializar: que lo resuelva el interprete */
    }

    transform_history();

    if (addr == 269)
    {
        vu_clip(v, &v->vf[19], v->vf[19].f[3]);
        vu_clip(v, &v->vf[18], v->vf[18].f[3]);
        vu_clip(v, &v->vf[15], v->vf[15].f[3]);
        v->cycle += 12;
        return 1;
    }

    if (addr == 394)
    {
        const int target = (int)(v->mem[187 & 255].u[0] & 0xFFFF);
        vu_clip(v, &v->vf[15], v->vf[15].f[3]);
        v->vi[15] = 399;
        v->vi[1] = color_routine(mode, target);
        v->cycle += 24;
        return 1;
    }

    /* 237 */
    {
        static const unsigned int fcor[6] = { 0xDF7DF, 0xEFBEF, 0x7DF7, 0xBEFB, 0xDF7D, 0xEFBE };
        const int target = (int)(v->mem[187 & 255].u[0] & 0xFFFF);
        int i, hit = 0, last = 0;

        vu_clip(v, &v->vf[19], v->vf[19].f[3]);
        vu_clip(v, &v->vf[18], v->vf[18].f[3]);
        vu_clip(v, &v->vf[15], v->vf[15].f[3]);
        v->vi[15] = 247;
        (void)color_routine(mode, target);
        v->cycle += 24;

        for (i = 0; i < 6; i++)
        {
            last = (((v->clip | fcor[i]) & 0xFFFFFF) == 0xFFFFFF);
            if (last) { hit = 1; break; }
        }
        v->vi[1] = last;
        v->vi[2] = hit ? 0 : 1;
        v->cycle += 12;
        return 1;
    }
}

static int native_mode(void)
{
    static int mode = -1;
    if (mode < 0)
    {
        const char* e = getenv("RECVX_VU0_NATIVE");
        mode = (e != NULL) ? atoi(e) : pcIniGetInt("vu0_native", 1);   /* entorno RECVX_VU0_NATIVE o vu0_native= en recvx.ini */
    }
    return mode;
}

/* Punto de entrada: devuelve 1 si la llamada ya se resolvio de forma nativa. */
int pcVu0NativeMicro(int addr)
{
    static PcVu0 before, native;
    static int reported = 0;
    const int mode = native_mode();

    if ((addr != 237) && (addr != 269) && (addr != 394)) return 0;
    if (mode == 0) return 0;

    if (mode != 2) return native_run(addr);

    /* A/B: nativa y interprete sobre el mismo estado de partida; se deja el resultado del interprete */
    memcpy(&before, &pcVu0, sizeof(before));
    if (!native_run(addr)) return 0;
    memcpy(&native, &pcVu0, sizeof(native));
    memcpy(&pcVu0, &before, sizeof(before));
    pcVu0MicroInterp(addr);

    if (reported < 20)
    {
        int r, c;
        for (r = 0; r < 32; r++)
            for (c = 0; c < 4; c++)
                if (memcmp(&native.vf[r].u[c], &pcVu0.vf[r].u[c], 4) != 0 && reported < 20)
                {
                    pcLog("vu0 native MISMATCH micro %d: vf%02d.%c native %08x interp %08x", addr, r, "xyzw"[c],
                          native.vf[r].u[c], pcVu0.vf[r].u[c]);
                    reported++;
                }
        for (r = 0; r < 4; r++)
            if (memcmp(&native.acc.u[r], &pcVu0.acc.u[r], 4) != 0 && reported < 20)
            {
                pcLog("vu0 native MISMATCH micro %d: ACC.%c native %08x interp %08x", addr, "xyzw"[r], native.acc.u[r], pcVu0.acc.u[r]);
                reported++;
            }
        for (r = 1; r < 16; r++)
            if (native.vi[r] != pcVu0.vi[r] && reported < 20)
            {
                pcLog("vu0 native MISMATCH micro %d: vi%02d native %d interp %d", addr, r, native.vi[r], pcVu0.vi[r]);
                reported++;
            }
        if (native.clip != pcVu0.clip && reported < 20)
        {
            pcLog("vu0 native MISMATCH micro %d: clip native %06x interp %06x", addr, native.clip, pcVu0.clip);
            reported++;
        }
    }
    return 1;
}

int pcVuTruncEnabled(void)
{
    static int on = -1;
    if (on < 0)
    {
        const char* e = getenv("RECVX_VU_TRUNC");
        on = (e != NULL) ? (atoi(e) != 0) : (pcIniGetInt("vu_trunc", 1) != 0);
        pcLog("VU rounding: %s", on ? "truncate toward zero + flush denormals (EE/VU)" : "round to nearest (host default)");
    }
    return on;
}
