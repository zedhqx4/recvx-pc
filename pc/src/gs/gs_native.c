/* Native CPU shadow renderer.
 *
 * Parallel reference path: the established OpenGL renderer receives every batch first, then the
 * same GsDrawState/GsVertex batch is sent here. This first milestone rasterizes only untextured,
 * unblended, depth-disabled primitives into private linear CPU framebuffers. It never writes to
 * gsVram, so it cannot perturb the live GS/OpenGL path.
 *
 * Unsupported GS state is counted and skipped rather than silently approximated.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gs_internal.h"

#define GS_NATIVE_MAX_TARGETS 8
#define GS_NATIVE_MAX_WIDTH   4096
#define GS_NATIVE_MAX_HEIGHT  2048
#define GS_NATIVE_DEFAULT_H   512

enum
{
    GS_NATIVE_SKIP_NONE = 0,
    GS_NATIVE_SKIP_TEXTURE,
    GS_NATIVE_SKIP_BLEND,
    GS_NATIVE_SKIP_FOG_AA,
    GS_NATIVE_SKIP_PIXEL_TEST,
    GS_NATIVE_SKIP_DEPTH,
    GS_NATIVE_SKIP_FORMAT,
    GS_NATIVE_SKIP_TARGET,
    GS_NATIVE_SKIP_PRIMITIVE,
    GS_NATIVE_SKIP_COUNT
};

typedef struct
{
    int used;
    unsigned int fbp, fbw;
    int width, height;
    unsigned int* pixels; /* 0xAABBGGRR, matching little-endian RGBA bytes */
} GsNativeTarget;

typedef struct
{
    unsigned int draws;
    unsigned int rasterized;
    unsigned int skipped[GS_NATIVE_SKIP_COUNT];
    unsigned long long pixels;
} GsNativeStats;

static GsNativeTarget native_targets[GS_NATIVE_MAX_TARGETS];
static GsNativeStats native_stats;
static unsigned int native_frame;
static int native_dump = -1;

extern void pcLog(const char* fmt, ...);
#ifndef GS_NATIVE_TEST
extern void pcGsScreenshot(const char* path, unsigned int fbp);
#endif

static void gsNativeFreeTargets(void)
{
    int i;
    for (i = 0; i < GS_NATIVE_MAX_TARGETS; i++)
        free(native_targets[i].pixels);
    memset(native_targets, 0, sizeof(native_targets));
}

void gsNativeInit(void)
{
    gsNativeFreeTargets();
    memset(&native_stats, 0, sizeof(native_stats));
    native_frame = 0;
    native_dump = -1;
}

static int gsNativeEnsureHeight(GsNativeTarget* target, int height)
{
    unsigned int* pixels;
    size_t old_count, new_count;

    if (height <= target->height)
        return 1;
    if (height > GS_NATIVE_MAX_HEIGHT)
        height = GS_NATIVE_MAX_HEIGHT;
    if (height <= target->height)
        return 1;

    old_count = (size_t)target->width * target->height;
    new_count = (size_t)target->width * height;
    pixels = (unsigned int*)realloc(target->pixels, new_count * sizeof(*pixels));
    if (pixels == NULL)
        return 0;

    memset(pixels + old_count, 0, (new_count - old_count) * sizeof(*pixels));
    target->pixels = pixels;
    target->height = height;
    return 1;
}

static GsNativeTarget* gsNativeGetTarget(unsigned int fbp, unsigned int fbw, int needed_height)
{
    GsNativeTarget* free_target = NULL;
    int i, width;

    if (fbw == 0)
        return NULL;
    width = (int)(fbw * 64u);
    if (width <= 0 || width > GS_NATIVE_MAX_WIDTH)
        return NULL;

    if (needed_height < GS_NATIVE_DEFAULT_H)
        needed_height = GS_NATIVE_DEFAULT_H;
    if (needed_height > GS_NATIVE_MAX_HEIGHT)
        needed_height = GS_NATIVE_MAX_HEIGHT;

    for (i = 0; i < GS_NATIVE_MAX_TARGETS; i++)
    {
        GsNativeTarget* target = &native_targets[i];
        if (target->used && target->fbp == fbp && target->fbw == fbw)
        {
            if (!gsNativeEnsureHeight(target, needed_height))
                return NULL;
            return target;
        }
        if (!target->used && free_target == NULL)
            free_target = target;
    }

    if (free_target == NULL)
        return NULL;
    free_target->pixels = (unsigned int*)calloc((size_t)width * needed_height, sizeof(unsigned int));
    if (free_target->pixels == NULL)
        return NULL;

    free_target->used = 1;
    free_target->fbp = fbp;
    free_target->fbw = fbw;
    free_target->width = width;
    free_target->height = needed_height;
    return free_target;
}

