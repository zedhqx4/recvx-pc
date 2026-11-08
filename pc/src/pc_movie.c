/* PC port: movie playback (replaces ps2_MovieFunc.c).
 *
 * The game drives movies through the Sofdec wrapper (ps2_sfd_mw.c), which calls the functions
 * below like the PS2 IPU player: initAll() when a movie starts, readMpeg() until a frame is
 * ready, setImageTag() to update its frame texture and vbrank_draw() to put it on screen, and
 * termAll() at the end. Frames come from MV_*.MPG (made by pc/tools/convert_movies.py) in the
 * extracted data folder and are decoded by pc_mpeg.c; the sprite is submitted with GS packets,
 * while the renderer samples the decoded frame from a dedicated OpenGL texture. Without converted movies (e.g. playing from the ISO) a movie reports
 * PREP -> PLAYING -> PLAYEND at once so the game moves on.
 *
 * Upscaled movies (MOVIE_HD/MV_*.MPG, made by recvx-config.exe's "Upscale movies") are used when
 * present and movies_hd= is not 0 in recvx.ini. Their frames are 4x the originals and do not fit
 * in GS memory: the game still draws its sprite as on the PS2, and the native renderer samples the upscaled frame directly.
 */
#include "../../include/ps2/veronica/prog/ps2_MovieFunc.h"
#include "../../include/ps2/veronica/prog/main.h"

#include "pc_platform.h"
#include "pc_renderer.h"

extern unsigned int Ps2_vcount;

RMI_WORK rmi;
MDSIZE_WORK mdSize;
int movie_draw;
StrFile infile;
u_long128 test_tag[1400];
VoBuf voBuf;

/* MWE_PLY_STAT */
#define MOVIE_STATE_PREP    1
#define MOVIE_STATE_PLAYING 2
#define MOVIE_STATE_END     3


int pcMpegOpen(const char* path);
void pcMpegClose(void);
void pcMpegPause(int on);
void pcMpegSetVolume(int vol);
int pcMpegUpdate(unsigned char* rgba, int max_bytes, int* w, int* h);
int pcMpegSize(int* w, int* h);
const char* pcCdDataDir(void);

static int playing;                 /* a converted movie is open */
static int image_w, image_h, image_new;
static unsigned char* image;        /* RGBA, 16 bytes of GIF tags in front */
static int image_cap = 512 * 512 * 4;
#define IMAGE_MAX image_cap
#define HD_SCALE 4                  /* upscaled movies are 4x the originals */
static int hd;                      /* playing an upscaled movie */
static int hd_w, hd_h;              /* its frame size */
static int movie_gl_active;         /* the dedicated native texture currently holds a movie frame */
static unsigned int movie_texture;
static int movie_texture_w, movie_texture_h;

void initAll()
{
    if (movie_texture != 0) {
        pcRendererDeleteTexture(movie_texture);
        movie_texture = 0;
        movie_gl_active = 0;
        movie_texture_w = movie_texture_h = 0;
    }

    rmi.iMovieState = MOVIE_STATE_PREP;
    rmi.iMovieFrame = Ps2_vcount;
    playing = 0;
}

static int file_exists(const char* path)
{
    FILE* f = fopen(path, "rb");

    if (f == NULL)
    {
        return 0;
    }

    fclose(f);
    return 1;
}

/* called by ps2mwPlyStartFname with the movie's name ("MV_000.PSS") */
void pcMovieStart(const char* name)
{
    const char* dir = pcCdDataDir();
    char path[1200];
    size_t n;

    if (dir == NULL)
    {
        pcLog("movie: %s skipped (movies need the extracted data folder, see pc/tools/convert_movies.py)", name);
        return;
    }

    /* the upscaled version first (unless turned off), then the converted original */
    hd = 0;
    snprintf(path, sizeof(path), "%s/MOVIE_HD/%s", dir, name);
    n = strlen(path);

    if ((n > 4) && (_stricmp(path + n - 4, ".PSS") == 0))
    {
        strcpy(path + n - 4, ".MPG");
    }

    if ((pcIniGetInt("movies_hd", 1) != 0) && file_exists(path) && pcMpegOpen(path))
    {
        hd = 1;
    }
    else
    {
        snprintf(path, sizeof(path), "%s/MOVIE/%s", dir, name);
        n = strlen(path);

        if ((n > 4) && (_stricmp(path + n - 4, ".PSS") == 0))
        {
            strcpy(path + n - 4, ".MPG");
        }
    }

    if (!hd && !pcMpegOpen(path))
    {
        pcLog("movie: %s not found, skipped (run pc/tools/convert_movies.py)", path);
        return;
    }

    {
        int w, h;

        if (pcMpegSize(&w, &h) && (w * h * 4 > image_cap))
        {
            free(image);
            image = NULL;
            image_cap = w * h * 4;
        }
    }

    if (image == NULL)
    {
        image = malloc(16 + IMAGE_MAX);
    }

    playing = 1;
    image_new = 0;
}

