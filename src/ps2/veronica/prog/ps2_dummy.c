#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "../../../../pc/include/pc_vu.h"
#include "../../../../pc/include/pc_game_profile.h"
#include "../../../ps2/veronica/prog/ps2_dummy.h"
#include "../../../ps2/veronica/prog/ps2_loadtim2.h"
#include "../../../ps2/veronica/prog/ps2_NaDraw2D.h"
#include "../../../ps2/veronica/prog/ps2_NaMath.h"
#include "../../../ps2/veronica/prog/ps2_NaSystem.h"
#include "../../../ps2/veronica/prog/ps2_NaTextureFunction.h"
#include "../../../ps2/veronica/prog/ps2_sg_pad.h"
#include "../../../ps2/veronica/prog/ps2_texture.h"
#include "../../../ps2/veronica/prog/sdfunc.h"
#include "../../../ps2/veronica/prog/main.h"
#include "pc_renderer.h"

#define PC_TIM2_FINGERPRINT_SLOTS 1024
typedef struct {
    int valid;
    const void* surface;
    unsigned int gindex;
    unsigned int image_size, clut_size;
    unsigned short header_size, image_width, image_height;
    unsigned char format_id, image_type, clut_type, pict_format;
    uint64_t image_hash, clut_hash;
} PcTim2SourceFingerprint;
static PcTim2SourceFingerprint pc_tim2_fingerprints[PC_TIM2_FINGERPRINT_SLOTS];

typedef struct {
    int valid;
    uint64_t state_generation, image_hash, clut_hash, tex0;
    unsigned int gindex, tbw, tw, th, psm;
    unsigned int image_width, image_height, pict_format, image_type, clut_type, clamp_flag, bank, render_sub_flag;
} PcPs2TextureBindCache;
static PcPs2TextureBindCache pc_ps2_texture_bind_cache;

static uint64_t pc_tim2_hash_bytes(const unsigned char* data, unsigned int size)
{
    uint64_t hash = 14695981039346656037ULL;
    unsigned int i;
    for (i = 0; i < size; i++)
        hash = (hash ^ data[i]) * 1099511628211ULL;
    return hash;
}

extern void* Ps2_tex_cache_buff[4];
extern unsigned int Ps2_tex_cache_beflag[4];

void Ps2PcTextureSourceCacheReset(void)
{
    unsigned int i;

    memset(pc_tim2_fingerprints, 0, sizeof(pc_tim2_fingerprints));
    memset(&pc_ps2_texture_bind_cache, 0, sizeof(pc_ps2_texture_bind_cache));

    /* Registration may reuse a source address for different bytes. The legacy pointer cache
     * must not report that new source as already transferred. */
    for (i = 0; i < 4; i++)
    {
        Ps2_tex_cache_buff[i] = NULL;
        Ps2_tex_cache_beflag[i] = 0;
    }
    Ps2_current_texbreak = 1;
}
// the three vars below were originally defined as unsigned int in ps2_NaFog.c, and redeclared as unsigned long here
extern unsigned long ulNaFogR;
extern unsigned long ulNaFogG;
extern unsigned long ulNaFogB;

void (*EorFunc)();
void (*VsyncFunc)();
unsigned int Ps2_vcount;
unsigned int Ps2_dbuff;
unsigned int Ps2_njControl3D_flag;
unsigned int Ps2_sys_cnt;
unsigned char Ps2_DRAW_TMP[16384];
NJS_SCREEN _nj_screen_;
unsigned int _nj_control_3d_flag_;
NJS_VERTEX_BUF* _nj_vertex_buf_;
Uint8* _BSG_END;
unsigned int Ps2_use_pt_flag;
NJS_MATRIX crmat;
float cmmat[2][16]; 
NJS_MATRIX lcmat[12];
float mbuf[128][16];
unsigned int palbuf[4096];
unsigned char Ps2_tex_mem[10485760];
unsigned char Ps2_PBUFF[1835008] __attribute__((aligned(64)));
PS2_TP_CACHE ps2_tp_cache[64];
float Ps2_shadow_vec[4];
float Ps2_shadow_fog;
int Ps2_shadow_z;
void* Ps2_tex_cache_buff[4];
unsigned int Ps2_tex_cache_beflag[4];
unsigned int Ps2_tex_cache_num;
PS2_GS_SAVE Ps2_gs_save;
NJS_TEXMEMLIST* Ps2_now_tex;
unsigned int Ps2_now_bank;
float Ps2AddPrimPrio;
int ViewType;
unsigned int Ps2_albinoid_flag;
unsigned int Ps2_ice_flag;
unsigned int PS2_Render_tex_sub_flag;
unsigned int Ps2_tex_load_tp_cancel;
/* unused below
unsigned int Ps2_highlight;
float Ps2_rand_seed[4];
unsigned int Ps2_divide_flag;
int _nj_tex_count;*/

unsigned char* Ps2_MOVIE = &Ps2_PBUFF[1179648]; 









// 99.93% matching
extern int pcIniGetInt(const char* key, int def);
extern void pcIniSet(const char* key, const char* value);

/* Upscaling: 2D pieces drawn texel for pixel (UI panels, backgrounds) often start half a texel
 * into their image (u = 60.5). At the PS2's resolution the screen samples never fall in that
 * outer half texel; upscaled, the outermost sub-pixels do and pick up a sliver of the next image
 * on the sheet: thin lines at every tile edge. Moving such a piece's texture coordinates to the
 * texel boundary keeps the texels the PS2 shows and stops the bleeding. UV (integer) coordinates
 * only: U and V are 1/16 texel in words 0 and 1 of each vertex, x and y floats in words 8, 9. */
static void pc_snap_uv(unsigned int* v, unsigned int num)
{
    int axis;
    unsigned int i;

    for (axis = 0; axis < 2; axis++)   /* U along x, V along y */
    {
        float tmin = 1e9f, tmax = -1e9f, smin = 1e9f, smax = -1e9f, t_at_smin = 0, off;
        int k;

        for (i = 0; i < num; i++)
        {
            float t = (v[(i * 12) + axis] & 0x3FFF) / 16.0f;
            float sc = ((float*)v)[(i * 12) + 8 + axis];

            if (t < tmin) tmin = t;
            if (t > tmax) tmax = t;

            if (sc < smin)
            {
                smin = sc;
                t_at_smin = t;
            }

            if (sc > smax) smax = sc;
        }

        /* only texel-for-pixel pieces that are not mirrored */
        if ((smax - smin < 1.0f) || (fabsf((tmax - tmin) - (smax - smin)) > 0.1f * (smax - smin)) || (t_at_smin != tmin))
        {
            continue;
        }

        off = floorf(tmin + 0.5f) - tmin;
        k = (int)(off * 16.0f + 0.5f);

        if (k <= 0)
        {
            continue;
        }

        for (i = 0; i < num; i++)
        {
            unsigned int w = v[(i * 12) + axis];

            v[(i * 12) + axis] = (w & ~0x3FFFu) | (((w & 0x3FFF) + k) & 0x3FFF);
        }
    }
}

/* Per-edge UI UV crop, expressed in 1/16-texel units (8 = half a texel).
 * Runtime tuning writes these values to recvx.ini so the next launch keeps them. */
static int pc_ui_uv_crop_values[4];
static int pc_ui_uv_crop_rot90_cw_values[4];
static int pc_ui_uv_crop_initialized;
static const char* pc_ui_uv_crop_keys[4] = {
    "ui_uv_crop_left", "ui_uv_crop_right", "ui_uv_crop_top", "ui_uv_crop_bottom"
};
static const char* pc_ui_uv_crop_rot90_cw_keys[4] = {
    "ui_uv_crop_rot90_cw_left", "ui_uv_crop_rot90_cw_right",
    "ui_uv_crop_rot90_cw_top", "ui_uv_crop_rot90_cw_bottom"
};

static int pc_ui_uv_crop_value(const char* key, int fallback)
{
    int value = pcIniGetInt(key, fallback);

    if (value < -64) value = -64; /* max four texels per edge in either direction */
    if (value > 64) value = 64;
    return value;
}

static void pc_ui_uv_crop_init(void)
{
    if (!pc_ui_uv_crop_initialized)
    {
        /* Defaults are in 1/16-texel units. Explicit recvx.ini values override them. */
        static const int automatic_defaults[4] = { -8, 16, -16, 16 };
        static const int rot90_cw_defaults[4] = { 0, 8, 16, 1 };
        int legacy = pc_ui_uv_crop_value("ui_uv_crop", -1);
        int i;

        for (i = 0; i < 4; i++)
        {
            int fallback = (legacy >= 0) ? legacy : automatic_defaults[i];
            pc_ui_uv_crop_values[i] = pc_ui_uv_crop_value(pc_ui_uv_crop_keys[i], fallback);
            pc_ui_uv_crop_rot90_cw_values[i] =
                pc_ui_uv_crop_value(pc_ui_uv_crop_rot90_cw_keys[i], rot90_cw_defaults[i]);
        }
        pc_ui_uv_crop_initialized = 1;
    }
}

static int* pc_ui_uv_crop_values_for_profile(int profile)
{
    return profile ? pc_ui_uv_crop_rot90_cw_values : pc_ui_uv_crop_values;
}

static const char* const* pc_ui_uv_crop_keys_for_profile(int profile)
{
    return profile ? pc_ui_uv_crop_rot90_cw_keys : pc_ui_uv_crop_keys;
}

static void pc_ui_uv_crop_save_edge_profile(int edge, int profile)
{
    char value[16];
    int* values = pc_ui_uv_crop_values_for_profile(profile);
    const char* const* keys = pc_ui_uv_crop_keys_for_profile(profile);

    if (edge < 0 || edge >= 4)
    {
        return;
    }

    snprintf(value, sizeof(value), "%d", values[edge]);
    pcIniSet(keys[edge], value);
}

static void pc_ui_uv_crop_amounts_from(const int values[4], int* left, int* right, int* top, int* bottom)
{
    *left = values[0];
    *right = values[1];
    *top = values[2];
    *bottom = values[3];
}

static void pc_ui_uv_crop_amounts_profile(int profile, int* left, int* right, int* top, int* bottom)
{
    pc_ui_uv_crop_init();
    pc_ui_uv_crop_amounts_from(pc_ui_uv_crop_values_for_profile(profile), left, right, top, bottom);
}

static void pc_ui_uv_crop_amounts(int* left, int* right, int* top, int* bottom)
{
    pc_ui_uv_crop_amounts_profile(0, left, right, top, bottom);
}

static void pc_ui_uv_crop_rot90_cw_amounts(int* left, int* right, int* top, int* bottom)
{
    pc_ui_uv_crop_amounts_profile(1, left, right, top, bottom);
}

/* Public PC-side controls used by the live keyboard tuner. Edge order: left, right, top, bottom. */
void pcUiUvCropGet(int* left, int* right, int* top, int* bottom)
{
    pc_ui_uv_crop_amounts(left, right, top, bottom);
}

void pcUiUvCropRot90CWGet(int* left, int* right, int* top, int* bottom)
{
    pc_ui_uv_crop_rot90_cw_amounts(left, right, top, bottom);
}