static GsNativeTarget* gsNativeFindDisplayTarget(unsigned int fbp, unsigned int fbw)
{
    GsNativeTarget* fallback = NULL;
    int i;

    for (i = 0; i < GS_NATIVE_MAX_TARGETS; i++)
    {
        GsNativeTarget* target = &native_targets[i];
        if (!target->used || target->fbp != fbp)
            continue;
        if (target->fbw == fbw)
            return target;
        if (fallback == NULL)
            fallback = target;
    }
    return fallback;
}

static int gsNativeSkipReason(const GsDrawState* st, int nverts)
{
    unsigned int fpsm = GSF(st->frame, 24, 6);
    unsigned int ztst = GSF(st->test, 17, 2);
    unsigned int zmsk = GSF(st->zbuf, 32, 1);

    if (st->tme)
        return GS_NATIVE_SKIP_TEXTURE;
    if (st->abe)
        return GS_NATIVE_SKIP_BLEND;
    if (st->fge || st->aa1 || GSF(st->fba, 0, 1) || GSF(st->dthe, 0, 1))
        return GS_NATIVE_SKIP_FOG_AA;
    if (GSF(st->test, 0, 1) || GSF(st->test, 14, 1))
        return GS_NATIVE_SKIP_PIXEL_TEST;
    /* Match the current GL backend's no-depth fast case; depth testing is a later milestone. */
    if (ztst != 1 || !zmsk)
        return GS_NATIVE_SKIP_DEPTH;
    if (fpsm != GS_PSMCT32 && fpsm != GS_PSMCT24)
        return GS_NATIVE_SKIP_FORMAT;
    if (nverts <= 0)
        return GS_NATIVE_SKIP_PRIMITIVE;

    switch (st->type)
    {
    case 0:
        break;
    case 1:
    case 2:
    case 6:
        if (nverts < 2 || (nverts & 1))
            return GS_NATIVE_SKIP_PRIMITIVE;
        break;
    case 3:
    case 4:
    case 5:
        if (nverts < 3 || (nverts % 3))
            return GS_NATIVE_SKIP_PRIMITIVE;
        break;
    default:
        return GS_NATIVE_SKIP_PRIMITIVE;
    }
    return GS_NATIVE_SKIP_NONE;
}

static unsigned char gsNativeByte(float value)
{
    if (!(value > 0.0f))
        return 0;
    if (value >= 255.0f)
        return 255;
    return (unsigned char)floorf(value);
}

static void gsNativeWritePixel(GsNativeTarget* target, const GsDrawState* st,
                               unsigned int fbmask, int x, int y,
                               float r, float g, float b, float a)
{
    unsigned int src, dst, mask;
    unsigned char alpha;
    size_t index;

    if (x < 0 || y < 0 || x >= target->width || y >= target->height)
        return;

    /* The GL fragment output stores min(A / 128, 1) in an 8-bit normalized target. */
    alpha = gsNativeByte(a * (255.0f / 128.0f) + 0.5f);
    src = (unsigned int)gsNativeByte(r) |
          ((unsigned int)gsNativeByte(g) << 8) |
          ((unsigned int)gsNativeByte(b) << 16) |
          ((unsigned int)alpha << 24);
    mask = fbmask;
    if (GSF(st->frame, 24, 6) == GS_PSMCT24)
        mask |= 0xFF000000u;

    index = (size_t)y * target->width + (unsigned int)x;
    dst = target->pixels[index];
    target->pixels[index] = (src & ~mask) | (dst & mask);
    native_stats.pixels++;
}

static int gsNativeInsideScissor(int x, int y, int x0, int y0, int x1, int y1)
{
    return x >= x0 && x <= x1 && y >= y0 && y <= y1;
}

static void gsNativeColor(const GsDrawState* st, const GsVertex* a,
                          const GsVertex* b, const GsVertex* c,
                          float wa, float wb, float wc,
                          float* r, float* g, float* blue, float* alpha)
{
    if (!st->iip)
    {
        *r = a->r; *g = a->g; *blue = a->b; *alpha = a->a;
    }
    else
    {
        *r = a->r * wa + b->r * wb + c->r * wc;
        *g = a->g * wa + b->g * wb + c->g * wc;
        *blue = a->b * wa + b->b * wb + c->b * wc;
        *alpha = a->a * wa + b->a * wb + c->a * wc;
    }
}

static void gsNativeDrawPoint(GsNativeTarget* target, const GsDrawState* st,
                              const GsVertex* v, unsigned int fbmask,
                              int x0, int y0, int x1, int y1)
{
    int x = (int)floorf(v->x);
    int y = (int)floorf(v->y);
    if (gsNativeInsideScissor(x, y, x0, y0, x1, y1))
        gsNativeWritePixel(target, st, fbmask, x, y, v->r, v->g, v->b, v->a);
}