void readMpeg()
{
    int r;

    movie_draw = 1;

    if (!playing)
    {
        rmi.iMovieState = (rmi.iMovieState == MOVIE_STATE_PREP) ? MOVIE_STATE_PLAYING : MOVIE_STATE_END;
        return;
    }

    if (rmi.iMovieState == MOVIE_STATE_PREP)
    {
        rmi.iMovieState = MOVIE_STATE_PLAYING;
        rmi.iMovieFrame = Ps2_vcount;
    }

    pcMpegPause((rmi.uiContFlag & 0x2) != 0);

    r = pcMpegUpdate(image + 16, IMAGE_MAX, &image_w, &image_h);

    if ((r > 0) && hd)
    {
        /* the game's sprite keeps the original frame size; the renderer gets the big one */
        hd_w = image_w;
        hd_h = image_h;
        image_w /= HD_SCALE;
        image_h /= HD_SCALE;
    }

    if (r < 0)
    {
        rmi.iMovieState = MOVIE_STATE_END;
    }
    else if (r > 0)
    {
        image_new = 1;
    }
}

void termAll()
{
    if (playing)
    {
        pcMpegClose();
        playing = 0;
    }

    if (movie_texture != 0) {
        pcRendererDeleteTexture(movie_texture);
        movie_texture = 0;
        movie_gl_active = 0;
        movie_texture_w = movie_texture_h = 0;
    }

    hd = 0;
}

/* update the renderer's dedicated copy of the current frame */
void setImageTag(unsigned int* tags, void* unused)
{
    (void)tags;
    (void)unused;

    if (!playing || !image_new)
    {
        return;
    }

    image_new = 0;

    /* Movie frames are standalone native OpenGL assets. No GIF IMAGE transfer or GS texture slot. */
    if (movie_texture != 0) {
        pcRendererDeleteTexture(movie_texture);
        movie_texture = 0;
    }

    movie_texture_w = hd ? hd_w : image_w;
    movie_texture_h = hd ? hd_h : image_h;
    if (movie_texture_w <= 0 || movie_texture_h <= 0) {
        movie_gl_active = 0;
        return;
    }

    /* Decoder marks alpha 0x80 to match PS2 TEXA; movie presentation is opaque. */
    {
        unsigned char* rgba = image + 16;
        size_t count = (size_t)movie_texture_w * (size_t)movie_texture_h;
        size_t i;
        for (i = 0; i < count; i++)
            rgba[i * 4 + 3] = 255;
    }
    movie_texture = pcRendererLoadTextureRGBA(image + 16, movie_texture_w, movie_texture_h, 1);
    if (movie_texture == 0) {
        pcLog("movie: native OpenGL texture upload failed (%dx%d)", movie_texture_w, movie_texture_h);
        movie_gl_active = 0;
        return;
    }
    movie_gl_active = 1;
}

/* the original's sprite: decoded image (TBP 14208) stretched to the movie's display rectangle */
void vbrank_draw()
{
    PcNativeVertex vertices[6];
    PcNativeDrawState state;
    float k = pcVideoWideScale();
    float x0, x1, y0, y1;
    float uv_scale = hd ? (float)HD_SCALE : 1.0f;
    float u0, u1, v0, v1;
    static const unsigned char corner_map[6] = { 0, 1, 2, 0, 2, 3 };
    unsigned int i;

    if (!playing || !movie_gl_active || movie_texture == 0 || image_w == 0)
        return;

    /* The PS2 code sends positions in 1/16-pixel GS units with XYOFFSET already present. */
    x0 = (float)(32768 + (int)((27648 - 32768) * k)) / 16.0f - 1728.0f;
    x1 = (float)(32768 + (int)((27648 + (int)(mdSize.sWidth * 16.0f) - 32768) * k)) / 16.0f - 1728.0f;
    y0 = (float)(mdSize.sDispY + 1824);
    y1 = y0 + (float)mdSize.sHeight;
    y0 -= 1808.0f;
    y1 -= 1808.0f;

    /* Pixel-center sampling, scaled into the HD source when present. */
    u0 = 0.5f * uv_scale;
    v0 = 0.5f * uv_scale;
    u1 = ((float)image_w - 0.5f) * uv_scale;
    v1 = ((float)image_h - 0.5f) * uv_scale;

    for (i = 0; i < 6; i++) {
        unsigned int corner = corner_map[i];
        PcNativeVertex* v = &vertices[i];
        if (corner == 0) { v->x=x0; v->y=y0; v->u=u0; v->v=v0; }
        else if (corner == 1) { v->x=x1; v->y=y0; v->u=u1; v->v=v0; }
        else if (corner == 2) { v->x=x1; v->y=y1; v->u=u1; v->v=v1; }
        else { v->x=x0; v->y=y1; v->u=u0; v->v=v1; }
        v->z = 0.5f;
        v->r = v->g = v->b = v->a = 1.0f;
        v->q = 1.0f;
        v->fog = 1.0f;
    }

    memset(&state, 0, sizeof(state));
    state.texture = movie_texture;
    state.texture_width = (float)movie_texture_w;
    state.texture_height = (float)movie_texture_h;
    state.texture_function = 1; /* TEXF=DECAL */
    state.texture_alpha = 0;     /* movie RGB replaces framebuffer RGB; vertex alpha remains */
    state.textured = 1;
    state.blend = 0;
    state.depth_test = 0;
    state.depth_write = 0;
    state.alpha_test = 0;
    state.linear_filter = 1;
    state.screen_space = 1;
    state.texture_perspective = 0;
    pcRendererDrawScreenTriangles(vertices, 6, &state);
}

int sendToIOP(int dst, u_char* src, int size)
{
    return size;
}

/* 0 .. 32767 from ps2mwPlySetOutVol */
void changeInputVolume(u_int val)
{
    double lin = val / 32767.0;
    int vol = (lin <= 0.00002) ? -960 : (int)(200.0 * log10(lin));

    pcMpegSetVolume((vol < -960) ? -960 : (vol > 0) ? 0 : vol);
}