static void pc_ui_uv_crop_adjust_profile(int profile, int edge, int delta)
{
    int value;
    int* values;

    pc_ui_uv_crop_init();
    if (edge < 0 || edge >= 4)
    {
        return;
    }

    values = pc_ui_uv_crop_values_for_profile(profile);
    value = values[edge] + delta;
    if (value < -64) value = -64;
    if (value > 64) value = 64;

    if (value != values[edge])
    {
        values[edge] = value;
        pc_ui_uv_crop_save_edge_profile(edge, profile);
    }
}

void pcUiUvCropAdjust(int edge, int delta)
{
    pc_ui_uv_crop_adjust_profile(0, edge, delta);
}

void pcUiUvCropRot90CWAdjust(int edge, int delta)
{
    pc_ui_uv_crop_adjust_profile(1, edge, delta);
}

static void pc_ui_uv_crop_reset_profile(int profile, int edge)
{
    int i, first = (edge == -1) ? 0 : edge, last = (edge == -1) ? 3 : edge;
    int* values = pc_ui_uv_crop_values_for_profile(profile);

    pc_ui_uv_crop_init();
    if (first < 0 || last >= 4)
    {
        return;
    }

    for (i = first; i <= last; i++)
    {
        values[i] = 0;
        pc_ui_uv_crop_save_edge_profile(i, profile);
    }
}

void pcUiUvCropReset(int edge)
{
    pc_ui_uv_crop_reset_profile(0, edge);
}

void pcUiUvCropRot90CWReset(int edge)
{
    pc_ui_uv_crop_reset_profile(1, edge);
}

/* Identify the specific clockwise quarter-turn mapping:
 * screen X follows V in reverse, while screen Y follows U forward. */
static int pc_ui_uv_mapping_is_rot90_cw(const unsigned int* v, unsigned int num)
{
    int uv_axis_for_screen[2] = { -1, -1 };
    int reversed_for_screen[2] = { 0, 0 };
    const float* fv = (const float*)v;
    int screen_axis;
    unsigned int i;

    if (num < 2)
    {
        return 0;
    }

    for (screen_axis = 0; screen_axis < 2; screen_axis++)
    {
        int uv_axis;

        for (uv_axis = 0; uv_axis < 2; uv_axis++)
        {
            unsigned int tmin = 0x3FFFu, tmax = 0, t_at_smin = 0;
            float smin = 1e9f, smax = -1e9f;
            int reversed, valid = 1;

            for (i = 0; i < num; i++)
            {
                unsigned int t = v[(i * 12) + uv_axis] & 0x3FFFu;
                float sc = fv[(i * 12) + 8 + screen_axis];

                if (t < tmin) tmin = t;
                if (t > tmax) tmax = t;
                if (sc < smin)
                {
                    smin = sc;
                    t_at_smin = t;
                }
                if (sc > smax) smax = sc;
            }

            if (smax - smin < 1.0f || tmax <= tmin ||
                fabsf(((float)(tmax - tmin) / 16.0f) - (smax - smin)) >
                    0.1f * (smax - smin) ||
                (t_at_smin != tmin && t_at_smin != tmax))
            {
                continue;
            }

            reversed = (t_at_smin == tmax);
            for (i = 0; i < num; i++)
            {
                unsigned int t = v[(i * 12) + uv_axis] & 0x3FFFu;
                float sc = fv[(i * 12) + 8 + screen_axis];

                if ((t != tmin && t != tmax) ||
                    (t == tmin && fabsf(sc - (reversed ? smax : smin)) > 0.01f) ||
                    (t == tmax && fabsf(sc - (reversed ? smin : smax)) > 0.01f))
                {
                    valid = 0;
                    break;
                }
            }

            if (valid)
            {
                uv_axis_for_screen[screen_axis] = uv_axis;
                reversed_for_screen[screen_axis] = reversed;
                break;
            }
        }

        if (uv_axis_for_screen[screen_axis] < 0)
        {
            return 0;
        }
    }

    return uv_axis_for_screen[0] == 1 && reversed_for_screen[0] &&
           uv_axis_for_screen[1] == 0 && !reversed_for_screen[1];
}

/* Apply left/right or top/bottom crop to whichever UV component actually follows that screen
 * axis. This handles normal, mirrored and 90-degree-rotated atlas mappings while keeping the
 * original safety checks for scaled, non-axis-aligned, and degenerate geometry. */
static int pc_crop_uv_edges(unsigned int* v, unsigned int num, const int inset[4])
{
    int screen_axis, changed = 0;
    unsigned int i;

    if (num < 2)
    {
        return 0;
    }

    for (screen_axis = 0; screen_axis < 2; screen_axis++)
    {
        int low_inset = inset[screen_axis == 0 ? 0 : 2];
        int high_inset = inset[screen_axis == 0 ? 1 : 3];
        int found = 0, chosen_uv_axis = 0, chosen_reversed = 0;
        unsigned int chosen_tmin = 0, chosen_tmax = 0;
        float chosen_smin = 0.0f, chosen_smax = 0.0f;
        int uv_axis;

        if (low_inset == 0 && high_inset == 0)
        {
            continue;
        }

        /* U and V are candidates; choose the one whose UV coordinate follows X or Y. */
        for (uv_axis = 0; uv_axis < 2; uv_axis++)
        {
            unsigned int tmin = 0x3FFFu, tmax = 0, t_at_smin = 0;
            float smin = 1e9f, smax = -1e9f;
            int reason = 0, reversed = 0, valid = 1;
            int adjusted_tmin, adjusted_tmax;
            unsigned int bad_index = 0, bad_t = 0;
            float bad_sc = 0.0f;

            for (i = 0; i < num; i++)
            {
                unsigned int t = v[(i * 12) + uv_axis] & 0x3FFFu;
                float sc = ((float*)v)[(i * 12) + 8 + screen_axis];

                if (t < tmin) tmin = t;
                if (t > tmax) tmax = t;
                if (sc < smin)
                {
                    smin = sc;
                    t_at_smin = t;
                }
                if (sc > smax) smax = sc;
            }

            if (smax - smin < 1.0f)
            {
                reason = 1; /* degenerate screen span */
            }
            else if (tmax <= tmin)
            {
                reason = 4; /* degenerate UV range */
            }
            else if (fabsf(((float)(tmax - tmin) / 16.0f) - (smax - smin)) > 0.1f * (smax - smin))
            {
                reason = 2; /* this UV component follows the other screen axis or has a different scale */
            }
            else if (t_at_smin != tmin && t_at_smin != tmax)
            {
                reason = 3; /* orientation cannot be represented by a simple axis-aligned mapping */
            }
            else
            {
                reversed = (t_at_smin == tmax);

                /* Select the safe UV range after applying insets at the low/high screen edges.
                 * On a mirrored mapping, those screen edges correspond to the opposite UV ends. */
                adjusted_tmin = (int)tmin + (reversed ? high_inset : low_inset);
                adjusted_tmax = (int)tmax - (reversed ? low_inset : high_inset);
                if (adjusted_tmin < 0 || adjusted_tmin > 0x3FFF ||
                    adjusted_tmax < 0 || adjusted_tmax > 0x3FFF)
                {
                    reason = 5; /* inset would overflow or underflow UV */
                }
                else if (adjusted_tmin >= adjusted_tmax)
                {
                    reason = 6; /* requested crop would collapse the UV range */
                }
                else
                {
                    /* Validate the whole axis before changing anything. All vertices carrying
                     * an endpoint UV must sit on its corresponding screen-space edge. */
                    for (i = 0; i < num; i++)
                    {
                        unsigned int t = v[(i * 12) + uv_axis] & 0x3FFFu;
                        float sc = ((float*)v)[(i * 12) + 8 + screen_axis];

                        if (t != tmin && t != tmax)
                        {
                            valid = 0;
                            reason = 7; /* UV is not on either edge */
                        }
                        else if (t == tmin && fabsf(sc - (reversed ? smax : smin)) > 0.01f)
                        {
                            valid = 0;
                            reason = 8; /* UV endpoint does not match its screen edge */
                        }
                        else if (t == tmax && fabsf(sc - (reversed ? smin : smax)) > 0.01f)
                        {
                            valid = 0;
                            reason = 8;
                        }

                        if (!valid)
                        {
                            bad_index = i;
                            bad_t = t;
                            bad_sc = sc;
                            break;
                        }
                    }
                }
            }

            if (reason == 0 && valid)
            {
                chosen_uv_axis = uv_axis;
                chosen_reversed = reversed;
                chosen_tmin = tmin;
                chosen_tmax = tmax;
                chosen_smin = smin;
                chosen_smax = smax;
                found = 1;
                break;
            }

        }

        if (!found)
        {
            continue;
        }

        /* Apply the named screen-edge inset to the matching UV component and direction. */
        for (i = 0; i < num; i++)
        {
            unsigned int* word = &v[(i * 12) + chosen_uv_axis];
            unsigned int w = *word;
            unsigned int t = w & 0x3FFFu;
            int adjusted = (int)t;

            if (!chosen_reversed)
            {
                if (t == chosen_tmin)
                {
                    adjusted += low_inset;
                }
                else if (t == chosen_tmax)
                {
                    adjusted -= high_inset;
                }
            }
            else
            {
                if (t == chosen_tmax)
                {
                    adjusted -= low_inset;
                }
                else if (t == chosen_tmin)
                {
                    adjusted += high_inset;
                }
            }

            t = (unsigned int)adjusted;
            *word = (w & ~0x3FFFu) | (t & 0x3FFFu);
        }

        changed++;
    }

    return changed;
}

int pcWide2DUnclipped;   /* widescreen: 2D drawn into the side areas on purpose (backgrounds) */
int pcWide2DFullWidth;   /* widescreen: full-width 2D that covers the picture (screen filters) */
int pcWide2DKeep43;      /* widescreen: full screen 2D that is 4:3 art although translucent */
int pcWide2DTrimRight;   /* widescreen: leave out the piece's last texel column (a screen-edge border) */
int pcWide2DStretch;     /* widescreen: 2D that is the 3D view itself (drawn back from a render texture) */

/* widescreen: clip a 2D rectangle (sprite, or quad with two distinct x values) to the 4:3
 * frame, 1728..2368 in GS coordinates. Vertices are 3 qwords: ST/Q (or UV), RGBA, XYZ. */