static void gsNativeDrawLine(GsNativeTarget* target, const GsDrawState* st,
                             const GsVertex* a, const GsVertex* b, unsigned int fbmask,
                             int x0, int y0, int x1, int y1)
{
    float dx = b->x - a->x, dy = b->y - a->y;
    int steps = (int)ceilf(fmaxf(fabsf(dx), fabsf(dy)));
    int i;

    if (steps < 1)
        steps = 1;
    for (i = 0; i <= steps; i++)
    {
        float t = (float)i / (float)steps;
        int x = (int)floorf(a->x + dx * t);
        int y = (int)floorf(a->y + dy * t);
        float r, g, blue, alpha;

        if (!gsNativeInsideScissor(x, y, x0, y0, x1, y1))
            continue;
        gsNativeColor(st, a, b, b, 1.0f - t, t, 0.0f, &r, &g, &blue, &alpha);
        gsNativeWritePixel(target, st, fbmask, x, y, r, g, blue, alpha);
    }
}

static void gsNativeDrawSprite(GsNativeTarget* target, const GsDrawState* st,
                               const GsVertex* a, const GsVertex* b, unsigned int fbmask,
                               int x0, int y0, int x1, int y1)
{
    float min_x = fminf(a->x, b->x), max_x = fmaxf(a->x, b->x);
    float min_y = fminf(a->y, b->y), max_y = fmaxf(a->y, b->y);
    int left = (int)floorf(min_x), right = (int)ceilf(max_x) - 1;
    int top = (int)floorf(min_y), bottom = (int)ceilf(max_y) - 1;
    int x, y;

    if (left < x0) left = x0;
    if (right > x1) right = x1;
    if (top < y0) top = y0;
    if (bottom > y1) bottom = y1;
    for (y = top; y <= bottom; y++)
        for (x = left; x <= right; x++)
            /* The current GL sprite expansion takes color from its second endpoint. */
            gsNativeWritePixel(target, st, fbmask, x, y, b->r, b->g, b->b, b->a);
}

static float gsNativeEdge(const GsVertex* a, const GsVertex* b, float x, float y)
{
    return (b->x - a->x) * (y - a->y) - (b->y - a->y) * (x - a->x);
}

static void gsNativeDrawTriangle(GsNativeTarget* target, const GsDrawState* st,
                                 const GsVertex* a, const GsVertex* b, const GsVertex* c,
                                 unsigned int fbmask, int x0, int y0, int x1, int y1)
{
    float area = gsNativeEdge(a, b, c->x, c->y);
    float min_x, max_x, min_y, max_y;
    int left, right, top, bottom, x, y;

    if (fabsf(area) < 0.000001f)
        return;
    min_x = fminf(a->x, fminf(b->x, c->x));
    max_x = fmaxf(a->x, fmaxf(b->x, c->x));
    min_y = fminf(a->y, fminf(b->y, c->y));
    max_y = fmaxf(a->y, fmaxf(b->y, c->y));
    left = (int)floorf(min_x);
    right = (int)ceilf(max_x) - 1;
    top = (int)floorf(min_y);
    bottom = (int)ceilf(max_y) - 1;

    if (left < x0) left = x0;
    if (right > x1) right = x1;
    if (top < y0) top = y0;
    if (bottom > y1) bottom = y1;

    for (y = top; y <= bottom; y++)
    {
        for (x = left; x <= right; x++)
        {
            float px = (float)x + 0.5f, py = (float)y + 0.5f;
            float wa = gsNativeEdge(b, c, px, py) / area;
            float wb = gsNativeEdge(c, a, px, py) / area;
            float wc = gsNativeEdge(a, b, px, py) / area;
            float r, g, blue, alpha;

            if (wa < 0.0f || wb < 0.0f || wc < 0.0f)
                continue;
            gsNativeColor(st, a, b, c, wa, wb, wc, &r, &g, &blue, &alpha);
            gsNativeWritePixel(target, st, fbmask, x, y, r, g, blue, alpha);
        }
    }
}

