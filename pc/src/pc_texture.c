/* Native TIM2 asset loader. Decodes source image/CLUT data directly into linear RGBA
 * and uploads a standalone OpenGL texture; it has no GS VRAM, TEX0 packet or GIF dependency. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../../include/ps2/veronica/prog/ps2_dummy.h"
#include "../../include/ps2/veronica/prog/ps2_loadtim2.h"
#include "pc_renderer.h"
#include "pc_platform.h"

#define PC_TEX_CACHE_MAX 4096

typedef struct {
    unsigned int gindex, bank;
    uint64_t image_hash, clut_hash;
    unsigned short width, height;
    unsigned char image_type, clut_type, pict_format;
    unsigned int texture;
    int used;
} PcNativeTextureEntry;

static PcNativeTextureEntry texture_cache[PC_TEX_CACHE_MAX];

static const unsigned char block32[4][8] = {
    {0,1,4,5,16,17,20,21},{2,3,6,7,18,19,22,23},
    {8,9,12,13,24,25,28,29},{10,11,14,15,26,27,30,31}
};
static const unsigned char block4[8][4] = {
    {0,2,8,10},{1,3,9,11},{4,6,12,14},{5,7,13,15},
    {16,18,24,26},{17,19,25,27},{20,22,28,30},{21,23,29,31}
};
static const unsigned char col32[8][8] = {
    {0,1,4,5,8,9,12,13},{2,3,6,7,10,11,14,15},
    {16,17,20,21,24,25,28,29},{18,19,22,23,26,27,30,31},
    {32,33,36,37,40,41,44,45},{34,35,38,39,42,43,46,47},
    {48,49,52,53,56,57,60,61},{50,51,54,55,58,59,62,63}
};
static const unsigned char col8_base[8][16] = {
    {0,4,16,20,32,36,48,52,2,6,18,22,34,38,50,54},
    {8,12,24,28,40,44,56,60,10,14,26,30,42,46,58,62},
    {33,37,49,53,1,5,17,21,35,39,51,55,3,7,19,23},
    {41,45,57,61,9,13,25,29,43,47,59,63,11,15,27,31},
    {96,100,112,116,64,68,80,84,98,102,114,118,66,70,82,86},
    {104,108,120,124,72,76,88,92,106,110,122,126,74,78,90,94},
    {65,69,81,85,97,101,113,117,67,71,83,87,99,103,115,119},
    {73,77,89,93,105,109,121,125,75,79,91,95,107,111,123,127}
};
static const unsigned short col4_base[8][32] = {
    {0,8,32,40,64,72,96,104,2,10,34,42,66,74,98,106,4,12,36,44,68,76,100,108,6,14,38,46,70,78,102,110},
    {16,24,48,56,80,88,112,120,18,26,50,58,82,90,114,122,20,28,52,60,84,92,116,124,22,30,54,62,86,94,118,126},
    {65,73,97,105,1,9,33,41,67,75,99,107,3,11,35,43,69,77,101,109,5,13,37,45,71,79,103,111,7,15,39,47},
    {81,89,113,121,17,25,49,57,83,91,115,123,19,27,51,59,85,93,117,125,21,29,53,61,87,95,119,127,23,31,55,63},
    {192,200,224,232,128,136,160,168,194,202,226,234,130,138,162,170,196,204,228,236,132,140,164,172,198,206,230,238,134,142,166,174},
    {208,216,240,248,144,152,176,184,210,218,242,250,146,154,178,186,212,220,244,252,148,156,180,188,214,222,246,254,150,158,182,190},
    {129,137,161,169,193,201,225,233,131,139,163,171,195,203,227,235,133,141,165,173,197,205,229,237,135,143,167,175,199,207,231,239},
    {145,153,177,185,209,217,241,249,147,155,179,187,211,219,243,251,149,157,181,189,213,221,245,253,151,159,183,191,215,223,247,255}
};

static unsigned char col8[16][16];
static unsigned short col4[16][32];
static int tables_ready;

static void init_tables(void)
{
    int y, x;
    if (tables_ready) return;
    for (y = 0; y < 16; y++) {
        for (x = 0; x < 16; x++)
            col8[y][x] = (unsigned char)(col8_base[y & 7][x] + ((y & 8) ? 128 : 0));
        for (x = 0; x < 32; x++)
            col4[y][x] = (unsigned short)(col4_base[y & 7][x] + ((y & 8) ? 256 : 0));
    }
    tables_ready = 1;
}

static size_t addr32(unsigned int bw, unsigned int x, unsigned int y)
{
    unsigned int page = (y >> 5) * bw + (x >> 6);
    unsigned int block = page * 32 + block32[(y >> 3) & 3][(x >> 3) & 7];
    return ((size_t)block * 64u + col32[y & 7][x & 7]) * 4u;
}

static size_t addr8(unsigned int bw, unsigned int x, unsigned int y)
{
    unsigned int page = (y >> 6) * (bw >> 1) + (x >> 7);
    unsigned int block = page * 32 + block32[(y >> 4) & 3][(x >> 4) & 7];
    return (size_t)block * 256u + col8[y & 15][x & 15];
}

static size_t addr4(unsigned int bw, unsigned int x, unsigned int y)
{
    unsigned int page = (y >> 7) * (bw >> 1) + (x >> 7);
    unsigned int block = page * 32 + block4[(y >> 4) & 7][(x >> 5) & 3];
    return (size_t)block * 512u + col4[y & 15][x & 31];
}

static uint64_t hash_bytes(const unsigned char* p, size_t n)
{
    uint64_t h = UINT64_C(14695981039346656037);
    size_t i;
    for (i = 0; i < n; i++) h = (h ^ p[i]) * UINT64_C(1099511628211);
    return h;
}

static unsigned char clamp_byte(unsigned int x)
{
    return (unsigned char)(x > 255 ? 255 : x);
}

static void rgba32(const unsigned char* p, unsigned char* o)
{
    o[0] = p[0]; o[1] = p[1]; o[2] = p[2];
    o[3] = clamp_byte(((unsigned int)p[3] * 255u + 64u) / 128u);
}

static void color16(unsigned short v, unsigned char org_type, unsigned char* out)
{
    unsigned int r, g, b, a;
    if (org_type == 1) {
        r = (v & 31u) * 255u / 31u;
        g = ((v >> 5) & 63u) * 255u / 63u;
        b = ((v >> 11) & 31u) * 255u / 31u;
        a = 255;
    } else if (org_type == 2) {
        r = (v & 15u) * 17u;
        g = ((v >> 4) & 15u) * 17u;
        b = ((v >> 8) & 15u) * 17u;
        a = ((v >> 12) & 15u) * 17u;
    } else {
        r = (v & 31u) * 255u / 31u;
        g = ((v >> 5) & 31u) * 255u / 31u;
        b = ((v >> 10) & 31u) * 255u / 31u;
        a = (v & 0x8000u) ? 255u : 0u;
    }
    if (r == 0 && g == 0 && b == 0 && a != 0) a = 0;
    out[0] = clamp_byte(r); out[1] = clamp_byte(g); out[2] = clamp_byte(b);
    out[3] = (unsigned char)a;
}

static int build_palette(const TIM2_PICTUREHEADER* ph, const unsigned char* clut,
                         unsigned int bank, unsigned char org_type, unsigned char* rgba, unsigned int palette_capacity)
{
    unsigned int i, n = ph->ClutColors;
    unsigned int base = ((bank & 0x30u) / 4u) * 16u;
    unsigned int type = ph->ClutType;
    if (n == 0) n = (ph->ImageType == TIM2_IDTEX4) ? 16u : 256u;
    if (n > palette_capacity) n = palette_capacity;
    if (type == TIM2_NONE || clut == NULL) return 0;

    for (i = 0; i < n; i++) {
        unsigned char* out = rgba + (size_t)i * 4u;
        const unsigned char* src;
        if (type == TIM2_RGB32) {
            src = clut + (size_t)i * 4u;
            rgba32(src, out);
        } else if (type == TIM2_RGB24) {
            src = clut + (size_t)i * 3u;
            out[0] = src[0]; out[1] = src[1]; out[2] = src[2]; out[3] = 255;
        } else if (type == TIM2_RGB16) {
            unsigned short v;
            src = clut + (size_t)i * 2u;
            memcpy(&v, src, sizeof(v));
            color16(v, org_type, out);
        } else {
            return 0;
        }
    }
    if (base >= n) base = 0;
    if (base != 0) {
        for (i = 0; i < n && i + base < n; i++)
            memcpy(rgba + (size_t)i * 4u, rgba + (size_t)(i + base) * 4u, 4);
    }
    return (int)n;
}

static int decode_tim2(const TIM2_PICTUREHEADER_EX* ext, const TIM2_PICTUREHEADER* ph,
                       const unsigned char* image, const unsigned char* clut, unsigned int bank,
                       unsigned char* rgba)
{
    unsigned int w = ph->ImageWidth, h = ph->ImageHeight, x, y;
    unsigned int bw;
    size_t offset;
    unsigned char palette[256 * 4];
    unsigned int palette_count = 0;

    if (w == 0 || h == 0 || w > 2048 || h > 2048 || image == NULL) return 0;
    init_tables();

    if (ph->ImageType == TIM2_IDTEX4 || ph->ImageType == TIM2_IDTEX8) {
        palette_count = (unsigned int)build_palette(ph, clut, bank, ext->OrgColorType, palette,
                          ph->ImageType == TIM2_IDTEX4 ? 16u : 256u);
        if (palette_count == 0) return 0;
    }

    if (ph->ImageType == TIM2_RGB32) {
        bw = (w + 63u) / 64u;
        for (y = 0; y < h; y++) for (x = 0; x < w; x++) {
            offset = addr32(bw, x, y);
            if (offset + 4u > ph->ImageSize) return 0;
            rgba32(image + offset, rgba + ((size_t)y * w + x) * 4u);
        }
        return 1;
    }

    if (ph->ImageType == TIM2_IDTEX8) {
        bw = (w + 63u) / 64u;
        if (bw & 1u) bw++;
        for (y = 0; y < h; y++) for (x = 0; x < w; x++) {
            unsigned int index;
            offset = addr8(bw, x, y);
            if (offset >= ph->ImageSize) return 0;
            index = image[offset];
            if (index >= palette_count) index = 0;
            memcpy(rgba + ((size_t)y * w + x) * 4u, palette + (size_t)index * 4u, 4);
        }
        return 1;
    }

    if (ph->ImageType == TIM2_IDTEX4) {
        bw = (w + 63u) / 64u;
        if (bw & 1u) bw++;
        for (y = 0; y < h; y++) for (x = 0; x < w; x++) {
            unsigned int index;
            offset = addr4(bw, x, y);
            if ((offset >> 1) >= ph->ImageSize) return 0;
            index = image[offset >> 1];
            index = (offset & 1u) ? (index >> 4) : (index & 15u);
            if (index >= palette_count) index = 0;
            memcpy(rgba + ((size_t)y * w + x) * 4u, palette + (size_t)index * 4u, 4);
        }
        return 1;
    }
    (void)ext;
    return 0;
}

static PcNativeTextureEntry* find_entry(unsigned int gindex, unsigned int bank)
{
    unsigned int i;
    for (i = 0; i < PC_TEX_CACHE_MAX; i++)
        if (texture_cache[i].used && texture_cache[i].gindex == gindex &&
            texture_cache[i].bank == bank) return &texture_cache[i];
    return NULL;
}

static PcNativeTextureEntry* allocate_entry(void)
{
    unsigned int i;
    for (i = 0; i < PC_TEX_CACHE_MAX; i++)
        if (!texture_cache[i].used) return &texture_cache[i];
    return NULL;
}

int pcNativeTextureLoad(NJS_TEXMEMLIST* addr)
{
    TIM2_PICTUREHEADER_EX* ext;
    TIM2_PICTUREHEADER* ph;
    const unsigned char* image;
    const unsigned char* clut;
    unsigned char* rgba;
    size_t pixels, image_size, clut_size;
    uint64_t ih, ch;
    unsigned int gindex, bank;
    PcNativeTextureEntry* entry;
    unsigned int texture;
    int linear = 1, wrap_u = 1, wrap_v = 1;

    if (addr == NULL || addr->texinfo.texsurface.pSurface == NULL) {
        pcRendererBindTexture(0);
        pcLog("native texture: null TIM2 source");
        return 0;
    }

    ext = (TIM2_PICTUREHEADER_EX*)addr->texinfo.texsurface.pSurface;
    if (memcmp(ext->FileId, "TIM2", 4) != 0 || ext->FormatId > 1 ||
        (ext->PictFormat >= 5 && ext->PictFormat <= 8) || ext->TpFlag != 0) {
        pcRendererBindTexture(0);
        pcLog("native texture: invalid/unsupported TIM2 header (Gindex %u)", ext->Gindex);
        return 0;
    }

    ph = (TIM2_PICTUREHEADER*)((unsigned char*)ext + ((ext->FormatId != 0) ? 128 : 16));
    if (ph->HeaderSize < sizeof(TIM2_PICTUREHEADER) ||
        ph->ImageSize == 0 ||
        ph->ImageWidth == 0 || ph->ImageHeight == 0 ||
        ph->TotalSize < ph->HeaderSize ||
        ph->ImageSize > ph->TotalSize - ph->HeaderSize ||
        ph->ClutSize > ph->TotalSize - ph->HeaderSize - ph->ImageSize) {
        pcRendererBindTexture(0);
        pcLog("native texture: malformed TIM2 payload (Gindex %u)", ext->Gindex);
        return 0;
    }

    image = (const unsigned char*)ph + ph->HeaderSize;
    clut = image + ph->ImageSize;
    image_size = ph->ImageSize;
    clut_size = ph->ClutSize;
    ih = hash_bytes(image, image_size);
    ch = hash_bytes(clut, clut_size);
    gindex = ext->Gindex;
    bank = addr->bank;

    entry = find_entry(gindex, bank);
    if (entry && entry->image_hash == ih && entry->clut_hash == ch &&
        entry->width == ph->ImageWidth && entry->height == ph->ImageHeight &&
        entry->image_type == ph->ImageType && entry->clut_type == ph->ClutType &&
        entry->pict_format == ph->PictFormat) {
        texture = entry->texture;
    } else {
        pixels = (size_t)ph->ImageWidth * ph->ImageHeight;
        if (pixels > (size_t)2048 * 2048 || pixels > SIZE_MAX / 4u) {
            pcRendererBindTexture(0);
            return 0;
        }
        rgba = (unsigned char*)malloc(pixels * 4u);
        if (rgba == NULL) return 0;
        if (!decode_tim2(ext, ph, image, clut, bank, rgba)) {
            free(rgba);
            pcRendererBindTexture(0);
            pcLog("native texture: unsupported TIM2 image Gindex=%u type=%u CLUT=%u format=%u size=%ux%u",
                  gindex, ph->ImageType, ph->ClutType, ph->PictFormat, ph->ImageWidth, ph->ImageHeight);
            return 0;
        }
        texture = pcRendererLoadTextureRGBA(rgba, ph->ImageWidth, ph->ImageHeight, linear);
        free(rgba);
        if (texture == 0) {
            pcRendererBindTexture(0);
            return 0;
        }
        if (entry == NULL) entry = allocate_entry();
        if (entry == NULL) {
            pcRendererDeleteTexture(texture);
            pcRendererBindTexture(0);
            pcLog("native texture: cache full (Gindex=%u)", gindex);
            return 0;
        }
        if (entry->used && entry->texture != 0) pcRendererDeleteTexture(entry->texture);
        entry->used = 1;
        entry->gindex = gindex;
        entry->bank = bank;
        entry->image_hash = ih;
        entry->clut_hash = ch;
        entry->width = ph->ImageWidth;
        entry->height = ph->ImageHeight;
        entry->image_type = ph->ImageType;
        entry->clut_type = ph->ClutType;
        entry->pict_format = ph->PictFormat;
        entry->texture = texture;
    }

    /* ClampFlag carries GS WMS/WMT in its low nibbles. */
    if (ext->ClampFlag != 0) {
        wrap_u = (int)(ext->ClampFlag & 3u);
        wrap_v = (int)((ext->ClampFlag >> 2) & 3u);
    }
    pcRendererConfigureTexture(texture, linear, wrap_u, wrap_v);
    pcRendererBindTexture(texture);
    pcRendererSetTextureCombine((int)ph->GsTex0.TFX, (int)ph->GsTex0.TCC);
    return 1;
}