static void pc_wide_clip(unsigned int* v, unsigned int num, int uv)
{
    static unsigned int orig[64 * 12];
    float xa = ((float*)v)[8], xb = xa;
    unsigned int i, j, k;

    for (i = 0; i < num; i++)
    {
        float x = ((float*)v)[(i * 12) + 8];

        if ((x != xa) && (xb == xa))
        {
            xb = x;
        }
        else if ((x != xa) && (x != xb))
        {
            return;   /* not a rectangle: leave it */
        }
    }

    memcpy(orig, v, num * 48);   /* interpolate from the unclipped corners */

    for (i = 0; i < num; i++)
    {
        unsigned int* a = &v[i * 12];
        float x = ((float*)a)[8];
        float e = (x < 1728.0f) ? 1728.0f : (x > 2368.0f) ? 2368.0f : x;
        unsigned int* b = NULL;
        float t;

        if (e == x)
        {
            continue;
        }

        /* the vertex across the rectangle on the same row (a sprite has just two) */
        for (j = 0; j < num; j++)
        {
            unsigned int* c = &orig[j * 12];

            if ((((float*)c)[8] != x) && ((num == 2) || (((float*)c)[9] == ((float*)a)[9])))
            {
                b = c;
                break;
            }
        }

        if (b == NULL)
        {
            continue;
        }

        t = (e - x) / (((float*)b)[8] - x);

        /* a sprite's other vertex is the opposite corner: only the horizontal texture
         * coordinate follows x there */
        if (uv)
        {
            for (k = 0; k < ((num > 2) ? 2u : 1u); k++)
            {
                a[k] = (unsigned int)((int)a[k] + (int)(t * ((int)b[k] - (int)a[k])));
            }
        }
        else
        {
            for (k = 0; k < ((num > 2) ? 3u : 1u); k++)
            {
                ((float*)a)[k] += t * (((float*)b)[k] - ((float*)a)[k]);
            }
        }

        /* sprites are flat shaded from their last vertex; the first one's color is unset */
        for (k = 4; (num > 2) && (k < 8); k++)
        {
            a[k] = (unsigned int)((int)a[k] + (int)(t * ((int)b[k] - (int)a[k]) + 0.5f));   /* vertice sintetico del recorte propio: no existe en la GS, se aproxima el valor exacto (natural) */
        }

        ((float*)a)[8] = e;

        if (num > 2)
        {
            ((float*)a)[10] += t * (((float*)b)[10] - ((float*)a)[10]);
        }
    }
}

/* CLAUDE-NOTE[N9]: dos descartes silenciosos al principio: (1) textured && Ps2_now_tex == NULL -> return sin log;
 * (2) mas abajo, en la ruta nativa 2D: count<1 || count>16384 -> return. Anadir contadores/log aqui.
 * CLAUDE-NOTE[N8]: la ruta nativa (clip_3d_on == 0) dibuja INMEDIATAMENTE en orden de llamada. El original metia el 2D en
 * el OT (Ps2AddOT, 4096 buckets por z) y lo pintaba en Ps2DrawOTag; esa prioridad se perdio. Los 2D con blend, ademas, se
 * difieren al flush (ver pc_renderer.c queue_transparent). Con clip_3d_on != 0 se cae al codigo PS2 original, que termina
 * en Ps2AddOT (inerte en PC): esos primitivos no se dibujan.
 * CLAUDE-NOTE[N11]: en la rama no-sprite de la ruta nativa, vertices[i].q no se inicializa (malloc sin memset). */
/* pri de njDraw*2D (<= -1; -1 = mas cerca): la GS lo convertia con sz = a + b/pri, equivalente a un Z de vista = -pri */
static float pcPriToViewZ(float pri)
{
    float z = -pri;
    if (!(z >= 1.0f)) z = 1.0f;
    if (z > 65535.0f) z = 65535.0f;
    return z;
}

static void pcPrimLog(const char* what, unsigned long prim, unsigned int num)
{
    static unsigned calls, drops;
    /* no "long long" / extern pcLog here: "long" is a 64-bit macro in game code under MSVC */
    unsigned int hi = (unsigned int)(prim >> 32), lo = (unsigned int)prim;
    if (what == NULL) { if ((calls++ % 300) < 6) pcLog("Ps2AddPrim: prim=%08x%08x num=%u tex=%s", hi, lo, num, Ps2_now_tex ? "set" : "NULL"); }
    else if (drops++ < 40) pcLog("Ps2AddPrim DROP (%s): prim=%08x%08x num=%u", what, hi, lo, num);
}

/* Diagnostico (solo en los 2 primeros frames de la rafaga F8): cada llamada de dibujo 3D/2D con su PRIM decodificado y los
 * primeros vertices CRUDOS tal como los dejo el juego (3 qwords por vertice: S,T,Q,flags / RGBA / X,Y,Z,F). Sirve para
 * saber QUIEN emite una primitiva (los vertices y la PRIM delatan al llamador) y comprobar el tipo (6 = sprite: 2 vertices por
 * rectangulo), el numero de vertices, la textura y la niebla (F, ultima palabra) que llega al renderer. */
static void pcPrimDiag(const char* who, unsigned long prim, const void* dp, unsigned int num)
{
    extern int pcRendererBurstDiag(void);
    static unsigned int lines;
    const float* v = (const float*)dp;
    unsigned int i, n = num < 4 ? num : 4;

    if (!pcRendererBurstDiag()) { lines = 0; return; }
    if (lines == 0) {
        extern void pcFogDiag(void);
        pcFogDiag();
    }
    if (lines++ >= 220 || dp == NULL)
        return;
    pcLog("P3D %s: prim=%08x%08x type=%u TME=%u ABE=%u FGE=%u FST=%u IIP=%u num=%u tex=%s", who,
          (unsigned int)(prim >> 32), (unsigned int)prim, PC_PRIM_TOPOLOGY(prim), (unsigned int)((prim >> 51) & 1u),
          (unsigned int)((prim >> 53) & 1u), (unsigned int)((prim >> 52) & 1u), (unsigned int)((prim >> 55) & 1u),
          (unsigned int)((prim >> 50) & 1u), num, Ps2_now_tex ? "set" : "NULL");
    for (i = 0; i < n; i++, v += 12) {
        unsigned int c[4];
        memcpy(c, v + 4, sizeof(c));
        pcLog("    v%u: stq=(%.4f %.4f %.4f) col=%08x %08x %08x %08x  xyz=(%.1f %.1f %.1f) F=%.1f", i, v[0], v[1], v[2],
              c[0], c[1], c[2], c[3], v[8], v[9], v[10], v[11]);
    }
}

/* Coordenada U o V de una primitiva con PRIM.FST=1 (registro UV de la GS): 14 bits, punto fijo 12.4 -> texels. La palabra llega
 * como float en el buffer de vertices; se reinterpreta su patron de bits. */
static float pcFixedUv(float word)
{
    unsigned int bits;
    memcpy(&bits, &word, sizeof(bits));
    return (float)(bits & 0x3FFFu) / 16.0f;
}