void gsNativeDraw(const GsDrawState* st, const GsVertex* v, int nverts)
{
    unsigned int fbp = GSF(st->frame, 0, 9);
    unsigned int fbw = GSF(st->frame, 16, 6);
    unsigned int fbmask = GSF(st->frame, 32, 32);
    unsigned int scissor_x0 = GSF(st->scissor, 0, 11);
    unsigned int scissor_x1 = GSF(st->scissor, 16, 11);
    unsigned int scissor_y0 = GSF(st->scissor, 32, 11);
    unsigned int scissor_y1 = GSF(st->scissor, 48, 11);
    GsNativeTarget* target;
    int reason, x0, y0, x1, y1, i;

    native_stats.draws++;
    target = gsNativeGetTarget(fbp, fbw, (int)scissor_y1 + 1);
    if (target == NULL)
    {
        native_stats.skipped[GS_NATIVE_SKIP_TARGET]++;
        return;
    }

    reason = gsNativeSkipReason(st, nverts);
    if (reason != GS_NATIVE_SKIP_NONE)
    {
        native_stats.skipped[reason]++;
        return;
    }

    x0 = (int)scissor_x0;
    y0 = (int)scissor_y0;
    x1 = (int)scissor_x1;
    y1 = (int)scissor_y1;
    if (x1 >= target->width) x1 = target->width - 1;
    if (y1 >= target->height) y1 = target->height - 1;
    if (x1 < x0 || y1 < y0)
    {
        native_stats.skipped[GS_NATIVE_SKIP_PRIMITIVE]++;
        return;
    }

    switch (st->type)
    {
    case 0:
        for (i = 0; i < nverts; i++)
            gsNativeDrawPoint(target, st, &v[i], fbmask, x0, y0, x1, y1);
        break;
    case 1:
    case 2:
        for (i = 0; i + 1 < nverts; i += 2)
            gsNativeDrawLine(target, st, &v[i], &v[i + 1], fbmask, x0, y0, x1, y1);
        break;
    case 6:
        for (i = 0; i + 1 < nverts; i += 2)
            gsNativeDrawSprite(target, st, &v[i], &v[i + 1], fbmask, x0, y0, x1, y1);
        break;
    case 3:
    case 4:
    case 5:
        for (i = 0; i + 2 < nverts; i += 3)
            gsNativeDrawTriangle(target, st, &v[i], &v[i + 1], &v[i + 2],
                                 fbmask, x0, y0, x1, y1);
        break;
    }
    native_stats.rasterized++;
}

static int gsNativeDumpEnabled(void)
{
    const char* value;
    if (native_dump < 0)
    {
        value = getenv("RECVX_NATIVE_DUMP");
        native_dump = (value != NULL && strcmp(value, "1") == 0) ? 1 : 0;
    }
    return native_dump;
}

static int gsNativeWritePpm(const GsNativeTarget* target, const char* path)
{
    FILE* file;
    int x, y;

    file = fopen(path, "wb");
    if (file == NULL)
        return 0;
    fprintf(file, "P6\n640 480\n255\n");
    for (y = 0; y < 480; y++)
    {
        int sy = (y < target->height) ? y : target->height - 1;
        for (x = 0; x < 640; x++)
        {
            int sx = (x < target->width) ? x : target->width - 1;
            unsigned int pixel = target->pixels[(size_t)sy * target->width + sx];
            unsigned char rgb[3];

            rgb[0] = (unsigned char)pixel;
            rgb[1] = (unsigned char)(pixel >> 8);
            rgb[2] = (unsigned char)(pixel >> 16);
            if (fwrite(rgb, 1, sizeof(rgb), file) != sizeof(rgb))
            {
                fclose(file);
                return 0;
            }
        }
    }
    fclose(file);
    return 1;
}

void gsNativePresent(unsigned int dispfb_fbp, unsigned int dispfb_fbw)
{
    GsNativeTarget* target;

    native_frame++;
    if (native_frame == 1 || (native_frame % 300) == 0)
    {
        pcLog("native-ref: frame %u draws=%u rasterized=%u pixels=%llu skipped(tex=%u blend=%u fog/aa=%u tests=%u depth=%u format=%u target=%u primitive=%u)",
              native_frame, native_stats.draws, native_stats.rasterized, native_stats.pixels,
              native_stats.skipped[GS_NATIVE_SKIP_TEXTURE],
              native_stats.skipped[GS_NATIVE_SKIP_BLEND],
              native_stats.skipped[GS_NATIVE_SKIP_FOG_AA],
              native_stats.skipped[GS_NATIVE_SKIP_PIXEL_TEST],
              native_stats.skipped[GS_NATIVE_SKIP_DEPTH],
              native_stats.skipped[GS_NATIVE_SKIP_FORMAT],
              native_stats.skipped[GS_NATIVE_SKIP_TARGET],
              native_stats.skipped[GS_NATIVE_SKIP_PRIMITIVE]);
        memset(&native_stats, 0, sizeof(native_stats));
    }

    if (!gsNativeDumpEnabled())
        return;
    target = gsNativeFindDisplayTarget(dispfb_fbp, dispfb_fbw);
    if (target == NULL)
        return;
    if (!gsNativeWritePpm(target, "recvx-native-reference.ppm"))
        pcLog("native-ref: could not write recvx-native-reference.ppm");
#ifndef GS_NATIVE_TEST
    /* Captured immediately after the matching GL present, for same-present visual comparison. */
    pcGsScreenshot("recvx-gs-reference.png", dispfb_fbp);
#endif
}