void Ps2AddPrim2D(unsigned long prim, void* dp, unsigned int num)
{
    unsigned int i;             
    float x;                   
    float y;                    
    float wide_2d = 1.0f;
    static unsigned int wide_buf[64 * 12];

    pcPrimLog(NULL, prim, num);
    
    if ((prim & PC_PRIM_TME))
    { 
        if (Ps2_now_tex == NULL) 
        { 
            pcPrimLog("textured, no current texture", prim, num);
            return;
        }
        
        if (((prim & PC_PRIM_ABE)) && (Ps2_use_pt_flag != 0)) 
        { 
            prim &= ~PC_PRIM_ABE; 
        }
        
        if ((!(prim & PC_PRIM_ABE)) && (((TIM2_PICTUREHEADER_EX*)(Ps2_now_tex->texinfo.texsurface.pSurface))->TpFlag != 0)) 
        { 
            Ps2_tex_load_tp_cancel = 1; 
            
            Ps2TexLoad(Ps2_now_tex);
            
            Ps2_tex_load_tp_cancel = 0; 
        }
    } 
    
    if ((prim & PC_PRIM_TME) && (prim & PC_PRIM_FST) && (num <= 64))
    {
        memcpy(wide_buf, dp, num * 48);
        dp = wide_buf;
        pc_snap_uv((unsigned int*)wide_buf, num);

        {
            static int crop_logged;
            int crop[4], crop_rot90_cw[4];
            int is_rot90_cw;

            pc_ui_uv_crop_amounts(&crop[0], &crop[1], &crop[2], &crop[3]);
            pc_ui_uv_crop_rot90_cw_amounts(&crop_rot90_cw[0], &crop_rot90_cw[1],
                                           &crop_rot90_cw[2], &crop_rot90_cw[3]);
            is_rot90_cw = pc_ui_uv_mapping_is_rot90_cw((const unsigned int*)wide_buf, num);
            if (is_rot90_cw)
            {
                memcpy(crop, crop_rot90_cw, sizeof(crop));
            }

            if ((crop[0] || crop[1] || crop[2] || crop[3]) &&
                pc_crop_uv_edges((unsigned int*)wide_buf, num, crop) > 0 && !crop_logged)
            {
                pcLog("UI UV edge crop active profile=%s (1/16 texel units): left=%d right=%d top=%d bottom=%d",
                      is_rot90_cw ? "rot90_cw" : "standard",
                      crop[0], crop[1], crop[2], crop[3]);
                crop_logged = 1;
            }
        }

        if (pcWide2DTrimRight)
        {
            unsigned int i2, umax = 0;

            for (i2 = 0; i2 < num; i2++)
            {
                if ((wide_buf[i2 * 12] & 0x3FFF) > umax) umax = wide_buf[i2 * 12] & 0x3FFF;
            }

            for (i2 = 0; i2 < num; i2++)
            {
                if ((wide_buf[i2 * 12] & 0x3FFF) == umax) wide_buf[i2 * 12] -= 16;
            }
        }
    }

    /* widescreen: 2D keeps its 4:3 layout in the middle of the stretched frame. Primitives
     * that cover the whole width stay full width when they are untextured (fades, flashes) or
     * translucent full screen overlays (room darkness filters); opaque full screen pictures
     * (logos, title) are 4:3 art and get pillarboxed like the rest. */
    {
        extern float pcVideoWideScale(void);

        wide_2d = pcVideoWideScale();

        if (wide_2d < 1.0f)
        {
            float x0 = 4096.0f, x1 = -4096.0f, y0 = 4096.0f, y1 = -4096.0f;

            for (i = 0; i < num; i++)
            {
                float vx = ((float*)dp)[(i * 12) + 8];
                float vy = ((float*)dp)[(i * 12) + 9];

                if (vx < x0) x0 = vx;
                if (vx > x1) x1 = vx;
                if (vy < y0) y0 = vy;
                if (vy > y1) y1 = vy;
            }

            if (pcWide2DStretch)
            {
                /* the room drawn into a texture already has the wider view, squeezed into the 640
                 * wide frame like the screen itself: stretched back, whatever its outline */
                wide_2d = 1.0f;
            }
            else if ((x0 <= 1728.0f + 1.0f) && (x1 >= 2368.0f - 1.0f) && !pcWide2DKeep43)
            {
                if (pcWide2DFullWidth || !(prim & PC_PRIM_TME) ||
                    ((prim & PC_PRIM_ABE) && (y0 <= 1808.0f + 1.0f) && (y1 >= 2288.0f - 1.0f)))
                {
                    wide_2d = 1.0f;
                }
            }

            /* 2D beyond the 4:3 edges (panels parked off screen or sliding in) was hidden by
             * the screen edge; squeezed, it would show in the side areas. Backgrounds the
             * game code extends into those areas on purpose set pcWide2DUnclipped. */
            /* Pieces reaching at most 32 pixels past an edge belong to panels drawn running off
             * the edge of the screen: they are drawn whole, so the panel continues instead of
             * ending in a hard cut. */
            if ((wide_2d < 1.0f) && !pcWide2DUnclipped && (x0 >= 1728.0f - 32.0f) && (x1 <= 2368.0f + 32.0f))
            {
            }
            else if ((wide_2d < 1.0f) && !pcWide2DUnclipped)
            {
                if ((x1 <= 1728.0f) || (x0 >= 2368.0f))
                {
                    pcPrimLog("wide2D off-screen", prim, num);
                    return;
                }

                if (((x0 < 1728.0f) || (x1 > 2368.0f)) && (num <= 64))
                {
                    if (dp != wide_buf)
                    {
                        memcpy(wide_buf, dp, num * 48);
                        dp = wide_buf;
                    }

                    pc_wide_clip((unsigned int*)wide_buf, num, (prim & PC_PRIM_FST) != 0);
                }
            }
        }
    }

    /* Native 2D path: consume Ninja's screen-space vertices directly. No VU1 packet,
       GIF tag, scratchpad DMA, GS register write or GS ordering-table submission. */
    {
        PcNativeVertex* vertices;
        PcNativeVertex sprite[6];
        PcNativeDrawState state;
        unsigned int topology = PC_PRIM_TOPOLOGY(prim);
        unsigned int i;
        int textured = (prim & PC_PRIM_TME) != 0;
        int transparent = (prim & PC_PRIM_ABE) != 0;
        int use_uv = (prim & PC_PRIM_FST) != 0;
        int count = (int)num;
        int owned = 0;
        float texture_width = 1.0f, texture_height = 1.0f;
        float use_last_pri = ((const float*)dp)[(size_t)(count > 0 ? count - 1 : 0) * 12u + 10u];

        pcPrimDiag("Ps2AddPrim(2D)", prim, dp, num);
        if (count < 1 || count > 16384)
            { pcPrimLog("bad count", prim, num); return; }
        if (textured && (Ps2_now_tex == NULL))
            { pcPrimLog("native textured, no texture", prim, num); return; }

        if ((topology == 6u) && (count >= 2))
        {
            const float* a = (const float*)dp;
            const float* b = a + 12;
            float x0 = a[8], y0 = a[9], x1 = b[8], y1 = b[9];
            float u0, v0, u1, v1;
            const float* color = b + 4;
            float cf[4];
            float pri_depth = pcRendererDepthFromViewZ(pcPriToViewZ(b[10]));
            unsigned int ci[4];

            /* CLAUDE-FIX: njDraw*2D guarda el color como ENTEROS (0..255, alfa 0..128) en esas palabras, no como float.
             * Leerlos como float daba ~0 (UI negra/transparente). Las UV (ST) van normalizadas como u/256 (ITOF12). */
            pcRendererCurrentTextureSize(&texture_width, &texture_height);
            memcpy(ci, color, sizeof(ci));
            cf[0] = (float)(ci[0] & 0xFFu); cf[1] = (float)(ci[1] & 0xFFu);
            cf[2] = (float)(ci[2] & 0xFFu); cf[3] = (float)(ci[3] & 0xFFu);
            if (use_uv) {
                /* PRIM.FST=1: palabras 0 y 1 = U,V en punto fijo 12.4 (texels * 16), no ST flotantes */
                u0 = pcFixedUv(a[0]); v0 = pcFixedUv(a[1]); u1 = pcFixedUv(b[0]); v1 = pcFixedUv(b[1]);
            } else {
                u0 = a[0] * texture_width;  v0 = a[1] * texture_height;
                u1 = b[0] * texture_width;  v1 = b[1] * texture_height;
            }
            color = cf;

            if (wide_2d < 1.0f) {
                x0 = 2048.0f + (x0 - 2048.0f) * wide_2d;
                x1 = 2048.0f + (x1 - 2048.0f) * wide_2d;
            }
            memset(sprite, 0, sizeof(sprite));
            for (i = 0; i < 6; i++) {
                static const unsigned char corner_map[6] = { 0, 1, 2, 0, 2, 3 };
                unsigned int corner = corner_map[i];
                float x, y, u, v;
                if (corner == 0) { x=x0; y=y0; u=u0; v=v0; }
                else if (corner == 1) { x=x1; y=y0; u=u1; v=v0; }
                else if (corner == 2) { x=x1; y=y1; u=u1; v=v1; }
                else { x=x0; y=y1; u=u0; v=v1; }
                sprite[i].x = x - 1728.0f;
                sprite[i].y = y - 1808.0f;
                sprite[i].z = pri_depth;
                sprite[i].r = fminf(fmaxf(color[0] / 255.0f, 0.0f), 1.0f);
                sprite[i].g = fminf(fmaxf(color[1] / 255.0f, 0.0f), 1.0f);
                sprite[i].b = fminf(fmaxf(color[2] / 255.0f, 0.0f), 1.0f);
                sprite[i].a = fminf(fmaxf(color[3] / 128.0f, 0.0f), 1.0f);
                sprite[i].u = u;
                sprite[i].v = v;
                sprite[i].q = 1.0f;
                sprite[i].fog = 1.0f;
            }
            vertices = sprite;
            count = 6;
            topology = 3u;
        }
        else
        {
            const float* source = (const float*)dp;
            vertices = (PcNativeVertex*)malloc((size_t)count * sizeof(*vertices));
            if (vertices == NULL)
                return;
            owned = 1;

            pcRendererCurrentTextureSize(&texture_width, &texture_height);
            for (i = 0; i < (unsigned int)count; i++, source += 12) {
                float x = source[8];
                float y = source[9];
                unsigned int ci[4];
                memcpy(ci, source + 4, sizeof(ci));
                if (wide_2d < 1.0f)
                    x = 2048.0f + (x - 2048.0f) * wide_2d;
                vertices[i].x = x - 1728.0f;
                vertices[i].y = y - 1808.0f;
                vertices[i].z = pcRendererDepthFromViewZ(pcPriToViewZ(source[10]));
                vertices[i].r = fminf(fmaxf((float)(ci[0] & 0xFFu) / 255.0f, 0.0f), 1.0f);
                vertices[i].g = fminf(fmaxf((float)(ci[1] & 0xFFu) / 255.0f, 0.0f), 1.0f);
                vertices[i].b = fminf(fmaxf((float)(ci[2] & 0xFFu) / 255.0f, 0.0f), 1.0f);
                vertices[i].a = fminf(fmaxf((float)(ci[3] & 0xFFu) / 128.0f, 0.0f), 1.0f);
                if (use_uv) {
                    vertices[i].u = pcFixedUv(source[0]);
                    vertices[i].v = pcFixedUv(source[1]);
                } else {
                    vertices[i].u = source[0] * texture_width;
                    vertices[i].v = source[1] * texture_height;
                }
                vertices[i].fog = 1.0f;
                vertices[i].q = 1.0f;
            }
        }

        memset(&state, 0, sizeof(state));
        state.texture = pcRendererCurrentTexture();
        pcRendererCurrentTextureSize(&texture_width, &texture_height);
        state.texture_width = texture_width;
        state.texture_height = texture_height;
        pcRendererCurrentTextureCombine(&state.texture_function, &state.texture_alpha);
        state.textured = textured && (state.texture != 0);
        state.blend = transparent;
        /* GS: 2D con ZTE=1/GEQUAL y Z = f(pri) (mismo espacio que el 3D); la prioridad decide quien queda delante. */
        state.depth_test = 1;
        state.depth_write = 1;
        state.alpha_test = 1;
        state.alpha_ref = 0.004f;
        state.linear_filter = 1;
        state.screen_space = 1;
        state.ui_layer = 1;
        state.cull_backfaces = 0;
        {
            /* Ps2AddOT usa el sz del ultimo vertice como clave del OT */
            float zv = pcPriToViewZ(use_last_pri);
            state.ot_z_valid = 1;
            state.ot_z = 65537.0f / zv + 1.0f;
            /* el 2D opaco tambien pasaba por el OT: fondo y rejilla del mapa comparten Z16 y solo los separa el orden */
            state.ot_queue = 1;
        }

        switch (topology) {
        case 0: pcRendererDrawPoints(vertices, count, &state); break;
        case 1: pcRendererDrawLines(vertices, count, &state); break;
        case 2: pcRendererDrawLineStrip(vertices, count, &state); break;
        case 3: pcRendererDrawTriangles(vertices, count, &state); break;
        case 4: pcRendererDrawTriangleStrip(vertices, count, &state); break;
        case 5:
            if (count >= 3) {
                PcNativeVertex* fan = (PcNativeVertex*)malloc((size_t)(count - 2) * 3u * sizeof(*fan));
                if (fan != NULL) {
                    unsigned int t, n = 0;
                    for (t = 1; t + 1 < (unsigned int)count; t++) {
                        fan[n++] = vertices[0]; fan[n++] = vertices[t]; fan[n++] = vertices[t + 1];
                    }
                    pcRendererDrawTriangles(fan, (int)n, &state);
                    free(fan);
                }
            }
            break;
        default:
            pcRendererDrawTriangles(vertices, count, &state);
            break;
        }

        if (owned)
            free(vertices);
        return;
    }
}


/* Etapa final del pipeline 3D (Ps2AddPrim3D*): lo que la GS recibia tras el empaquetador VU0.
 *
 * El juego (vu1DrawTriangleStrip*, njCnk*) entrega vertices ya proyectados a coordenadas de la GS: ST/Q, color y XYZ + fog,
 * con el "kick" por vertice en la cuarta palabra (32768 = el vertice no cierra triangulo: cara trasera o triangulo que el
 * recorte reemplazo por otros). pcPs2Prim3DCore es el empaquetador original: calcula el Z de la GS, el XY en 12.4, los
 * flags de recorte (ADC) y la Z media. Aqui se dibuja exactamente ese resultado: nada se reproyecta ni se reordena.
 *
 * Orden de los primitivos con ABE (Ps2AddOT):
 *   PC_OT_AVERAGE      Z media de los vertices
 *   PC_OT_FIXED        Z dada por quien llama (Ps2AddPrimPrio)
 *   PC_OT_PER_TRIANGLE Ps2AddPrim3DExI: un item por triangulo con Z minima/media/maxima segun ViewType
 *   PC_OT_IMMEDIATE    sin OT: la GS lo dibujaba al momento */
typedef enum { PC_OT_IMMEDIATE, PC_OT_AVERAGE, PC_OT_FIXED, PC_OT_PER_TRIANGLE } PcOtPolicy;

typedef struct {
    int floating_color;     /* RGBA en float (0..255) en lugar de enteros */
    int shadow_mask;        /* contexto 2 (volumen de sombra): sin prueba de alfa ni niebla, siempre inmediato */
    PcOtPolicy ot;
    float ot_z;             /* PC_OT_FIXED */
} PcPrim3D;


/* Ps2AddOT: con Ps2_ice_flag las texturas de hielo se ordenan con Z >> 12 en lugar de Z >> 4. */
static int pcOtCoarse(void)
{
    unsigned int id;
    if (Ps2_ice_flag == 0 || Ps2_now_tex == NULL)
        return 0;
    id = Ps2_now_tex->globalIndex & 0xFFFFF;
    return ((id >= 26510) && (id <= 26614)) || (id == 107170);
}

/* Ensambla los vertices en una lista respetando el kick (ADC) de la GS: el primitivo que cierra un vertice con kick no se
 * dibuja, pero el vertice sigue contando para los siguientes. Devuelve el numero de vertices de salida. */
static unsigned int pcAssemblePrepared(const PcNativeVertex* v, const unsigned char* kick, unsigned int count,
                                       unsigned int topology, PcNativeVertex* out)
{
    unsigned int i, n = 0;
    switch (topology) {
    case 0: /* puntos */
        for (i = 0; i < count; i++) if (!kick[i]) out[n++] = v[i];
        break;
    case 1: /* lineas */
        for (i = 1; i < count; i += 2) if (!kick[i]) { out[n++] = v[i - 1]; out[n++] = v[i]; }
        break;
    case 2: /* tira de lineas */
        for (i = 1; i < count; i++) if (!kick[i]) { out[n++] = v[i - 1]; out[n++] = v[i]; }
        break;
    case 4: /* tira de triangulos */
        for (i = 2; i < count; i++) if (!kick[i]) { out[n++] = v[i - 2]; out[n++] = v[i - 1]; out[n++] = v[i]; }
        break;
    case 5: /* abanico */
        for (i = 2; i < count; i++) if (!kick[i]) { out[n++] = v[0]; out[n++] = v[i - 1]; out[n++] = v[i]; }
        break;
    case 6: /* sprite: 2 vertices = esquina y esquina opuesta; rectangulo alineado a los ejes (2 triangulos) */
        for (i = 1; i < count; i += 2) {
            PcNativeVertex c0, c1, c2, c3;
            if (kick[i])
                continue;
            /* La GS pinta el sprite con el color, la Z y la niebla del SEGUNDO vertice (sin gradiente); las coordenadas de
             * textura van de la primera esquina a la opuesta. */
            c0 = v[i]; c1 = v[i]; c2 = v[i]; c3 = v[i];
            c0.x = v[i - 1].x; c0.y = v[i - 1].y; c0.u = v[i - 1].u; c0.v = v[i - 1].v; c0.q = v[i - 1].q;
            c1.x = v[i].x;     c1.y = v[i - 1].y; c1.u = v[i].u;     c1.v = v[i - 1].v;
            c2.x = v[i].x;     c2.y = v[i].y;     c2.u = v[i].u;     c2.v = v[i].v;
            c3.x = v[i - 1].x; c3.y = v[i].y;     c3.u = v[i - 1].u; c3.v = v[i].v;     c3.q = v[i - 1].q;
            out[n++] = c0; out[n++] = c1; out[n++] = c2;
            out[n++] = c0; out[n++] = c2; out[n++] = c3;
        }
        break;
    default: /* lista de triangulos */
        for (i = 2; i < count; i += 3) if (!kick[i]) { out[n++] = v[i - 2]; out[n++] = v[i - 1]; out[n++] = v[i]; }
        break;
    }
    return n;
}

/* Buffers de trabajo de los envios nativos: crecen una vez y se reutilizan (antes, 4-5 malloc/free POR TIRA, ~550 tiras por
 * frame). El juego envia desde un solo hilo y estos envios no se anidan entre si (cada ranura la usa un solo nivel). */
static void* pcScratch(int slot, size_t bytes)
{
    static void* buf[6];
    static size_t cap[6];
    if (bytes > cap[slot])
    {
        size_t want = bytes * 2u + 256u;
        void* grown = realloc(buf[slot], want);
        if (grown == NULL)
            return NULL;
        buf[slot] = grown;
        cap[slot] = want;
    }
    return buf[slot];
}

/* FTOI0 de la VU1 (truncar hacia cero) tras fijar al rango del campo de 8 bits: asi llegan a la GS el color RGBA y la F de
 * la niebla. El float de la VU se queda en el entero de abajo; no se redondea al mas cercano. */
static float pcFtoi0Sat(float x)
{
    return floorf(fminf(fmaxf(x, 0.0f), 255.0f));
}

/* Cola final: vertices ya en pantalla (+ kick y Z de la GS por vertice) -> renderer nativo.
 * La usan el empaquetador del EE (pcSubmitPrepared3D) y pcSubmitStrip3D. */
static void pcSubmitVertices(unsigned long prim, const PcNativeVertex* vertices, const unsigned char* kick, const float* gs_z,
                             unsigned int count, const PcPrim3D* mode, float average_z, float tex_width, float tex_height,
                             unsigned int texture, int screen_space)
{
    PcNativeVertex* assembled;
    PcNativeDrawState state;
    unsigned int i, n, topology = PC_PRIM_TOPOLOGY(prim);
    int textured = (prim & PC_PRIM_TME) != 0;
    int blend = (prim & PC_PRIM_ABE) != 0;

    assembled = (PcNativeVertex*)pcScratch(0, (size_t)count * 3u * sizeof(*assembled));
    if (assembled == NULL)
        return;

    memset(&state, 0, sizeof(state));
    state.texture = texture;
    state.texture_width = tex_width;
    state.texture_height = tex_height;
    pcRendererCurrentTextureCombine(&state.texture_function, &state.texture_alpha);
    state.textured = textured;
    state.blend = blend;
    state.immediate = blend && (mode->shadow_mask || mode->ot == PC_OT_IMMEDIATE);
    state.depth_test = 1;
    state.depth_write = !blend;
    state.alpha_test = !mode->shadow_mask;   /* TEST_1: ATE=1 GREATER 0; TEST_2 del volumen de sombra: ATE=0 */
    state.alpha_ref = 0.004f;
    state.linear_filter = 1;
    state.screen_space = screen_space;
    if (screen_space == 3 || screen_space == 4) {   /* vertices crudos: el vertex shader necesita las constantes de Z de la GS */
        state.zbuff_a = Ps2_zbuff_a;
        state.zbuff_b = Ps2_zbuff_b;
    }
    state.fog = !mode->shadow_mask && (prim & PC_PRIM_FGE) != 0;
    /* espacio 1: el fragmento divide S/Q a mano; espacio 2 (homogeneo): la GPU ya interpola UV con correccion de perspectiva */
    state.texture_perspective = (screen_space == 1 || screen_space == 4) && (prim & PC_PRIM_FST) == 0;
    state.cull_backfaces = 0;
    if (blend && !state.immediate) {
        state.ot_z_valid = 1;
        state.ot_z = (mode->ot == PC_OT_FIXED) ? mode->ot_z : average_z;
        state.ot_coarse = pcOtCoarse();
    }

    if (mode->ot == PC_OT_PER_TRIANGLE && blend && topology == 4 && count > 3 && ViewType < 3 && ((count * 3) - 6) < 167) {
        /* Ps2AddPrim3DExI: cada triangulo de la tira es un item del OT con su propia Z */
        for (i = 2; i < count; i++) {
            const float z0 = gs_z[i - 2], z1 = gs_z[i - 1], z2 = gs_z[i];
            if (kick[i])
                continue;
            if (ViewType == 0)
                state.ot_z = fminf(fminf(z0, z1), z2);
            else if (ViewType == 1)
                state.ot_z = 0.33333334f * (z0 + z1 + z2);
            else
                state.ot_z = fmaxf(fmaxf(z0, z1), z2);
            assembled[0] = vertices[i - 2]; assembled[1] = vertices[i - 1]; assembled[2] = vertices[i];
            pcRendererDrawTriangles(assembled, 3, &state);
        }
        goto done;
    }

    if (topology == 4) {
        for (i = 2; i < count && !kick[i]; i++) { }
        if (i == count) {                    /* ningun kick: la tira se dibuja tal cual */
            pcRendererDrawTriangleStrip(vertices, (int)count, &state);
            goto done;
        }
    }

    n = pcAssemblePrepared(vertices, kick, count, topology, assembled);
    if (n == 0)
        goto done;
    switch (topology) {
    case 0: pcRendererDrawPoints(assembled, (int)n, &state); break;
    case 1:
    case 2: pcRendererDrawLines(assembled, (int)n, &state); break;
    default: pcRendererDrawTriangles(assembled, (int)n, &state); break;
    }

done:
    return;
}

static void pcSubmitPrepared3DGpu(unsigned long prim, const void* data, unsigned int count, const PcPrim3D* mode);

static void pcSubmitPrepared3D(unsigned long prim, const void* data, unsigned int count, const PcPrim3D* mode)
{
    const float* source = (const float*)data;
    uint32_t* packed;
    PcNativeVertex* vertices;
    unsigned char* kick;
    float* gs_z;
    unsigned int i;
    float tex_width = 1.0f, tex_height = 1.0f, average_z;
    int textured = (prim & PC_PRIM_TME) != 0;
    unsigned int texture = pcRendererCurrentTexture();

    if (source == NULL || count < 1 || count > 16384)
        return;
    if (textured && texture == 0)
        return;
    /* Conversion en el vertex shader. Los sprites (topologia 6) siguen aqui: copian Z/color/niebla del segundo
     * vertice y la Q del primero, y con vertices crudos la Z sale de la Q. */
    if (PC_PRIM_TOPOLOGY(prim) != 6u) {
        pcSubmitPrepared3DGpu(prim, data, count, mode);
        return;
    }

    packed = (uint32_t*)pcScratch(1, ((size_t)count * 12u + 4u) * sizeof(*packed));
    vertices = (PcNativeVertex*)pcScratch(2, (size_t)count * sizeof(*vertices));
    kick = (unsigned char*)pcScratch(3, count);
    gs_z = (float*)pcScratch(4, (size_t)count * sizeof(*gs_z));
    if (packed == NULL || vertices == NULL || kick == NULL || gs_z == NULL)
        goto done;

    pcPs2Prim3DCore(data, packed, count, mode->floating_color);
    average_z = ((const float*)packed)[(size_t)count * 12u + 3u];
    pcRendererCurrentTextureSize(&tex_width, &tex_height);

    for (i = 0; i < count; i++, source += 12) {
        const uint32_t* d = packed + (size_t)i * 12u;
        float r, g, b, a;
        if (mode->floating_color) {
            r = pcFtoi0Sat(source[4]); g = pcFtoi0Sat(source[5]); b = pcFtoi0Sat(source[6]); a = pcFtoi0Sat(source[7]);
        } else {
            uint32_t rgba[4];
            memcpy(rgba, source + 4, sizeof(rgba));
            r = (float)(rgba[0] & 0xFFu);
            g = (float)(rgba[1] & 0xFFu);
            b = (float)(rgba[2] & 0xFFu);
            a = (float)(rgba[3] & 0xFFu);
        }

        gs_z[i] = (float)(int32_t)d[10] / 16.0f;
        vertices[i].x = (float)(int32_t)d[8] / 16.0f - 1728.0f;
        vertices[i].y = (float)(int32_t)d[9] / 16.0f - 1808.0f;
        vertices[i].z = pcRendererDepthFromGsZ(gs_z[i]);
        vertices[i].r = fminf(fmaxf(r / 255.0f, 0.0f), 1.0f);
        vertices[i].g = fminf(fmaxf(g / 255.0f, 0.0f), 1.0f);
        vertices[i].b = fminf(fmaxf(b / 255.0f, 0.0f), 1.0f);
        vertices[i].a = fminf(fmaxf(a / 128.0f, 0.0f), 1.0f);
        vertices[i].u = source[0] * tex_width;   /* ST normalizado -> texels */
        vertices[i].v = source[1] * tex_height;
        vertices[i].q = source[2];
        /* F de la GS: todos los llamadores (particulas njPtcl*, NaDraw, NaGraphics3D...) dejan scr.fog (0..255) en el .w del
         * 3er qword y el empaquetador original lo mete en XYZF2.F (bits 4..11). Antes solo se leia con color flotante y el
         * resto quedaba en 1.0 (sin niebla): las particulas con PRIM.FGE no se perdian en la niebla. */
        vertices[i].fog = pcFtoi0Sat(source[11]) / 255.0f;
        kick[i] = (d[11] & 0x8000u) != 0;
    }

    pcSubmitVertices(prim, vertices, kick, gs_z, count, mode, average_z, tex_width, tex_height, texture, 1);

done:
    return;
}

/* Tiras 3D de los modelos (vu1DrawTriangleStrip*): registros VU1_PRIM_BUF de 12 floats que el juego ya dejo proyectados
 * (S,T,Q, kick, RGBA, X,Y,Z, F). Esta ruta NO empaqueta GIF ni usa punto fijo 12.4 ni flags ADC de recorte ni poligonos de
 * scissor: los vertices pasan como floats en pantalla homogenea (x*w, y*w, z*w, w con w = 1/Q) y la GPU recorta contra los
 * planos cercano/lejano y los lados. Lo que el juego decide se conserva: el kick por cara trasera (ulKick, tiras Single), las
 * politicas de OT y la profundidad de la GS (afin en Q, cuantizada a 16 bits en el fragmento). */
/* Conversion de vertices: la CPU solo copia el registro crudo de la VU1 y el vertex shader (screen_space 3 del renderer) hace la
 * conversion a lo que ve la GS: punto fijo 12.4, Z de 16 bits, FTOI0 de color/niebla, S/Q en texels (ver pc/tools/test_gpu_vertex.c y
 * notas 0.9.146). Solo los sprites (topologia 6) siguen convirtiendo en la CPU (screen_space 2). */

/* Perfil (debug=1 + profile_game=1): costes por separado de las tiras 3D. "convert" = CPU por vertice (bucle de conversion o de
 * copia + recorte ADC); "submit" = ensamblado + cola/lote + llamadas a GL hasta volver. El tiempo de GPU sale en
 * "PROFILE gpu scene" (pc_renderer.c). */
static struct { double convert, submit; unsigned int strips, verts; } pcs3d_acc[2];
static void pcStrips3DRecord(int kind, int gpu, double convert_ms, double submit_ms, unsigned int verts)
{
    /* kind 0 = tiras de los modelos (vu1Draw*), 1 = empaquetador del EE (Ps2AddPrim3D / Ps2AddPrim3DMod) */
    pcs3d_acc[kind].convert += convert_ms;
    pcs3d_acc[kind].submit += submit_ms;
    pcs3d_acc[kind].verts += verts;
    if (++pcs3d_acc[kind].strips >= 4000) {
        pcLog("PROFILE %s [%s]: %u strips %u verts | CPU por vertice (convert) %.3f us | envio (ensamblado+cola/lote+GL) %.3f us",
              kind ? "prepared3D" : "strips3D", gpu ? "GPU" : "CPU", pcs3d_acc[kind].strips, pcs3d_acc[kind].verts,
              pcs3d_acc[kind].convert * 1000.0 / pcs3d_acc[kind].verts, pcs3d_acc[kind].submit * 1000.0 / pcs3d_acc[kind].verts);
        memset(&pcs3d_acc[kind], 0, sizeof(pcs3d_acc[kind]));
    }
}

/* Ruta GPU del empaquetador del EE (Ps2AddPrim3D / Ps2AddPrim3DMod), screen_space 4. La CPU ya no ejecuta pcPs2Prim3DCore ni la
 * conversion por vertice: copia el registro de 12 floats (S,T,Q,flags,R,G,B,A,X,Y,Z,F) y el vertex shader hace lo mismo que la
 * ruta CPU de pcSubmitPrepared3D: XY = (int)(X*16)/16 (FTOI4 trunca hacia cero), Z de la GS = (int)(sz*16)/16 con sz = -a - b*Q
 * entre 0 y 65534, color y niebla FTOI0 saturados, S,T en texels. En la CPU quedan solo lo que decide el CPU de verdad: el kick
 * (filtro de recorte ADC de 6 bits) y, solo con ABE, la Z media del OT. El color entero (floating_color=0) se pasa como float
 * 0..255 (mascara 0xFF) para no depender de que la GPU conserve patrones de bits denormales. */
static void pcSubmitPrepared3DGpu(unsigned long prim, const void* data, unsigned int count, const PcPrim3D* mode)
{
    const float* s = (const float*)data;
    PcNativeVertex* vertices;
    unsigned char* kick;
    float* gs_z;
    unsigned int i, clip_hist = 0, texture = pcRendererCurrentTexture();
    float tex_width = 1.0f, tex_height = 1.0f, zsum = 0.0f;
    const int blend = (prim & PC_PRIM_ABE) != 0;
    const int profile = pcGameProfileEnabled();
    double t_start = profile ? pcGameProfileNowMs() : 0.0, t_conv = 0.0;

    if (s == NULL || count < 1 || count > 16384)
        return;
    if ((prim & PC_PRIM_TME) && texture == 0)
        return;

    vertices = (PcNativeVertex*)pcScratch(2, (size_t)count * sizeof(*vertices));
    kick = (unsigned char*)pcScratch(3, count);
    gs_z = (float*)pcScratch(4, (size_t)count * sizeof(*gs_z));
    if (vertices == NULL || kick == NULL || gs_z == NULL)
        return;
    pcRendererCurrentTextureSize(&tex_width, &tex_height);

    for (i = 0; i < count; i++, s += 12) {
        uint32_t flags;
        unsigned int f = 0;
        const float cx = s[8] - 2048.0f, cy = s[9] - 2048.0f, cz = (s[10] + -2048.0625f) + s[10] * 0.062501907f;

        memcpy(&flags, &s[3], sizeof(flags));
        /* mismo filtro de recorte que pcPs2Prim3DCore: x,y contra la ventana de 4096 alrededor de 2048, z contra el rango */
        if (cx > 2047.0f) f |= 0x01;
        if (cx < -2047.0f) f |= 0x02;
        if (cy > 2047.0f) f |= 0x04;
        if (cy < -2047.0f) f |= 0x08;
        if (cz > 2047.0f) f |= 0x10;
        if (cz < -2047.0f) f |= 0x20;
        clip_hist = ((clip_hist << 6) | f) & 0xFFFFFFu;
        if (clip_hist & 0x3FFFFu)
            flags |= 0x8000u;
        kick[i] = (flags & 0x8000u) != 0;

        memcpy(&vertices[i].u, &s[0], 3 * sizeof(float));
        if (mode->floating_color) {
            memcpy(&vertices[i].r, &s[4], 4 * sizeof(float));
        } else {
            uint32_t rgba[4];
            memcpy(rgba, &s[4], sizeof(rgba));
            vertices[i].r = (float)(rgba[0] & 0xFFu);
            vertices[i].g = (float)(rgba[1] & 0xFFu);
            vertices[i].b = (float)(rgba[2] & 0xFFu);
            vertices[i].a = (float)(rgba[3] & 0xFFu);
        }
        memcpy(&vertices[i].x, &s[8], 3 * sizeof(float));
        vertices[i].fog = s[11];

        if (blend) {                                   /* la Z entera solo la usa el OT de las primitivas con mezcla */
            float sz = -Ps2_zbuff_a + s[2] * -Ps2_zbuff_b;
            sz = sz < 0.0f ? 0.0f : sz > 65534.0f ? 65534.0f : sz;
            zsum += sz;
            gs_z[i] = (float)(int32_t)(sz * 16.0f) / 16.0f;
        } else gs_z[i] = 0.0f;
    }
    if (profile) t_conv = pcGameProfileNowMs();

    pcSubmitVertices(prim, vertices, kick, gs_z, count, mode, blend ? zsum / (float)count : 0.0f, tex_width, tex_height, texture, 4);
    if (profile) pcStrips3DRecord(1, 1, t_conv - t_start, pcGameProfileNowMs() - t_conv, count);
}

static void pcSubmitStrip3D(unsigned long prim, const void* data, unsigned int count, const PcPrim3D* mode)
{
    const float* rec = (const float*)data;
    PcNativeVertex* vertices;
    unsigned char* kick;
    float* gs_z;
    unsigned int i, texture = pcRendererCurrentTexture();
    float tex_width = 1.0f, tex_height = 1.0f, zsum = 0.0f;
    unsigned int clip_hist = 0;
    /* Sprite (topologia 6): la GS usa Z/color/niebla del SEGUNDO vertice y la Q del primero para dos esquinas; el vertex shader
     * saca Z de la Q del vertice, asi que no puede expresar eso. Ese caso sigue por la conversion de la CPU (mismo resultado). */
    const int gpu = PC_PRIM_TOPOLOGY(prim) != 6u;
    const int need_z = !gpu || (prim & PC_PRIM_ABE) != 0;   /* en GPU, la Z entera solo hace falta en CPU para ordenar el OT */
    const int profile = pcGameProfileEnabled();
    double t_start = profile ? pcGameProfileNowMs() : 0.0, t_conv = 0.0;

    if (rec == NULL || count < 3 || count > 16384)
        return;
    if ((prim & PC_PRIM_TME) && texture == 0)
        return;

    vertices = (PcNativeVertex*)pcScratch(2, (size_t)count * sizeof(*vertices));
    kick = (unsigned char*)pcScratch(3, count);
    gs_z = (float*)pcScratch(4, (size_t)count * sizeof(*gs_z));
    if (vertices == NULL || kick == NULL || gs_z == NULL)
        goto done;

    pcRendererCurrentTextureSize(&tex_width, &tex_height);
    {
        static unsigned int strips;
        if (strips++ < 3 || strips % 20000 == 0)
            pcLog("tiras 3D nativas (homogeneas): #%u n=%u prim=0x%x q0=%.5f xy0=(%.1f,%.1f)", strips, count,
                  (unsigned)(prim >> 47), rec[2], rec[8], rec[9]);
    }

    for (i = 0; i < count; i++, rec += 12) {
        uint32_t flags;
        float q = rec[2], sz, zc;

        memcpy(&flags, &rec[3], sizeof(flags));
        /* Filtro de recorte del VU0 original (pcPs2Prim3DCore): un vertice fuera de la ventana de 4096 (+-2047 alrededor de
         * 2048) o del rango Z marca ADC (no dispara triangulo) y el juego dibuja en su lugar los poligonos de scissor. El
         * historial de 6 bits por vertice abarca los 3 ultimos: cualquier vertice fuera descarta el triangulo entero. Sin
         * esto el triangulo original se dibuja ademas del recortado (0.9.123: escaleras de la pantalla de carga rotas). */
        {
            const float cx = rec[8] - 2048.0f, cy = rec[9] - 2048.0f, cz = (rec[10] + -2048.0625f) + rec[10] * 0.062501907f;
            unsigned int f = 0;
            if (cx > 2047.0f) f |= 0x01;
            if (cx < -2047.0f) f |= 0x02;
            if (cy > 2047.0f) f |= 0x04;
            if (cy < -2047.0f) f |= 0x08;
            if (cz > 2047.0f) f |= 0x10;
            if (cz < -2047.0f) f |= 0x20;
            clip_hist = ((clip_hist << 6) | f) & 0xFFFFFFu;
            if (clip_hist & 0x3FFFFu)
                flags |= 0x8000u;
            if (f) {   /* diagnostico: por que lados se sale el vertice (ver "tiras ADC" en la rafaga) */
                static unsigned int n_out, n_far, n_near, n_xy, n_total;
                extern int pcRendererBurstDiag(void);
                n_out++;
                if (f & 0x10) n_far++;
                if (f & 0x20) n_near++;
                if (f & 0x0F) n_xy++;
                if (pcRendererBurstDiag() && (n_out % 64) == 1)
                    pcLog("tiras ADC: fuera=%u (z lejano=%u z cercano=%u xy=%u) ultimo cz=%.1f z=%.1f", n_out, n_far, n_near, n_xy, cz, rec[10]);
                (void)n_total;
            }
        }
        if (gpu) {
            /* Ruta GPU: el registro (S,T,Q, flags, R,G,B,A, X,Y,Z, F) pasa tal cual al vertex shader. PcNativeVertex lleva
             * u,v,q = S,T,Q; r,g,b,a = RGBA crudo; x,y,z = X,Y,Z; fog = F. Sin conversion por vertice en la CPU. */
            memcpy(&vertices[i].u, &rec[0], 3 * sizeof(float));
            memcpy(&vertices[i].r, &rec[4], 4 * sizeof(float));
            memcpy(&vertices[i].x, &rec[8], 3 * sizeof(float));
            vertices[i].fog = rec[11];
            if (need_z) {                               /* solo tiras con mezcla (OT): su orden necesita la Z entera de la GS */
                if (q > -1e-9f && q < 1e-9f)
                    q = 1e-9f;
                sz = -Ps2_zbuff_a + q * -Ps2_zbuff_b;
                zc = sz < 0.0f ? 0.0f : sz > 65534.0f ? 65534.0f : sz;
                zc = floorf(zc);
                gs_z[i] = zc;
                zsum += zc;
            } else gs_z[i] = 0.0f;
            kick[i] = (flags & 0x8000u) != 0;
            continue;
        }
        if (q > -1e-9f && q < 1e-9f)
            q = 1e-9f;                                  /* w = 1/q finita */

        sz = -Ps2_zbuff_a + q * -Ps2_zbuff_b;           /* Z de la GS: afin en Q, por eso el recorte homogeneo es exacto */
        zc = sz < 0.0f ? 0.0f : sz > 65534.0f ? 65534.0f : sz;
        zc = floorf(zc);                                /* Z de 32 bits entero (FTOI0): se trunca, no se redondea */
        gs_z[i] = zc;
        zsum += zc;

        /* XYZ2 de la GS es punto fijo 12.4: FTOI4 trunca a 1/16 de pixel */
        vertices[i].x = floorf(rec[8] * 16.0f) / 16.0f - 1728.0f;
        vertices[i].y = floorf(rec[9] * 16.0f) / 16.0f - 1808.0f;
        /* Z de la GS con el MISMO fijado por vertice que el empaquetador original (0..65534): lo que quedaba mas cerca que el
         * plano cercano se dibujaba aplastado contra el, no recortado. Con el Z sin fijar (0.9.123-125) la GPU recortaba la
         * geometria cercana: escaleras de la pantalla de carga (z = -0.27) desaparecian. */
        vertices[i].z = pcRendererDepthFromGsZ(zc);
        vertices[i].q = q;
        vertices[i].u = rec[0] / q * tex_width;         /* S = U*Q: con la interpolacion de la GPU ya no se divide por Q */
        vertices[i].v = rec[1] / q * tex_height;
        vertices[i].r = pcFtoi0Sat(rec[4]) / 255.0f;
        vertices[i].g = pcFtoi0Sat(rec[5]) / 255.0f;
        vertices[i].b = pcFtoi0Sat(rec[6]) / 255.0f;
        vertices[i].a = fminf(pcFtoi0Sat(rec[7]) / 128.0f, 1.0f);
        vertices[i].fog = pcFtoi0Sat(rec[11]) / 255.0f;
        kick[i] = (flags & 0x8000u) != 0;
    }
    if (profile) t_conv = pcGameProfileNowMs();

    pcSubmitVertices(prim, vertices, kick, gs_z, count, mode, need_z ? zsum / (float)count : 0.0f, tex_width, tex_height, texture,
                     gpu ? 3 : 2);
    if (profile) pcStrips3DRecord(0, gpu, t_conv - t_start, pcGameProfileNowMs() - t_conv, count);

done:
    return;
}

void Ps2AddPrim3D(unsigned long prim, void* dp, unsigned int num)
{
    TIM2_PICTUREHEADER_EX* timp;  
    
    
    if ((prim & PC_PRIM_TME)) 
    {
        if (Ps2_now_tex == NULL) 
        {
            return;
        }

        if ((prim & PC_PRIM_ABE)) 
        {
            if (Ps2_use_pt_flag != 0)
            {
                prim &= ~PC_PRIM_ABE;
            }
        }
        
        if (!(prim & PC_PRIM_ABE)) 
        {
            timp = (TIM2_PICTUREHEADER_EX*)Ps2_now_tex->texinfo.texsurface.pSurface;
            
            if (timp->TpFlag != 0) 
            {
                Ps2_tex_load_tp_cancel = 1;
                
                Ps2TexLoad(Ps2_now_tex);
                
                Ps2_tex_load_tp_cancel = 0;
            }
        }
    } 

    {
        PcPrim3D mode = { 0, 0, PC_OT_AVERAGE, 0.0f };
        pcPrimDiag("Ps2AddPrim3D", prim, dp, num);
        pcSubmitPrepared3D(prim, dp, num, &mode);
        return;
    }
}

// 100% matching!
void Ps2AddPrim3DEx(unsigned long prim, void* dp, unsigned int num)
{
    TIM2_PICTUREHEADER_EX* timp;  
    
    
    if ((prim & PC_PRIM_TME)) 
    {
        if (Ps2_now_tex == NULL) 
        {
            return;
        }

        if ((prim & PC_PRIM_ABE)) 
        {
            if (Ps2_use_pt_flag != 0)
            {
                prim &= ~PC_PRIM_ABE;
            }
        }
        
        if (!(prim & PC_PRIM_ABE)) 
        {
            timp = (TIM2_PICTUREHEADER_EX*)Ps2_now_tex->texinfo.texsurface.pSurface;
            
            if (timp->TpFlag != 0) 
            {
                Ps2_tex_load_tp_cancel = 1;
                
                Ps2TexLoad(Ps2_now_tex);
                
                Ps2_tex_load_tp_cancel = 0;
            }
        }
    } 

    {
        PcPrim3D mode = { 1, 0, PC_OT_AVERAGE, 0.0f };
        pcSubmitStrip3D(prim, dp, num, &mode);
        return;
    }
}

// 100% matching!
void Ps2AddPrim3DEx1P(unsigned long prim, void* dp, unsigned int num)
{
    TIM2_PICTUREHEADER_EX* timp;  
    
    
    if ((prim & PC_PRIM_TME)) 
    {
        if (Ps2_now_tex == NULL) 
        {
            return;
        }

        if ((prim & PC_PRIM_ABE)) 
        {
            if (Ps2_use_pt_flag != 0)
            {
                prim &= ~PC_PRIM_ABE;
            }
        } 
        
        if (!(prim & PC_PRIM_ABE)) 
        {
            timp = (TIM2_PICTUREHEADER_EX*)Ps2_now_tex->texinfo.texsurface.pSurface;
            
            if (timp->TpFlag != 0) 
            {
                Ps2_tex_load_tp_cancel = 1;
                
                Ps2TexLoad(Ps2_now_tex);
                
                Ps2_tex_load_tp_cancel = 0;
            }
        } 
        else if (Ps2_albinoid_flag != 0) 
        {
            Ps2_tex_load_tp_cancel = 1;
                
            Ps2TexLoad(Ps2_now_tex);
            
            Ps2_tex_load_tp_cancel = 0;
        }
    } 

    {
        PcPrim3D mode = { 1, 0, (Ps2_albinoid_flag == 0) ? PC_OT_FIXED : PC_OT_IMMEDIATE, Ps2AddPrimPrio };
        pcSubmitStrip3D(prim, dp, num, &mode);
        return;
    }
}

// 100% matching!
void Ps2AddPrim3DMod(unsigned long prim, void* dp, unsigned int num)
{
    
    
    {
        PcPrim3D mode = { 1, 1, PC_OT_IMMEDIATE, 0.0f };
        pcPrimDiag("Ps2AddPrim3DMod", prim, dp, num);
        pcSubmitPrepared3D(prim, dp, num, &mode);
        return;
    }
}

// 100% matching! 
/* CLAUDE-NOTE[N8]: en PC esto ya NO recorre el OT; solo vacia la cola de transparencias. El orden de pintado por prioridad
 * (otz) que implementaba Ps2DrawOTagSub no existe. Ademas ya no envuelve el dibujo con Ps2ZbuffOff()/Ps2ZbuffOn(). */
void Ps2DrawOTag()
{
    pcRendererFlushTransparent();
    Ps2_current_texbreak = 1;
}

// 100% matching!
unsigned int Ps2BitCount(register unsigned int value)
{
    {
        unsigned int v = value - 1;
        unsigned int n = 0;

        if (value == 0)
        {
            return 0;
        }

        while (v != 0)
        {
            n++;
            v >>= 1;
        }

        return n;
    }
}

// 100% matching!
void Ps2InitTexCache()
{
    unsigned int i;

    for (i = 0; i < 4; i++) 
    {
        Ps2_tex_cache_buff[i] = NULL;
        
        Ps2_tex_cache_beflag[i] = 0;
    } 
    
    Ps2_tex_cache_num = 4;
    Ps2PcTextureSourceCacheReset();
    pcRendererBindTexture(0);
}

// 100% matching!
int Ps2GlobalIndexTexLoad(unsigned int index)
{
	int no;

	no = SearchNumber(index, -1);

    if (no >= 0) 
	{
        Ps2TexLoad(&Ps2_tex_info[no]);
    }
	else 
	{
    	printf("Ps2GlobalIndexTexLoad ERROR!!! %08x\n", index);
	}
    return 0;
}

// 97.17% matching
int Ps2TexLoad(NJS_TEXMEMLIST* addr)
{
    extern int pcNativeTextureLoad(NJS_TEXMEMLIST* source);
    if (addr == NULL) {
        Ps2_now_tex = NULL;
        Ps2_now_bank = 0;
        pcRendererBindTexture(0);
        return 0;
    }
    Ps2_now_tex = addr;
    Ps2_now_bank = addr->bank;
    {
        /* Con el flag de render-tex activo, una textura RGB32 (no paletizada y no PictFormat 5..8) se sustituye por la copia
         * de pantalla y el flag se consume (rama final de Ps2BindTextureState). */
        const TIM2_PICTUREHEADER_EX* timp = (const TIM2_PICTUREHEADER_EX*)addr->texinfo.texsurface.pSurface;
        if (PS2_Render_tex_sub_flag != 0 && timp != NULL &&
            !((timp->PictFormat >= 5) && (timp->PictFormat <= 8)) && timp->ImageType != 4 && timp->ImageType != 5)
        {
            pcRendererBindRenderTexture();
            PS2_Render_tex_sub_flag = 0;
            return 0;
        }
    }
    return pcNativeTextureLoad(addr);
}

// 100% matching!
void Ps2SetFogColor()
{
    extern float fNaFogR, fNaFogG, fNaFogB;
    extern unsigned int ulNaFogState;
    pcRendererSetGlobalFog(ulNaFogState != 0, fNaFogR, fNaFogG, fNaFogB);
    return;
}

// 100% matching!
void Ps2SetFogColorSys(unsigned int r, unsigned int g, unsigned int b)
{
    extern unsigned int ulNaFogState;
    pcRendererSetGlobalFog(ulNaFogState != 0, r / 255.0f, g / 255.0f, b / 255.0f);
    return;
}

// 100% matching!
void Ps2AlphaIs000(unsigned int* cp, unsigned int num)
{
    unsigned int i;
    
    for (i = 0; i < num; i++, cp++)
    {
        if ((((unsigned char*)cp)[3] != 0) && (((unsigned char*)cp)[0] == 0) && (((unsigned char*)cp)[1] == 0) && (((unsigned char*)cp)[2] == 0)) 
        {
            ((unsigned char*)cp)[3] = 0;
        }
    }
}

// 100% matching!
unsigned int Ps2AlphaIsHalf(unsigned int* cp, unsigned int num)
{
    unsigned int i;
    
    for (i = 0; i < num; i++, cp++)
    {
        ((unsigned char*)cp)[3] = (((unsigned char*)cp)[3] + 1) >> 1;
    }
    return 0;
}

// 100% matching!
unsigned int Ps2Alpha4to8(unsigned int* cp, unsigned int num)
{
    unsigned int i;
    unsigned int val;
    
    for (i = 0; i < num; i++, cp++)
    {
        val = ((unsigned char*)cp)[3];
        
        ((unsigned char*)cp)[3] = val | (val >> 4);
    }
    return 0;
}

// 100% matching!
int Ps2CheckTextureAlpha(void* pp)
{
    unsigned int *cp;  
    unsigned int num;  
    unsigned int flag; 
    unsigned int temp; // not from the debugging symbols
    TIM2_PICTUREHEADER_EX* temp2; // not from the debugging symbols

    temp2 = pp;
    
    if (temp2->ClutChange != 0)
    {
        return (unsigned short)temp2->ClutChange;
    }
    
    cp = (unsigned int*)((int)pp + temp2->ImageSize + 256);
    
    num = temp2->ClutSize >> 2;
    
    if (isVQ(temp2->PictFormat) != 0) 
    {
        switch (temp2->OrgColorType) 
        {                       
        case 0:                                     
            flag = 0x8000;
            
            printf("ARGB1555\n", temp2->OrgColorType);
            break;
        case 1:                                    
            flag = 0x8000;
            
            printf("RGB565\n", temp2->OrgColorType);
            break;
        case 2:                                    
            flag = 0x8000;
            
            Ps2Alpha4to8(cp, num);
            Ps2AlphaIsHalf(cp, num);
            
            printf("ARGB4444\n");
            break;
        case 6:                                  
            flag = 0x8000;
            
            Ps2AlphaIsHalf(cp, num);
            
            printf("ARGB8888\n");
            break;
        default:                                   
            printf("ERROR ERROR ERROR ERROR ERROR %04x\n", temp2->OrgColorType);
            break;
        }
    } 
    else 
    {
        switch (temp2->OrgColorType)
        {                       
        case 0:
            flag = 0x8000;
            
            Ps2AlphaIs000(cp, num);
            
            printf("ARGB1555\n");
            break;
        case 1:
            flag = 0x8000;
            
            Ps2AlphaIs000(cp, num);
            
            printf("RGB565\n");
            break;
        case 2:
            flag = 0x8000;
            
            Ps2AlphaIs000(cp, num);
            Ps2Alpha4to8(cp, num);
            Ps2AlphaIsHalf(cp, num);
            
            printf("ARGB4444\n");
            break;
        case 6:
            flag = 0x8000;
            
            Ps2AlphaIs000(cp, num);
            Ps2AlphaIsHalf(cp, num);
            
            printf("ARGB8888\n");
            break;
        default:
            printf("ERROR ERROR ERROR ERROR ERROR %04x\n", temp2->OrgColorType);
            break;
        }
    }
    
    temp2->ClutChange = flag;
    
    temp = temp2->Gindex & 0xFFFFF;
    
    if ((temp == 20500) || (temp == 15205) || (temp == 15206) || (temp == 15207) || (temp == 15208) || (temp == 15209))
    {
        temp2->ClampFlag = 0xFF000FF0;
    }
    else 
    {
        temp2->ClampFlag = 0xFF000FF5;
    }
 
    Ps2PxlconvCheck(pp);
    
    return flag;
}

// 100% matching! 
void Ps2ShadowStart()
{
	Ps2_current_texbreak = 1;
    
    /* FRAME_2 = mascara de 16 bits (FBP 380) con la Z de la escena (ZBUF_2.ZMSK=1); se limpia a 0 */
    pcRendererShadowBegin();
    return;
}

// 100% matching!
void Ps2ShadowDraw()
{

    if ((Ps2_shadow_vec[0] < 1536.0f) || (Ps2_shadow_vec[0] >= 2560.0f))
    {
        return;
    }

    if ((Ps2_shadow_vec[1] < 1536.0f) || (Ps2_shadow_vec[1] >= 2560.0f))
    {
        return;
    }

    if ((Ps2_shadow_vec[2] < 1536.0f) || (Ps2_shadow_vec[2] >= 2560.0f))
    {
        return;
    }

    if ((Ps2_shadow_vec[3] < 1536.0f) || (Ps2_shadow_vec[3] >= 2560.0f))
    {
        return;
    }

    if (Ps2_shadow_vec[0] < 1728.0f) 
    {
        Ps2_shadow_vec[0] = 1728.0f;
    }
    
    if (Ps2_shadow_vec[0] > 2368.0f) 
    {
        Ps2_shadow_vec[0] = 2368.0f;
    }
    
    if (Ps2_shadow_vec[1] < 1808.0f) 
    {
        Ps2_shadow_vec[1] = 1808.0f;
    }
    
    if (Ps2_shadow_vec[1] > 2288.0f) 
    {
        Ps2_shadow_vec[1] = 2288.0f;
    }
    
    if (Ps2_shadow_vec[2] < 1728.0f) 
    {
        Ps2_shadow_vec[2] = 1728.0f;
    }
    
    if (Ps2_shadow_vec[2] > 2368.0f)
    {
        Ps2_shadow_vec[2] = 2368.0f;
    }
    
    if (Ps2_shadow_vec[3] < 1808.0f) 
    {
        Ps2_shadow_vec[3] = 1808.0f;
    }
    
    if (Ps2_shadow_vec[3] > 2288.0f) 
    {
        Ps2_shadow_vec[3] = 2288.0f;
    }
    
    if (Ps2_shadow_z != 0)
    {
        Ps2_shadow_vec[0] = 0;
        Ps2_shadow_vec[1] = 0;
        Ps2_shadow_vec[2] = 0;
        Ps2_shadow_vec[3] = 0;
    }
    
    /* sprite con la mascara como textura, RGB = fog*3/16, mezcla (Cd - Cs); despues se limpia el rectangulo de la mascara */
    pcRendererShadowCompose(Ps2_shadow_vec[0] - 1728.0f, Ps2_shadow_vec[1] - 1808.0f,
                            Ps2_shadow_vec[2] - 1728.0f, Ps2_shadow_vec[3] - 1808.0f,
                            (int)(((unsigned int)Ps2_shadow_fog * 3) / 16));
    return;
}

// 100% matching! 
void Ps2ShadowMain0()
{
	static const unsigned long shadow_head[8] = { 0x70000003UL, 0x0UL, 0x1000000000008002UL, 0x0EUL, 0x13A00012CUL, 0x4FUL, 0x8000000029UL, 0x43UL };

	Ps2_shadow_vec[0] =  65536.0f;
	Ps2_shadow_vec[1] =  65536.0f;

	Ps2_shadow_vec[2] = -65536.0f;
	Ps2_shadow_vec[3] = -65536.0f;

    /* cabecera del paquete: ZBUF_2 (ZMSK=1) y ALPHA_2 = 0x8000000029 -> Cs + Cd (suma las caras del volumen) */
    { static const PcBlendEquation eq = { PC_BLEND_CD, PC_BLEND_ZERO, PC_BLEND_FIX, PC_BLEND_CS, 128 }; pcRendererSetBlend2(&eq); }
    return;
}

// 100% matching! 
void Ps2ShadowMain1()
{
	static const unsigned long shadow_tail[6] = { 0x70000002UL, 0x0UL, 0x1000000000008001UL, 0x0EUL, 0x80000000A1UL, 0x43UL };

    /* ALPHA_2 = 0x80000000A1 -> Cd - Cs (resta las caras del otro sentido) */
    { static const PcBlendEquation eq = { PC_BLEND_CD, PC_BLEND_CS, PC_BLEND_FIX, PC_BLEND_ZERO, 128 }; pcRendererSetBlend2(&eq); }
    return;
}

// 100% matching! 
void Ps2ShadowEnd()
{
    pcRendererShadowEnd();
    { static const PcBlendEquation eq = { PC_BLEND_CS, PC_BLEND_CS, PC_BLEND_AS, PC_BLEND_CS, 0 }; pcRendererSetBlend2(&eq); }   /* (Cs - Cs) * As + Cs: sin mezcla */
    return;
}

// 100% matching!
void Ps2AddPrim3DExI(unsigned long prim, void* dp, unsigned int num)
{
    TIM2_PICTUREHEADER_EX* timp;      

    
    if ((prim & PC_PRIM_TME)) 
    {
        if (Ps2_now_tex == NULL) 
        {
            return;
        }

        if ((prim & PC_PRIM_ABE)) 
        {
            if (Ps2_use_pt_flag != 0)
            {
                prim &= ~PC_PRIM_ABE;
            }
        }
        
        if (!(prim & PC_PRIM_ABE)) 
        {
            timp = (TIM2_PICTUREHEADER_EX*)Ps2_now_tex->texinfo.texsurface.pSurface;
            
            if (timp->TpFlag != 0) 
            {
                Ps2_tex_load_tp_cancel = 1;
                
                Ps2TexLoad(Ps2_now_tex);
                
                Ps2_tex_load_tp_cancel = 0;
            }
        }
    } 

    {
        PcPrim3D mode = { 1, 0, PC_OT_PER_TRIANGLE, 0.0f };
        pcSubmitStrip3D(prim, dp, num, &mode);
        return;
    }
}

// 100% matching!
/* PC: el target de PS2_Render_Tex_Sub es una textura GPU escalable; las UV lógicas siguen siendo 256x256. */
void PS2_Render_Tex_Sub() 
{
    PS2_Render_tex_sub_flag = 1;

    /* La GS copiaba la pantalla (sprite UV 0..640x480 -> XY 0,-16..320,240) a la zona de VRAM de 256x256 que despues se
     * enlazaba como textura (Ps2BindTextureState con el flag activo). Ver pcRendererCopyScreenToRenderTexture. */
    pcRendererCopyScreenToRenderTexture(0, 0, SCR_WIDTH2, SCR_HEIGHT2, 0, -16, SCR_WIDTH2 / 2, SCR_HEIGHT2 / 2);
    return;
}
