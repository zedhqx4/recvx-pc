/* PC port, GS emulator: OpenGL 3.3 renderer.
 *
 * Render targets: every frame buffer the game draws to (FRAME.FBP) is an FBO, rendered at
 * pcGsScaleX x pcGsScale times the PS2 resolution (more horizontally for widescreen). A texture whose TBP0 is the address of a render target
 * samples that FBO (the PS2 copies its draw buffer to the display buffer this way). Other
 * textures are decoded from GS memory and cached until the pages they cover are written.
 *
 * Colors stay in GS units in the shader (0x80 = 1.0 for vertex colors and alpha); render target
 * alpha holds As / 128 so the GS blend factor As maps to GL_SRC_ALPHA.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "gs_internal.h"
#include "gl_funcs.h"
#include "../../include/pc_platform.h"
#include "../../include/pc_texture_cache.h"

/* Defined by gs.c; renderer timing is disabled unless RECVX_GS_TEX_DIAG=1. */
extern int gsTextureDiagnosticsEnabled(void);

int pcGsScale = 3;
float pcGsScaleX = 3.0f;
float pcGsScaleY = 3.0f;

static int gs_scale_x(int x)
{
    return (int)(x * pcGsScaleX + 0.5f);
}

static int gs_scale_y(int y)
{
    return (int)(y * pcGsScaleY + 0.5f);
}

#define PC_GL_DEFINE(ret, name, args) PFN_pc_##name pc_##name;
PC_GL_FUNCS(PC_GL_DEFINE)
#undef PC_GL_DEFINE

int pcGlLoad(void)
{
#define PC_GL_LOAD(ret, name, args) \
    pc_##name = (PFN_pc_##name)SDL_GL_GetProcAddress(#name); \
    if (pc_##name == NULL) { pcLog("OpenGL: missing %s", #name); return 0; }
    PC_GL_FUNCS(PC_GL_LOAD)
#undef PC_GL_LOAD
    return 1;
}

/* Redundant OpenGL state changes are expensive on the CPU-side driver path.
 * Keep this cache in one place and route every state change in this renderer
 * through the wrappers below. Unknown state always falls back to OpenGL. */
#define PC_GS_TEXTURE_UNITS_TRACKED 32

typedef struct {
    int valid;
    GLuint value;
} PcGsGlObjectState;

typedef struct {
    int read_fbo_valid, draw_fbo_valid;
    GLuint read_fbo, draw_fbo;

    int viewport_valid;
    GLint viewport[4];

    int scissor_valid;
    GLint scissor[4];

    int depth_func_valid;
    GLenum depth_func;
    int depth_mask_valid;
    GLboolean depth_mask;

    int color_mask_valid;
    GLboolean color_mask[4];

    int blend_color_valid;
    GLfloat blend_color[4];
    int blend_equation_valid;
    GLenum blend_equation_rgb, blend_equation_alpha;
    int blend_func_valid;
    GLenum blend_src_rgb, blend_dst_rgb, blend_src_alpha, blend_dst_alpha;

    unsigned char cap_valid[3];
    unsigned char cap_enabled[3];

    int program_valid;
    GLuint program;
    int vao_valid;
    GLuint vao;
    int array_buffer_valid;
    GLuint array_buffer;

    int active_texture_valid;
    GLenum active_texture;
    unsigned char texture_2d_valid[PC_GS_TEXTURE_UNITS_TRACKED];
    GLuint texture_2d[PC_GS_TEXTURE_UNITS_TRACKED];
} PcGsGlState;

static PcGsGlState pcGsGlState;

static void pcGsGlStateResetCache(void)
{
    memset(&pcGsGlState, 0, sizeof(pcGsGlState));
}

static int pcGsGlCapIndex(GLenum cap)
{
    switch (cap)
    {
    case GL_BLEND: return 0;
    case GL_DEPTH_TEST: return 1;
    case GL_SCISSOR_TEST: return 2;
    default: return -1;
    }
}

static int pcGsGlTextureUnitIndex(GLenum unit)
{
    if ((unit < GL_TEXTURE0) || (unit >= GL_TEXTURE0 + PC_GS_TEXTURE_UNITS_TRACKED))
        return -1;
    return (int)(unit - GL_TEXTURE0);
}

static void pcGsBindFramebuffer(GLenum target, GLuint fbo)
{
    int skip = 0;

    if (target == GL_FRAMEBUFFER)
    {
        skip = pcGsGlState.read_fbo_valid && pcGsGlState.draw_fbo_valid &&
               (pcGsGlState.read_fbo == fbo) && (pcGsGlState.draw_fbo == fbo);
    }
    else if (target == GL_READ_FRAMEBUFFER)
    {
        skip = pcGsGlState.read_fbo_valid && (pcGsGlState.read_fbo == fbo);
    }
    else if (target == GL_DRAW_FRAMEBUFFER)
    {
        skip = pcGsGlState.draw_fbo_valid && (pcGsGlState.draw_fbo == fbo);
    }

    if (skip)
        return;

    glBindFramebuffer(target, fbo);

    if (target == GL_FRAMEBUFFER)
    {
        pcGsGlState.read_fbo_valid = pcGsGlState.draw_fbo_valid = 1;
        pcGsGlState.read_fbo = pcGsGlState.draw_fbo = fbo;
    }
    else if (target == GL_READ_FRAMEBUFFER)
    {
        pcGsGlState.read_fbo_valid = 1;
        pcGsGlState.read_fbo = fbo;
    }
    else if (target == GL_DRAW_FRAMEBUFFER)
    {
        pcGsGlState.draw_fbo_valid = 1;
        pcGsGlState.draw_fbo = fbo;
    }
    else
    {
        pcGsGlState.read_fbo_valid = pcGsGlState.draw_fbo_valid = 0;
    }
}

static void pcGsDeleteFramebuffers(GLsizei n, const GLuint* framebuffers)
{
    glDeleteFramebuffers(n, framebuffers);
    /* A deleted framebuffer name can be reused by GL; don't trust stale names. */
    pcGsGlState.read_fbo_valid = pcGsGlState.draw_fbo_valid = 0;
}

static void pcGsViewport(GLint x, GLint y, GLsizei w, GLsizei h)
{
    GLint value[4] = { x, y, w, h };
    if (pcGsGlState.viewport_valid && memcmp(pcGsGlState.viewport, value, sizeof(value)) == 0)
        return;

    glViewport(x, y, w, h);
    memcpy(pcGsGlState.viewport, value, sizeof(value));
    pcGsGlState.viewport_valid = 1;
}

static void pcGsScissor(GLint x, GLint y, GLsizei w, GLsizei h)
{
    GLint value[4] = { x, y, w, h };
    if (pcGsGlState.scissor_valid && memcmp(pcGsGlState.scissor, value, sizeof(value)) == 0)
        return;

    glScissor(x, y, w, h);
    memcpy(pcGsGlState.scissor, value, sizeof(value));
    pcGsGlState.scissor_valid = 1;
}

static void pcGsSetCapability(GLenum cap, int enabled)
{
    int index = pcGsGlCapIndex(cap);

    if (index < 0)
    {
        if (enabled) glEnable(cap); else glDisable(cap);
        return;
    }

    if (pcGsGlState.cap_valid[index] && pcGsGlState.cap_enabled[index] == (unsigned char)enabled)
        return;

    if (enabled) glEnable(cap); else glDisable(cap);
    pcGsGlState.cap_valid[index] = 1;
    pcGsGlState.cap_enabled[index] = (unsigned char)enabled;
}

static void pcGsEnable(GLenum cap)  { pcGsSetCapability(cap, 1); }
static void pcGsDisable(GLenum cap) { pcGsSetCapability(cap, 0); }

static void pcGsDepthFunc(GLenum func)
{
    if (pcGsGlState.depth_func_valid && pcGsGlState.depth_func == func)
        return;
    glDepthFunc(func);
    pcGsGlState.depth_func_valid = 1;
    pcGsGlState.depth_func = func;
}

static void pcGsDepthMask(GLboolean mask)
{
    if (pcGsGlState.depth_mask_valid && pcGsGlState.depth_mask == mask)
        return;
    glDepthMask(mask);
    pcGsGlState.depth_mask_valid = 1;
    pcGsGlState.depth_mask = mask;
}

static void pcGsColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a)
{
    GLboolean value[4] = { r, g, b, a };
    if (pcGsGlState.color_mask_valid && memcmp(pcGsGlState.color_mask, value, sizeof(value)) == 0)
        return;
    glColorMask(r, g, b, a);
    memcpy(pcGsGlState.color_mask, value, sizeof(value));
    pcGsGlState.color_mask_valid = 1;
}

static void pcGsBlendColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
    GLfloat value[4] = { r, g, b, a };
    if (pcGsGlState.blend_color_valid && memcmp(pcGsGlState.blend_color, value, sizeof(value)) == 0)
        return;
    glBlendColor(r, g, b, a);
    memcpy(pcGsGlState.blend_color, value, sizeof(value));
    pcGsGlState.blend_color_valid = 1;
}

static void pcGsBlendEquationSeparate(GLenum rgb, GLenum alpha)
{
    if (pcGsGlState.blend_equation_valid &&
        pcGsGlState.blend_equation_rgb == rgb && pcGsGlState.blend_equation_alpha == alpha)
        return;
    glBlendEquationSeparate(rgb, alpha);
    pcGsGlState.blend_equation_valid = 1;
    pcGsGlState.blend_equation_rgb = rgb;
    pcGsGlState.blend_equation_alpha = alpha;
}

static void pcGsBlendFuncSeparate(GLenum src_rgb, GLenum dst_rgb, GLenum src_alpha, GLenum dst_alpha)
{
    if (pcGsGlState.blend_func_valid &&
        pcGsGlState.blend_src_rgb == src_rgb && pcGsGlState.blend_dst_rgb == dst_rgb &&
        pcGsGlState.blend_src_alpha == src_alpha && pcGsGlState.blend_dst_alpha == dst_alpha)
        return;
    glBlendFuncSeparate(src_rgb, dst_rgb, src_alpha, dst_alpha);
    pcGsGlState.blend_func_valid = 1;
    pcGsGlState.blend_src_rgb = src_rgb;
    pcGsGlState.blend_dst_rgb = dst_rgb;
    pcGsGlState.blend_src_alpha = src_alpha;
    pcGsGlState.blend_dst_alpha = dst_alpha;
}

static void pcGsUseProgram(GLuint program)
{
    if (pcGsGlState.program_valid && pcGsGlState.program == program)
        return;
    glUseProgram(program);
    pcGsGlState.program_valid = 1;
    pcGsGlState.program = program;
}

static void pcGsBindVertexArray(GLuint vao)
{
    if (pcGsGlState.vao_valid && pcGsGlState.vao == vao)
        return;
    glBindVertexArray(vao);
    pcGsGlState.vao_valid = 1;
    pcGsGlState.vao = vao;
}

static void pcGsBindBuffer(GLenum target, GLuint buffer)
{
    if (target == GL_ARRAY_BUFFER)
    {
        if (pcGsGlState.array_buffer_valid && pcGsGlState.array_buffer == buffer)
            return;
        glBindBuffer(target, buffer);
        pcGsGlState.array_buffer_valid = 1;
        pcGsGlState.array_buffer = buffer;
        return;
    }
    glBindBuffer(target, buffer);
}

static void pcGsActiveTexture(GLenum unit)
{
    if (pcGsGlState.active_texture_valid && pcGsGlState.active_texture == unit)
        return;
    glActiveTexture(unit);
    pcGsGlState.active_texture_valid = 1;
    pcGsGlState.active_texture = unit;
}

static void pcGsBindTexture(GLenum target, GLuint texture)
{
    int unit = pcGsGlState.active_texture_valid ? pcGsGlTextureUnitIndex(pcGsGlState.active_texture) : -1;

    if ((target == GL_TEXTURE_2D) && (unit >= 0))
    {
        if (pcGsGlState.texture_2d_valid[unit] && pcGsGlState.texture_2d[unit] == texture)
            return;
        glBindTexture(target, texture);
        pcGsGlState.texture_2d_valid[unit] = 1;
        pcGsGlState.texture_2d[unit] = texture;
        return;
    }

    glBindTexture(target, texture);
}

static void pcGsDeleteTextures(GLsizei n, const GLuint* textures)
{
    glDeleteTextures(n, textures);
    /* Deletion unbinds matching textures and may allow the numeric name to be reused. */
    memset(pcGsGlState.texture_2d_valid, 0, sizeof(pcGsGlState.texture_2d_valid));
}

/* gs_replace.c uses raw OpenGL texture calls. Invalidate its side effects before
 * returning to the renderer's cached binding path. */
static void pcGsInvalidateTextureBindings(void)
{
    memset(pcGsGlState.texture_2d_valid, 0, sizeof(pcGsGlState.texture_2d_valid));
}

/* ------------------------------------------------------------------------------------------- */
/* shaders */

static const char* vs_src =
    "#version 330 core\n"
    "layout(location = 0) in vec3 aPos;\n"      /* x, y in pixels, z 0..1 */
    "layout(location = 1) in vec4 aColor;\n"    /* GS color / 255 */
    "layout(location = 2) in vec3 aSTQ;\n"
    "layout(location = 3) in vec2 aUV;\n"
    "layout(location = 4) in float aFog;\n"
    "layout(location = 5) in vec4 aClamp;\n"   /* texels a 2D piece may sample: u0, v0, u1, v1 */
    "uniform vec2 uTarget;\n"                   /* render target size in PS2 pixels */
    "out vec4 vColor;\n"
    "flat out vec4 vFlatColor;\n"
    "noperspective out vec3 vSTQ;\n"
    "noperspective out vec2 vUV;\n"
    "noperspective out float vFog;\n"
    "flat out vec4 vClamp;\n"
    "void main() {\n"
    "    gl_Position = vec4(aPos.xy / uTarget * 2.0 - 1.0, aPos.z * 2.0 - 1.0, 1.0);\n"
    "    vColor = aColor; vFlatColor = aColor; vSTQ = aSTQ; vUV = aUV; vFog = aFog; vClamp = aClamp;\n"
    "}\n";

static const char* fs_src =
    "#version 330 core\n"
    "in vec4 vColor;\n"
    "flat in vec4 vFlatColor;\n"
    "noperspective in vec3 vSTQ;\n"
    "noperspective in vec2 vUV;\n"
    "noperspective in float vFog;\n"
    "flat in vec4 vClamp;\n"
    "uniform sampler2D uTex;\n"
    "uniform sampler2D uPalette;\n"
    "uniform int uIndexed;\n"
    "uniform int uTme, uFst, uTfx, uTcc, uIip;\n"
    "uniform int uWireframe;\n"
    "uniform vec3 uTexa;\n"                    /* TA0, TA1, AEM */
    "uniform int uHD;\n"                      /* replaced (HD) texture: filtered, any size */
    "uniform int uTexRT;\n"                     /* 1: texture is a render target */
    "uniform vec2 uTexSize;\n"                  /* 2^TW, 2^TH */
    "uniform vec2 uRTSize;\n"                   /* render target logical size (RT textures) */
    "uniform ivec2 uWrap;\n"
    "uniform ivec4 uRegion;\n"                  /* minu, maxu, minv, maxv */
    "uniform int uLinear;\n"
    "uniform int uAte, uAtst, uAref, uAtestInvert;\n"
    "uniform int uFge;\n"
    "uniform vec3 uFogCol;\n"
    "out vec4 oColor;\n"
    "int wrapc(int c, int size, int mode, int mn, int mx) {\n"
    "    if (mode == 0) return c & (size - 1);\n"
    "    if (mode == 1) return clamp(c, 0, size - 1);\n"
    "    if (mode == 2) return clamp(c, mn, mx);\n"
    "    return (c & mn) | mx;\n"
    "}\n"
    "vec4 fetch(ivec2 c) {\n"
    "    ivec2 sz = ivec2(uTexSize);\n"
    "    c.x = wrapc(c.x, sz.x, uWrap.x, uRegion.x, uRegion.y);\n"
    "    c.y = wrapc(c.y, sz.y, uWrap.y, uRegion.z, uRegion.w);\n"
    "    if (uIndexed != 0) {\n"
    "        int index = int(floor(texelFetch(uTex, c, 0).r * 255.0 + 0.5));\n"
    "        return texelFetch(uPalette, ivec2(index, 0), 0);\n"
    "    }\n"
    "    return texelFetch(uTex, c, 0);\n"
    "}\n"
    "vec4 sampleTex(vec2 uv) {\n"
    "    if (uHD != 0) {\n"
    "        return texture(uTex, uv / uTexSize) * 255.0;\n"
    "    }\n"
    "    if (uTexRT == 2) {\n"
    "        vec4 t = texture(uTex, uv / uRTSize);\n"
    "        vec3 c = floor(t.rgb * 31.0 + 0.5) * 8.0;\n"
    "        float a = (t.a * 128.0 >= 127.5) ? uTexa.y : ((uTexa.z != 0.0 && c == vec3(0.0)) ? 0.0 : uTexa.x);\n"
    "        return vec4(c, a);\n"
    "    }\n"
    "    if (uTexRT != 0) {\n"
    "        vec4 t = texture(uTex, uv / uRTSize);\n"
    "        return vec4(t.rgb * 255.0, t.a * 128.0);\n"
    "    }\n"
    "    vec4 t;\n"
    "    if (uLinear != 0) {\n"
    "        vec2 p = uv - 0.5; ivec2 i = ivec2(floor(p)); vec2 f = fract(p);\n"
    "        t = mix(mix(fetch(i), fetch(i + ivec2(1, 0)), f.x), mix(fetch(i + ivec2(0, 1)), fetch(i + ivec2(1, 1)), f.x), f.y);\n"
    "    } else {\n"
    "        t = fetch(ivec2(floor(uv)));\n"
    "    }\n"
    "    return t * 255.0;\n"
    "}\n"
    "bool atest(float a) {\n"
    "    int ia = int(a);\n"
    "    bool r = true;\n"
    "    if (uAtst == 0) r = false; else if (uAtst == 2) r = ia < uAref; else if (uAtst == 3) r = ia <= uAref;\n"
    "    else if (uAtst == 4) r = ia == uAref; else if (uAtst == 5) r = ia >= uAref; else if (uAtst == 6) r = ia > uAref;\n"
    "    else if (uAtst == 7) r = ia != uAref;\n"
    "    return r;\n"
    "}\n"
    "void main() {\n"
    "    if (uWireframe != 0) { oColor = vec4(0.0, 1.0, 1.0, 1.0); return; }\n"
    "    vec4 vc = (uIip != 0 ? vColor : vFlatColor) * 255.0;\n"
    "    vec3 rgb = vc.rgb; float a = vc.a;\n"
    "    if (uTme != 0) {\n"
    "        vec2 uv = (uFst != 0) ? clamp(vUV, vClamp.xy, vClamp.zw) : (vSTQ.xy / vSTQ.z) * uTexSize;\n"
    "        vec4 t = sampleTex(uv);\n"
    "        if (uTfx == 0) { rgb = t.rgb * vc.rgb / 128.0; a = (uTcc != 0) ? t.a * vc.a / 128.0 : vc.a; }\n"
    "        else if (uTfx == 1) { rgb = t.rgb; a = (uTcc != 0) ? t.a : vc.a; }\n"
    "        else if (uTfx == 2) { rgb = t.rgb * vc.rgb / 128.0 + vc.a; a = (uTcc != 0) ? t.a + vc.a : vc.a; }\n"
    "        else { rgb = t.rgb * vc.rgb / 128.0 + vc.a; a = (uTcc != 0) ? t.a : vc.a; }\n"
    "    }\n"
    "    rgb = clamp(floor(rgb), 0.0, 255.0); a = clamp(floor(a), 0.0, 255.0);\n"
    "    if (uFge != 0) { float f = vFog * 255.0; rgb = floor((f * rgb + (255.0 - f) * uFogCol) / 256.0); }\n"
    "    if (uAte != 0) { bool pass = atest(a); if (pass == (uAtestInvert != 0)) discard; }\n"
    "    oColor = vec4(rgb / 255.0, min(a / 128.0, 1.0));\n"
    "}\n";

static const char* present_vs =
    "#version 330 core\n"
    "layout(location = 0) in vec3 aPos;\n"
    "layout(location = 2) in vec3 aSTQ;\n"
    "out vec2 vTC;\n"
    "void main() { gl_Position = vec4(aPos.xy, 0.0, 1.0); vTC = aSTQ.xy; }\n";

static const char* present_fs =
    "#version 330 core\n"
    "in vec2 vTC; uniform sampler2D uTex; out vec4 oColor;\n"
    "void main() { oColor = vec4(texture(uTex, vTC).rgb, 1.0); }\n";

static GLuint prog, present_prog;
static GLuint vao, vbo;

static struct {
    GLint target, tme, fst, tfx, tcc, iip, texrt, texsize, rtsize, wrap, region, linear;
    GLint ate, atst, aref, atinv, fge, fogcol, tex, palette, indexed, texa, hd, wireframe;
} U;

static GLuint compile(GLenum type, const char* src)
{
    GLuint s = glCreateShader(type);
    GLint ok = 0;

    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);

    if (!ok)
    {
        char log[2048];

        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        pcPlatformFatal("Shader compile error:\n%s", log);
    }

    return s;
}

static GLuint link(const char* vs, const char* fs)
{
    GLuint p = glCreateProgram();
    GLint ok = 0;

    glAttachShader(p, compile(GL_VERTEX_SHADER, vs));
    glAttachShader(p, compile(GL_FRAGMENT_SHADER, fs));
    glLinkProgram(p);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);

    if (!ok)
    {
        char log[2048];

        glGetProgramInfoLog(p, sizeof(log), NULL, log);
        pcPlatformFatal("Shader link error:\n%s", log);
    }

    return p;
}

/* ------------------------------------------------------------------------------------------- */
/* vertex stream */

typedef struct {
    float x, y, z;
    unsigned char r, g, b, a;
    float s, t, q;
    float u, v;
    float fog;
    float cu0, cv0, cu1, cv1;   /* sampling limits in texels (GsVertex) */
} GlVertex;

#define MAX_GL_VERTS 65536

static GlVertex glverts[MAX_GL_VERTS];

/* ------------------------------------------------------------------------------------------- */
/* render targets */

typedef struct {
    int used;
    unsigned int fbp, fbw;
    int w, h;              /* PS2 pixels */
    GLuint fbo, color;
    GLuint depth_attached;
    int vram_dirty;        /* GS memory was written inside this buffer */
    int rendered;          /* drawn into since it last matched GS memory */
    int drawn_h;           /* rows (PS2 pixels) drawn into since then */
    int load_y0, load_y1;  /* rows below those that GS memory changed in, to reload alone */
    unsigned int psm;      /* FRAME.PSM last drawn with */
    unsigned int last_use;
} RenderTarget;

typedef struct {
    int used;
    unsigned int zbp;
    unsigned int psm;       /* GS ZBUF.PSM: preserve native depth precision */
    int w, h;
    GLuint tex;
} DepthBuffer;

#define MAX_RT 16
#define MAX_DB 8

static RenderTarget rts[MAX_RT];
static DepthBuffer dbs[MAX_DB];
static unsigned int frame_counter;
static int dump_frame;
static unsigned int dump_draw_index;
static int ui_wireframe;

static int gs_dump_3d_only_enabled(void)
{
    static int enabled = -1;

    if (enabled < 0)
    {
        const char* value = getenv("RECVX_GSDUMP_3D");
        enabled = (value != NULL && value[0] == '1' && value[1] == '\0') ? 1 : 0;
    }

    return enabled;
}

int pcGsToggleUiWireframe(void)
{
    ui_wireframe = !ui_wireframe;
    pcLog("gs: UI wireframe overlay %s", ui_wireframe ? "enabled" : "disabled");
    return ui_wireframe;
}


/* per-interval statistics for the periodic log */
static struct {
    unsigned int frames, draws, verts, rt_loads, rt_reads, tex_decodes, tex_texels;
    double t_load, t_read, t_decode;
    double t_pre, t_geom, t_setup, t_upload, t_submit, t_draw;
} stats;
static int tex_cached;                     /* textures in the cache, decoded from GS memory */
static double tex_cached_bytes;            /* their size */

double pcOsTime(void);

/* Detailed CPU-stage profiling is opt-in; it stays off in normal play. */
static int gs_perf_timing_enabled(void)
{
    static int enabled = -1;

    if (enabled < 0)
    {
        enabled = pcIniGetInt("debug", 0) != 0;
    }

    return enabled;
}

void pcGsLogStats(void)
{
    unsigned int f = stats.frames ? stats.frames : 1;

    pcLog("gs/frame: %u draws, %u verts, %.1f rt loads (%.1f ms), %.1f rt reads (%.1f ms), %.1f tex decodes (%u texels, %.1f ms)",
          stats.draws / f, stats.verts / f, (double)stats.rt_loads / f, stats.t_load * 1000 / f, (double)stats.rt_reads / f,
          stats.t_read * 1000 / f, (double)stats.tex_decodes / f, stats.tex_texels / f, stats.t_decode * 1000 / f);
    pcLog("gs: %d textures cached (%.0f MB)", tex_cached, tex_cached_bytes / 1048576.0);
    if (gs_perf_timing_enabled())
    {
        pcLog("gs CPU/present: pre %.2f ms, geometry %.2f ms, GL setup %.2f ms, buffer upload %.2f ms, draw/restore %.2f ms, total gs draw CPU %.2f ms",
              stats.t_pre * 1000 / f, stats.t_geom * 1000 / f, stats.t_setup * 1000 / f,
              stats.t_upload * 1000 / f, stats.t_submit * 1000 / f, stats.t_draw * 1000 / f);
    }
    memset(&stats, 0, sizeof(stats));
}

static GLuint make_texture(int w, int h, GLint ifmt, GLenum fmt, GLenum type, GLint filter)
{
    GLuint t;

    glGenTextures(1, &t);
    pcGsBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, ifmt, w, h, 0, fmt, type, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    return t;
}

static void rt_load_from_vram(RenderTarget* rt);

static RenderTarget* find_rt(unsigned int fbp)
{
    int i;

    for (i = 0; i < MAX_RT; i++)
    {
        if (rts[i].used && (rts[i].fbp == fbp))
        {
            return &rts[i];
        }
    }

    return NULL;
}

static RenderTarget* get_rt(unsigned int fbp, unsigned int fbw)
{
    RenderTarget* rt = find_rt(fbp);
    int i, oldest = 0;

    if (fbw == 0)
    {
        fbw = 10;
    }

    if ((rt != NULL) && (rt->fbw == fbw))
    {
        return rt;
    }

    if (rt == NULL)
    {
        for (i = 0; i < MAX_RT; i++)
        {
            if (!rts[i].used)
            {
                break;
            }

            if (rts[i].last_use < rts[oldest].last_use)
            {
                oldest = i;
            }
        }

        rt = &rts[(i < MAX_RT) ? i : oldest];
    }

    if (rt->used)
    {
        pcGsDeleteFramebuffers(1, &rt->fbo);
        pcGsDeleteTextures(1, &rt->color);
    }

    memset(rt, 0, sizeof(*rt));
    rt->used = 1;
    rt->fbp = fbp;
    rt->fbw = fbw;
    rt->w = fbw * 64;
    rt->h = 512;
    rt->color = make_texture(gs_scale_x(rt->w), gs_scale_y(rt->h), GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR);

    glGenFramebuffers(1, &rt->fbo);
    pcGsBindFramebuffer(GL_FRAMEBUFFER, rt->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rt->color, 0);

    /* start from whatever GS memory holds there (normally cleared) */
    rt_load_from_vram(rt);

    pcLog("gs: render target FBP %u (%ux%u)", fbp, rt->w, rt->h);

    return rt;
}

static int gs_force_depth32f_enabled(void)
{
    static int enabled = -1;

    if (enabled < 0)
    {
        const char* value = getenv("RECVX_GS_DEPTH32F");
        enabled = (value != NULL && value[0] == '1' && value[1] == '\0') ? 1 : 0;
    }

    return enabled;
}

static DepthBuffer* get_db(unsigned int zbp, int psm, int w, int h)
{
    int i;
    GLint internal_format;
    GLenum type;

    /* Match the GS depth-buffer precision instead of quantizing every Z format to D24.
     * In particular, Z32 must not lose eight bits before depth comparisons. */
    if (psm == 0)
    {
        internal_format = GL_DEPTH_COMPONENT32;
        type = GL_UNSIGNED_INT;
    }
    else if (psm == 1)
    {
        internal_format = GL_DEPTH_COMPONENT24;
        type = GL_UNSIGNED_INT;
    }
    else
    {
        internal_format = GL_DEPTH_COMPONENT16;
        type = GL_UNSIGNED_SHORT;
    }

    /* A/B diagnostic: test depth-buffer quantization without changing GS Z or ZTST. */
    if (gs_force_depth32f_enabled())
    {
        internal_format = GL_DEPTH_COMPONENT32F;
        type = GL_FLOAT;
    }

    for (i = 0; i < MAX_DB; i++)
    {
        if (dbs[i].used && (dbs[i].zbp == zbp) && (dbs[i].psm == psm) &&
            (dbs[i].w == w) && (dbs[i].h == h))
        {
            return &dbs[i];
        }
    }

    for (i = 0; i < MAX_DB; i++)
    {
        if (!dbs[i].used)
        {
            dbs[i].used = 1;
            dbs[i].zbp = zbp;
            dbs[i].psm = psm;
            dbs[i].w = w;
            dbs[i].h = h;
            dbs[i].tex = make_texture(gs_scale_x(w), gs_scale_y(h), internal_format, GL_DEPTH_COMPONENT,
                                      type, GL_NEAREST);
            if (gs_force_depth32f_enabled())
                pcLog("gs: depth test override: ZBP=%u PSM=%u -> GL_DEPTH_COMPONENT32F", zbp, psm);
            return &dbs[i];
        }
    }

    /* Keep the existing bounded fallback if all depth-buffer slots are already occupied. */
    return &dbs[0];
}
/* ------------------------------------------------------------------------------------------- */
/* texture cache (textures decoded from GS memory) */

typedef struct {
    int used;
    u64 key0, key1, key2;
    int w, h;
    int rows, cols;      /* part decoded: the draws it serves never sample outside it */
    unsigned int page_lo, page_hi, clut_page;
    unsigned long long gen;
    GLuint tex;
    int hd;              /* tex is an HD replacement (owned by gs_replace.c) */
    int indexed;          /* tex is a palette-independent GL_R8 index atlas */
    int index_slot;       /* owning entry in indexed_atlases when indexed */
    unsigned int last_use;
    int hash_next;
} CachedTex;

#define MAX_TEX 2048

static CachedTex texcache[MAX_TEX];
static unsigned int* decode_buf;
static unsigned char* index_buf;

#define MAX_INDEXED_ATLASES 128
typedef struct {
    int used;
    u64 key0;
    int w, h;
    unsigned long long gen;
    GLuint tex;
    unsigned int last_use;
} CachedIndexedAtlas;

static CachedIndexedAtlas indexed_atlases[MAX_INDEXED_ATLASES];

#define MAX_PALETTE_TEX 256
typedef struct {
    int used;
    int entries;
    unsigned long long hash;   /* raw CLUT content hash from gsClutHash */
    u64 texa_key;
    unsigned int cpsm;
    GLuint tex;
    unsigned int last_use;
} CachedPaletteTex;

static CachedPaletteTex palette_texcache[MAX_PALETTE_TEX];

/* A hit should not scan all 2048 cached textures. Use bucket chains for exact-key
 * lookup; keep the existing LRU scan only on cache misses/evictions. */
#define TEX_HASH_BUCKETS 4096

static int texcache_hash_heads[TEX_HASH_BUCKETS];

#define ASSET_HASH_BUCKETS 2048
#define MAX_ASSET_ALIASES 4096
typedef struct {
    int used;
    unsigned int asset_id;
    uint64_t content_hash;
    int texture_slot;
    int next;
} CachedAssetAlias;
static CachedAssetAlias texture_asset_aliases[MAX_ASSET_ALIASES];
static int texture_asset_hash_heads[ASSET_HASH_BUCKETS];
static unsigned int texture_asset_next_free;

/* Per-present interval diagnostics: only emitted for texture misses or heavy CLUT flushing. */
static struct {
    unsigned int hits, misses, atlas_hits, atlas_misses, palette_hits, palette_misses, decoded, texels;
    double decode_seconds, create_seconds, upload_seconds, replacement_seconds;
    double palette_create_seconds, palette_upload_seconds;
    unsigned int asset_hits;
} texFrameStats;

static unsigned int texture_asset_hash(unsigned int asset_id)
{
    unsigned int value = asset_id;
    value ^= value >> 11;
    value *= 0x9e3779b1U;
    value ^= value >> 13;
    return value & (ASSET_HASH_BUCKETS - 1);
}


static void texture_cache_asset_remove(int slot)
{
    unsigned int bucket;

    /* The same asset ID may map to multiple renderer cache slots with distinct layouts. */
    for (bucket = 0; bucket < ASSET_HASH_BUCKETS; bucket++)
    {
        int *link = &texture_asset_hash_heads[bucket];
        while (*link >= 0)
        {
            int alias_slot = *link;
            CachedAssetAlias *alias = &texture_asset_aliases[alias_slot];

            if (alias->used && alias->texture_slot == slot)
            {
                *link = alias->next;
                alias->used = 0;
                alias->asset_id = 0;
                alias->content_hash = 0;
                alias->texture_slot = -1;
                alias->next = -1;
            }
            else
            {
                link = &alias->next;
            }
        }
    }
}

static void texture_cache_asset_insert(unsigned int asset_id, uint64_t content_hash, int texture_slot)
{
    unsigned int bucket;
    int alias_slot;
    unsigned int step;

    if (!pcGsTextureAssetCacheEnabled())
        return;

    bucket = texture_asset_hash(asset_id);
    for (alias_slot = texture_asset_hash_heads[bucket]; alias_slot >= 0;
         alias_slot = texture_asset_aliases[alias_slot].next)
    {
        CachedAssetAlias *alias = &texture_asset_aliases[alias_slot];
        if (alias->used && alias->asset_id == asset_id && alias->content_hash == content_hash && alias->texture_slot == texture_slot)
            return;
    }

    for (step = 0; step < MAX_ASSET_ALIASES; step++)
    {
        alias_slot = (int)((texture_asset_next_free + step) % MAX_ASSET_ALIASES);
        if (!texture_asset_aliases[alias_slot].used)
            break;
    }

    if (step == MAX_ASSET_ALIASES)
        return; /* conservative fallback: full asset table means normal upload */

    texture_asset_next_free = (unsigned int)(alias_slot + 1) % MAX_ASSET_ALIASES;
    texture_asset_aliases[alias_slot].used = 1;
    texture_asset_aliases[alias_slot].asset_id = asset_id;
    texture_asset_aliases[alias_slot].content_hash = content_hash;
    texture_asset_aliases[alias_slot].texture_slot = texture_slot;
    texture_asset_aliases[alias_slot].next = texture_asset_hash_heads[bucket];
    texture_asset_hash_heads[bucket] = alias_slot;
}


int pcGsTextureAssetCacheEnabled(void)
{
    static int enabled = -1;

    if (enabled < 0)
    {
        const char *value = getenv("RECVX_PS2_TEX_CACHE");
        /* The static indexed TIM2 cache is enabled by default.
         * Set RECVX_PS2_TEX_CACHE=0 only to disable it for diagnostics. */
        enabled = (value != NULL && value[0] == '0' && value[1] == '\0') ? 0 : 1;
        if (enabled)
            pcLog("gs: static indexed TIM2 asset cache enabled (default)");
        else
            pcLog("gs: static indexed TIM2 asset cache disabled (RECVX_PS2_TEX_CACHE=0)");
    }

    return enabled;
}

static unsigned int texture_cache_hash(u64 gen, u64 key0, u64 key1, u64 key2,
                                       int w, int h, int rows, int cols)
{
    unsigned long long hash = 14695981039346656037ULL;

#define TEX_HASH_MIX(value) hash = (hash ^ (unsigned long long)(value)) * 1099511628211ULL
    TEX_HASH_MIX(gen);
    TEX_HASH_MIX(key0);
    TEX_HASH_MIX(key1);
    TEX_HASH_MIX(key2);
    TEX_HASH_MIX((unsigned int)w);
    TEX_HASH_MIX((unsigned int)h);
    TEX_HASH_MIX((unsigned int)rows);
    TEX_HASH_MIX((unsigned int)cols);
#undef TEX_HASH_MIX

    return (unsigned int)(hash ^ (hash >> 32)) & (TEX_HASH_BUCKETS - 1);
}

static int texture_cache_find(u64 gen, u64 key0, u64 key1, u64 key2,
                              int w, int h, int rows, int cols)
{
    unsigned int bucket = texture_cache_hash(gen, key0, key1, key2, w, h, rows, cols);
    int i;

    for (i = texcache_hash_heads[bucket]; i >= 0; i = texcache[i].hash_next)
    {
        CachedTex* t = &texcache[i];

        if (t->used && t->gen == gen && t->key0 == key0 && t->key1 == key1 &&
            t->key2 == key2 && t->w == w && t->h == h &&
            t->rows == rows && t->cols == cols)
        {
            return i;
        }
    }

    return -1;
}

static int texture_cache_find_asset(unsigned int asset_id, uint64_t content_hash, u64 key0, int w, int h, int rows, int cols)
{
    unsigned int bucket;
    int alias_slot;

    bucket = texture_asset_hash(asset_id);
    for (alias_slot = texture_asset_hash_heads[bucket]; alias_slot >= 0;
         alias_slot = texture_asset_aliases[alias_slot].next)
    {
        CachedAssetAlias *alias = &texture_asset_aliases[alias_slot];
        CachedTex *t;

        if (!alias->used || alias->asset_id != asset_id || alias->content_hash != content_hash ||
            alias->texture_slot < 0 || alias->texture_slot >= MAX_TEX)
            continue;

        t = &texcache[alias->texture_slot];
        if (t->used && t->indexed && !t->hd && (t->key0 == key0) &&
            (t->w == w) && (t->h == h) && (t->rows == rows) && (t->cols == cols) &&
            (t->index_slot >= 0) && (t->index_slot < MAX_INDEXED_ATLASES) &&
            indexed_atlases[t->index_slot].used && indexed_atlases[t->index_slot].tex == t->tex)
        {
            return alias->texture_slot;
        }
    }

    return -1;
}


int pcGsTextureAssetCacheContains(unsigned int asset_id, uint64_t content_hash, uint64_t tex0, int width, int height)
{
    unsigned long long key0;
    unsigned long long cpsm_key;

    if (!pcGsTextureAssetCacheEnabled() || width <= 0 || height <= 0)
        return 0;

    /* Query by Gindex + source image hash, then verify the native texture layout. */
    key0 = tex0 & 0x0000001FFFFFFFFFULL & ~(0x3ULL << 35) & ~(1ULL << 34);
    cpsm_key = ((unsigned long long)GSF(tex0, 51, 4) << 37);
    key0 |= cpsm_key;

    return texture_cache_find_asset(asset_id, content_hash, key0, width, height, height, width) >= 0;
}


static void texture_cache_remove(int slot)
{
    CachedTex* t = &texcache[slot];
    unsigned int bucket;
    int* link;

    if (!t->used)
        return;

    texture_cache_asset_remove(slot);
    bucket = texture_cache_hash(t->gen, t->key0, t->key1, t->key2,
                                t->w, t->h, t->rows, t->cols);
    link = &texcache_hash_heads[bucket];

    while (*link >= 0)
    {
        if (*link == slot)
        {
            *link = t->hash_next;
            t->hash_next = -1;
            return;
        }

        link = &texcache[*link].hash_next;
    }

    /* Defensive recovery if the index is ever out of sync. */
    t->hash_next = -1;
}

static void texture_cache_insert(int slot)
{
    CachedTex* t = &texcache[slot];
    unsigned int bucket = texture_cache_hash(t->gen, t->key0, t->key1, t->key2,
                                             t->w, t->h, t->rows, t->cols);

    t->hash_next = texcache_hash_heads[bucket];
    texcache_hash_heads[bucket] = slot;
}

static void texture_cache_init(void)
{
    unsigned int i;

    for (i = 0; i < TEX_HASH_BUCKETS; i++)
        texcache_hash_heads[i] = -1;
    for (i = 0; i < ASSET_HASH_BUCKETS; i++)
        texture_asset_hash_heads[i] = -1;

    for (i = 0; i < MAX_TEX; i++)
    {
        texcache[i].hash_next = -1;
        texcache[i].index_slot = -1;
    }
    for (i = 0; i < MAX_ASSET_ALIASES; i++)
    {
        texture_asset_aliases[i].used = 0;
        texture_asset_aliases[i].asset_id = 0;
        texture_asset_aliases[i].content_hash = 0;
        texture_asset_aliases[i].texture_slot = -1;
        texture_asset_aliases[i].next = -1;
    }
    texture_asset_next_free = 0;
}

void pcGsResetTextureAssetCache(void)
{
    unsigned int i;
    for (i = 0; i < ASSET_HASH_BUCKETS; i++)
        texture_asset_hash_heads[i] = -1;
    for (i = 0; i < MAX_ASSET_ALIASES; i++)
    {
        texture_asset_aliases[i].used = 0;
        texture_asset_aliases[i].asset_id = 0;
        texture_asset_aliases[i].content_hash = 0;
        texture_asset_aliases[i].texture_slot = -1;
        texture_asset_aliases[i].next = -1;
    }
    texture_asset_next_free = 0;
}

static void psm_page_size(int psm, int* pw, int* ph)
{
    switch (gsPsmBits(psm))
    {
    case 32: *pw = 64; *ph = 32; break;
    case 16: *pw = 64; *ph = 64; break;
    case 8:  *pw = 128; *ph = 64; break;
    default: *pw = 128; *ph = 128; break;
    }
}

/* Content hash of GS memory pages (8 KB), recomputed only when a page's generation moves.
 * Textures are cached by content: the game streams many textures through the same memory every
 * frame, so the same bytes come back again and again at the same address. */
static unsigned long long page_hash[GS_PAGES];
static unsigned int page_hash_gen[GS_PAGES];
static unsigned char page_hash_valid[GS_PAGES];

static unsigned long long hash_page(unsigned int p)
{
    p &= GS_PAGES - 1;

    if (!page_hash_valid[p] || (page_hash_gen[p] != gsPageGen[p]))
    {
        const unsigned long long* w = (const unsigned long long*)&gsVram[p * 8192];
        unsigned long long h = 0x9E3779B97F4A7C15ULL ^ p;
        int i;

        for (i = 0; i < 8192 / 8; i++)
        {
            h = (h ^ w[i]) * 0x100000001B3ULL;
            h ^= h >> 29;
        }

        page_hash[p] = h;
        page_hash_gen[p] = gsPageGen[p];
        page_hash_valid[p] = 1;
    }

    return page_hash[p];
}

/* content hash of the pages holding a cols x rows corner of a texture */
static unsigned long long gen_sum_rect(unsigned int page_lo, unsigned int stride, int bands, int page_cols)
{
    unsigned long long s = 0;
    int b, c;

    for (b = 0; b < bands; b++)
    {
        for (c = 0; c < page_cols; c++)
        {
            s = (s * 0x100000001B3ULL) ^ hash_page(page_lo + b * stride + c);
        }
    }

    return s;
}

static int indexed_psm(unsigned int psm)
{
    return (psm == GS_PSMT8) || (psm == GS_PSMT4) ||
           (psm == GS_PSMT8H) || (psm == GS_PSMT4HL) || (psm == GS_PSMT4HH);
}

static int indexed_palette_entries(unsigned int psm)
{
    if ((psm == GS_PSMT8) || (psm == GS_PSMT8H)) return 256;
    if ((psm == GS_PSMT4) || (psm == GS_PSMT4HL) || (psm == GS_PSMT4HH)) return 16;
    return 0;
}

/* Separate CLUT textures let several palettes share one source index atlas. Cache by the
 * expanded colors actually consumed by the shader, including alpha semantics from TEXA. */
static GLuint get_palette_texture(u64 tex0, u64 texa, int entries, u64 palette_hash, int diagnostics)
{
    unsigned int colors[256];
    unsigned int cpsm = GSF(tex0, 51, 4);
    int i, slot = -1, oldest = 0;
    CachedPaletteTex* t;

    for (i = 0; i < MAX_PALETTE_TEX; i++)
    {
        t = &palette_texcache[i];

        if (!t->used && (slot < 0))
        {
            slot = i;
        }

        if (t->last_use < palette_texcache[oldest].last_use)
        {
            oldest = i;
        }

        if (t->used && (t->entries == entries) && (t->hash == palette_hash) &&
            (t->texa_key == texa) && (t->cpsm == cpsm))
        {
            t->last_use = frame_counter;
            if (diagnostics) texFrameStats.palette_hits++;
            return t->tex;
        }
    }

    if (diagnostics) texFrameStats.palette_misses++;

    if (slot < 0)
    {
        slot = oldest;
        pcGsDeleteTextures(1, &palette_texcache[slot].tex);
        tex_cached--;
        tex_cached_bytes -= (double)palette_texcache[slot].entries * 4.0;
    }

    memset(colors, 0, sizeof(colors));
    gsReadClut32(tex0, texa, entries, colors);

    t = &palette_texcache[slot];
    t->used = 1;
    t->entries = entries;
    t->hash = palette_hash;
    t->texa_key = texa;
    t->cpsm = cpsm;
    t->last_use = frame_counter;

    {
        double t0 = diagnostics ? pcOsTime() : 0.0;
        t->tex = make_texture(entries, 1, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_NEAREST);
        if (diagnostics) texFrameStats.palette_create_seconds += pcOsTime() - t0;
    }

    {
        double t0 = diagnostics ? pcOsTime() : 0.0;
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, entries, 1, GL_RGBA, GL_UNSIGNED_BYTE, colors);
        if (diagnostics) texFrameStats.palette_upload_seconds += pcOsTime() - t0;
    }

    tex_cached++;
    tex_cached_bytes += (double)entries * 4.0;
    return t->tex;
}

/* Cache the raw index plane without a CLUT. Palette variants then bind a small lookup texture
 * instead of decoding and uploading another full RGBA atlas. */
static int get_indexed_atlas_slot(u64 key0, u64 gen, u64 tex0, int w, int h, int diagnostics,
                                  double* create_elapsed, double* decode_elapsed, double* upload_elapsed)
{
    int i, slot = -1, oldest = 0;
    unsigned int same_key_different_content = 0;
    unsigned long long previous_gen = 0;
    CachedIndexedAtlas* t;

    *create_elapsed = 0.0;
    *decode_elapsed = 0.0;
    *upload_elapsed = 0.0;

    for (i = 0; i < MAX_INDEXED_ATLASES; i++)
    {
        t = &indexed_atlases[i];

        if (!t->used && (slot < 0))
        {
            slot = i;
        }

        if (t->last_use < indexed_atlases[oldest].last_use)
        {
            oldest = i;
        }

        if (t->used && (t->key0 == key0) && (t->w == w) && (t->h == h))
        {
            if (t->gen == gen)
            {
                t->last_use = frame_counter;
                if (diagnostics) texFrameStats.atlas_hits++;
                return i;
            }

            same_key_different_content++;
            if (previous_gen == 0)
            {
                previous_gen = t->gen;
            }
        }
    }

    if (diagnostics)
    {
        texFrameStats.atlas_misses++;
        if (same_key_different_content > 0)
        {
            pcLog("gs indexed atlas changed: key=%010llx size=%dx%d previous-gen=%016llx current-gen=%016llx cached-versions=%u",
                  (unsigned long long)key0, w, h, previous_gen, (unsigned long long)gen,
                  same_key_different_content);
        }
    }

    if (index_buf == NULL)
    {
        return -1;
    }

    if (slot < 0)
    {
        slot = oldest;

        /* The owning atlas is being evicted; remove any palette aliases before deleting it. */
        for (i = 0; i < MAX_TEX; i++)
        {
            if (texcache[i].used && texcache[i].indexed && (texcache[i].index_slot == slot))
            {
                texture_cache_remove(i);
                texcache[i].used = 0;
                texcache[i].tex = 0;
                texcache[i].indexed = 0;
                texcache[i].index_slot = -1;
                texcache[i].hash_next = -1;
            }
        }

        pcGsDeleteTextures(1, &indexed_atlases[slot].tex);
        tex_cached--;
        tex_cached_bytes -= (double)indexed_atlases[slot].w * indexed_atlases[slot].h;
    }

    t = &indexed_atlases[slot];
    memset(t, 0, sizeof(*t));
    t->used = 1;
    t->key0 = key0;
    t->gen = gen;
    t->w = w;
    t->h = h;
    t->last_use = frame_counter;

    {
        double t0 = diagnostics ? pcOsTime() : 0.0;
        t->tex = make_texture(w, h, GL_R8, GL_RED, GL_UNSIGNED_BYTE, GL_NEAREST);
        if (diagnostics) *create_elapsed = pcOsTime() - t0;
        if (diagnostics) texFrameStats.create_seconds += *create_elapsed;
    }

    {
        double t0 = pcOsTime();
        gsDecodeIndices(tex0, w, h, index_buf);
        *decode_elapsed = pcOsTime() - t0;

        stats.tex_decodes++;
        stats.tex_texels += (unsigned int)(w * h);
        stats.t_decode += *decode_elapsed;

        if (diagnostics)
        {
            texFrameStats.decoded++;
            texFrameStats.texels += (unsigned int)(w * h);
            texFrameStats.decode_seconds += *decode_elapsed;
        }
    }

    {
        double t0 = diagnostics ? pcOsTime() : 0.0;
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RED, GL_UNSIGNED_BYTE, index_buf);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        if (diagnostics)
        {
            *upload_elapsed = pcOsTime() - t0;
            texFrameStats.upload_seconds += *upload_elapsed;
        }
    }

    tex_cached++;
    tex_cached_bytes += (double)w * h;
    return slot;
}

static GLuint get_texture(unsigned int asset_id, int asset_id_valid, uint64_t content_hash, u64 tex0, u64 texa, u64 texclut,
                          int w, int h, int rows, int cols,
                          int* hd, int* indexed, GLuint* palette_texture)
{
    unsigned int psm = GSF(tex0, 20, 6);
    unsigned int tbp = GSF(tex0, 0, 14);
    unsigned int tbw = GSF(tex0, 14, 6);
    int is_indexed = indexed_psm(psm);
    u64 index_key0 = tex0 & 0x0000001FFFFFFFFFULL & ~(0x3ULL << 35) & ~(1ULL << 34);
    u64 key0 = index_key0;   /* TBP..TH, drop TCC/TFX and CLUT fields */
    u64 cpsm_key = ((u64)GSF(tex0, 51, 4) << 37);
    u64 key1 = is_indexed ? texa :
               (((psm == GS_PSMCT24) || (gsPsmBits(psm) == 16) || (GSF(tex0, 51, 4) >= 2)) ? texa : 0);
    u64 key2 = gsClutHash(tex0);   /* palette contents (0-entry hash for direct color) */
    int pw, ph, i, slot = -1, oldest = 0, cached_slot;
    int diagnostics;
    int requested_rows, requested_cols;
    double create_elapsed = 0.0, decode_elapsed = 0.0, upload_elapsed = 0.0;
    unsigned int page_lo, page_hi, clut_page, stride;
    unsigned long long gen, coverage_area = ~0ULL;
    int coverage_slot = -1;
    CachedTex* t;

    diagnostics = gsTextureDiagnosticsEnabled();
    requested_rows = rows;
    requested_cols = cols;
    key0 |= cpsm_key;
    psm_page_size(psm, &pw, &ph);
    stride = (tbw * 64 + pw - 1) / pw;

    if (stride == 0)
    {
        stride = 1;
    }

    page_lo = tbp >> 5;
    if ((rows <= 0) || (rows > h))
    {
        rows = h;
    }
    else
    {
        rows = ((rows + ph - 1) / ph) * ph;   /* whole pages, so draws that need a little more share it */

        if (rows > h)
        {
            rows = h;
        }
    }

    if ((cols <= 0) || (cols > w))
    {
        cols = w;
    }
    else
    {
        cols = ((cols + pw - 1) / pw) * pw;

        if (cols > w)
        {
            cols = w;
        }
    }

    /* Indexed PS2 textures are commonly used as atlases. Decode their full index plane once;
     * the CLUT is uploaded separately and applied in the shader, so palette variants share
     * the same atlas storage without merging their colors. */
    if ((psm == GS_PSMT8) || (psm == GS_PSMT4) ||
        (psm == GS_PSMT8H) || (psm == GS_PSMT4HL) || (psm == GS_PSMT4HH))
    {
        rows = h;
        cols = w;
    }

    page_hi = page_lo + ((rows + ph - 1) / ph - 1) * stride + (w + pw - 1) / pw;
    clut_page = GSF(tex0, 37, 14) >> 5;

    cached_slot = (pcGsTextureAssetCacheEnabled() && asset_id_valid) ?
        texture_cache_find_asset(asset_id, content_hash, key0, w, h, rows, cols) : -1;
    if (cached_slot >= 0)
    {
        t = &texcache[cached_slot];
        t->last_use = frame_counter;
        indexed_atlases[t->index_slot].last_use = frame_counter;
        *hd = t->hd;
        *indexed = 1;
        *palette_texture = get_palette_texture(tex0, texa, indexed_palette_entries(psm), key2, diagnostics);
        if (diagnostics)
            texFrameStats.asset_hits++;
        return t->tex;
    }

    gen = gen_sum_rect(page_lo, stride, (rows + ph - 1) / ph, (cols + pw - 1) / pw);
    cached_slot = texture_cache_find(gen, key0, key1, key2, w, h, rows, cols);
    if (cached_slot >= 0)
    {
        if (is_indexed && asset_id_valid)
            texture_cache_asset_insert(asset_id, content_hash, cached_slot);
        if (diagnostics)
        {
            texFrameStats.hits++;
        }
        t = &texcache[cached_slot];
        t->last_use = frame_counter;
        if (t->indexed && (t->index_slot >= 0) && (t->index_slot < MAX_INDEXED_ATLASES))
        {
            indexed_atlases[t->index_slot].last_use = frame_counter;
        }
        *hd = t->hd;
        *indexed = t->indexed;
        *palette_texture = t->indexed ?
            get_palette_texture(tex0, texa, indexed_palette_entries(psm), key2, diagnostics) : 0;
        return t->tex;
    }

    /* The renderer decodes only the rectangle a draw can sample, but every cache object
     * represents the full logical texture. Reuse an already-decoded superset when its
     * source pages and palette still match; this avoids duplicate GL texture allocation
     * for the same TEX0 sampled through smaller rectangles. */
    for (i = 0; i < MAX_TEX; i++)
    {
        unsigned long long area;

        t = &texcache[i];

        if (!t->used && (slot < 0))
        {
            slot = i;
        }

        if (t->last_use < texcache[oldest].last_use)
        {
            oldest = i;
        }

        if (t->used && (t->key0 == key0) && (t->key1 == key1) && (t->key2 == key2) &&
            (t->w == w) && (t->h == h) && (t->page_lo == page_lo) &&
            (t->rows >= rows) && (t->cols >= cols))
        {
            unsigned long long cached_gen =
                gen_sum_rect(page_lo, stride, (t->rows + ph - 1) / ph, (t->cols + pw - 1) / pw);

            if (cached_gen == t->gen)
            {
                area = (unsigned long long)t->rows * (unsigned long long)t->cols;
                if (area < coverage_area)
                {
                    coverage_area = area;
                    coverage_slot = i;
                }
            }
        }
    }

    if (coverage_slot >= 0)
    {
        if (is_indexed && asset_id_valid)
            texture_cache_asset_insert(asset_id, content_hash, coverage_slot);
        t = &texcache[coverage_slot];
        t->last_use = frame_counter;
        if (t->indexed && (t->index_slot >= 0) && (t->index_slot < MAX_INDEXED_ATLASES))
        {
            indexed_atlases[t->index_slot].last_use = frame_counter;
        }
        *hd = t->hd;
        *indexed = t->indexed;
        *palette_texture = t->indexed ?
            get_palette_texture(tex0, texa, indexed_palette_entries(psm), key2, diagnostics) : 0;

        if (diagnostics)
        {
            texFrameStats.hits++;
        }

        return t->tex;
    }

    if (diagnostics)
    {
        texFrameStats.misses++;
    }

    if (slot < 0)
    {
        slot = oldest;
        texture_cache_remove(slot);

        if (!texcache[slot].hd && !texcache[slot].indexed)
        {
            pcGsDeleteTextures(1, &texcache[slot].tex);
            tex_cached--;
            tex_cached_bytes -= (double)texcache[slot].w * texcache[slot].h * 4;
        }
    }

    t = &texcache[slot];
    t->used = 1;
    t->key0 = key0;
    t->key1 = key1;
    t->key2 = key2;
    t->w = w;
    t->h = h;
    t->rows = rows;
    t->cols = cols;
    t->page_lo = page_lo;
    t->page_hi = page_hi;
    t->clut_page = clut_page;
    t->gen = gen;
    t->last_use = frame_counter;
    t->hd = 0;
    t->indexed = 0;
    t->index_slot = -1;
    texture_cache_insert(slot);
    if (is_indexed && asset_id_valid)
        texture_cache_asset_insert(asset_id, content_hash, slot);

    {
        extern GLuint gsReplaceLookup(u64 tex0, u64 texa);
        GLuint r;

        double replacement_t0 = diagnostics ? pcOsTime() : 0.0;

        pcGsInvalidateTextureBindings();
        r = gsReplaceLookup(tex0, texa);
        if (diagnostics)
        {
            texFrameStats.replacement_seconds += pcOsTime() - replacement_t0;
        }

        if (r != 0)
        {
            texture_cache_asset_remove(slot);
            t->tex = r;
            t->hd = 1;
            t->indexed = 0;
            t->index_slot = -1;
            *hd = 1;
            *indexed = 0;
            *palette_texture = 0;
            return r;
        }
    }

    *hd = 0;

    if (is_indexed)
    {
        int atlas_slot = get_indexed_atlas_slot(index_key0, gen, tex0, w, h, diagnostics,
                                                &create_elapsed, &decode_elapsed, &upload_elapsed);
        if (atlas_slot >= 0)
        {
            t->tex = indexed_atlases[atlas_slot].tex;
            t->indexed = 1;
            t->index_slot = atlas_slot;
            indexed_atlases[atlas_slot].last_use = frame_counter;
            *indexed = 1;
            *palette_texture = get_palette_texture(tex0, texa, indexed_palette_entries(psm), key2, diagnostics);

            if (diagnostics)
            {
                pcLog("gs texture miss frame %u: TEX0=%08x:%08x TBP=%u PSM=%02x CLUT=%u index-key=%010llx gen=%016llx size=%dx%d sample=%dx%d decoded-index=%dx%d create=%.2fms decode=%.2fms upload=%.2fms",
                      frame_counter + 1, (unsigned int)(tex0 >> 32), (unsigned int)tex0,
                      tbp, psm, GSF(tex0, 37, 14), (unsigned long long)index_key0, (unsigned long long)gen,
                      w, h, requested_cols, requested_rows, w, h,
                      create_elapsed * 1000.0, decode_elapsed * 1000.0, upload_elapsed * 1000.0);
            }

            return t->tex;
        }
    }

    *indexed = 0;
    *palette_texture = 0;
    {
        double t0 = diagnostics ? pcOsTime() : 0.0;
        t->tex = make_texture(w, h, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_NEAREST);
        if (diagnostics)
        {
            create_elapsed = pcOsTime() - t0;
            texFrameStats.create_seconds += create_elapsed;
        }
    }
    tex_cached++;
    tex_cached_bytes += (double)w * h * 4;

    {
        double t0 = pcOsTime();
        gsDecodeTexture(tex0, texa, texclut, cols, rows, decode_buf);
        {
            double elapsed = pcOsTime() - t0;
            stats.tex_decodes++;
            stats.tex_texels += cols * rows;
            stats.t_decode += elapsed;
            decode_elapsed = elapsed;
            if (diagnostics)
            {
                texFrameStats.decoded++;
                texFrameStats.texels += (unsigned int)(cols * rows);
                texFrameStats.decode_seconds += elapsed;
            }
        }
    }
    {
        double t0 = diagnostics ? pcOsTime() : 0.0;
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, cols, rows, GL_RGBA, GL_UNSIGNED_BYTE, decode_buf);
        if (diagnostics)
        {
            upload_elapsed = pcOsTime() - t0;
            texFrameStats.upload_seconds += upload_elapsed;
        }
    }

    if (diagnostics)
    {
        pcLog("gs texture miss frame %u: TEX0=%08x:%08x TBP=%u PSM=%02x CLUT=%u size=%dx%d sample=%dx%d decoded=%dx%d create=%.2fms decode=%.2fms upload=%.2fms",
              frame_counter + 1, (unsigned int)(tex0 >> 32), (unsigned int)tex0,
              tbp, psm, GSF(tex0, 37, 14), w, h, requested_cols, requested_rows, cols, rows,
              create_elapsed * 1000.0, decode_elapsed * 1000.0, upload_elapsed * 1000.0);
    }

    return t->tex;
}

/* ------------------------------------------------------------------------------------------- */
/* upscaled movies (pc_movie.c): while one plays, textures at its TBP sample this frame instead
 * of GS memory, like an HD texture. The frame sits at the top left of a texture that stands for
 * the whole 512x512 texture area the PS2 player uses, at <scale> times its size. */

static GLuint movie_tex;
static unsigned int movie_tbp;
static int movie_on, movie_size;

void pcGsMovieFrame(unsigned int tbp, const unsigned char* rgba, int w, int h, int scale)
{
    if (rgba == NULL)
    {
        movie_on = 0;
        return;
    }

    if ((movie_tex == 0) || (movie_size != 512 * scale))
    {
        if (movie_tex != 0)
        {
            pcGsDeleteTextures(1, &movie_tex);
        }

        movie_size = 512 * scale;
        movie_tex = make_texture(movie_size, movie_size, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR);
    }

    pcGsBindTexture(GL_TEXTURE_2D, movie_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    movie_tbp = tbp;
    movie_on = 1;
}

/* ------------------------------------------------------------------------------------------- */
/* GS memory <-> render targets */

static int rt_overlaps(const RenderTarget* rt, unsigned int bp, unsigned int bw, unsigned int psm, unsigned int y, unsigned int h)
{
    unsigned int rt_lo = rt->fbp * 32;
    unsigned int rt_hi = rt_lo + (rt->w / 64) * (rt->h / 32) * 32;
    unsigned int lo = bp + (y / 32) * bw * 32;
    unsigned int hi = bp + ((y + h + 31) / 32) * (bw ? bw : 1) * 32;

    (void)psm;

    return (lo < rt_hi) && (hi > rt_lo);
}

/* copies rows y0..y1-1 of GS memory into the render target */
static void rt_load_rows(RenderTarget* rt, int y0, int y1)
{
    double t0 = pcOsTime();
    int x, y, sx, sy;
    int rw = gs_scale_x(rt->w);
    int py0 = gs_scale_y(y0);
    int py1 = gs_scale_y(y1);
    int out_h = py1 - py0;
    int ix = (int)(pcGsScaleX + 0.5f);
    int iy = (int)(pcGsScaleY + 0.5f);
    int integer_scale = (ix >= 1) && (iy >= 1) &&
                        (fabsf(pcGsScaleX - ix) < 0.0001f) &&
                        (fabsf(pcGsScaleY - iy) < 0.0001f);
    unsigned int* buf;

    if (rw <= 0 || out_h <= 0)
    {
        return;
    }

    buf = (unsigned int*)malloc((size_t)rw * out_h * sizeof(*buf));
    if (buf == NULL)
    {
        pcLog("gs: could not allocate %d x %d pixels while loading GS memory", rw, out_h);
        return;
    }

    if (integer_scale)
    {
        /* Preserve the fast replication path for the normal integer upscales (1x, 2x, 3x...). */
        for (y = y0; y < y1; y++)
        {
            for (x = 0; x < rt->w; x++)
            {
                unsigned int c = gsMemRead(GS_PSMCT32, rt->fbp * 32, rt->fbw, x, y);
                unsigned int a = (c >> 24) >= 0x80 ? 255 : (c >> 24) * 2;
                c = (c & 0xFFFFFF) | (a << 24);

                for (sy = 0; sy < iy; sy++)
                {
                    for (sx = 0; sx < ix; sx++)
                    {
                        buf[((y - y0) * iy + sy) * rw + x * ix + sx] = c;
                    }
                }
            }
        }
    }
    else
    {
        /* Custom render sizes may be below one output pixel per GS coordinate. Resample the
         * source VRAM rows into the smaller backing buffer; GPU readback later expands to GS size. */
        for (y = py0; y < py1; y++)
        {
            int src_y = (int)((y + 0.5f) / pcGsScaleY);
            if (src_y < y0) src_y = y0;
            if (src_y >= y1) src_y = y1 - 1;

            for (x = 0; x < rw; x++)
            {
                int src_x = (int)((x + 0.5f) / pcGsScaleX);
                unsigned int c, a;
                if (src_x >= rt->w) src_x = rt->w - 1;
                c = gsMemRead(GS_PSMCT32, rt->fbp * 32, rt->fbw, src_x, src_y);
                a = (c >> 24) >= 0x80 ? 255 : (c >> 24) * 2;
                buf[(y - py0) * rw + x] = (c & 0xFFFFFF) | (a << 24);
            }
        }
    }

    pcGsBindTexture(GL_TEXTURE_2D, rt->color);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, py0, rw, out_h, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    free(buf);

    stats.rt_loads++;
    stats.t_load += pcOsTime() - t0;
}

static void rt_load_from_vram(RenderTarget* rt)
{
    rt_load_rows(rt, 0, rt->h);

    rt->vram_dirty = 0;
    rt->rendered = 0;
    rt->drawn_h = 0;
    rt->load_y0 = rt->load_y1 = 0;
}

/* brings the render target up to date with GS memory before it is drawn into, sampled or shown */
static void rt_sync(RenderTarget* rt)
{
    if (rt->vram_dirty)
    {
        rt_load_from_vram(rt);
    }
    else if (rt->load_y1 > rt->load_y0)
    {
        rt_load_rows(rt, rt->load_y0, rt->load_y1);
        rt->load_y0 = rt->load_y1 = 0;
    }
}

void gsXferDone(unsigned int bp, unsigned int bw, unsigned int psm, unsigned int x, unsigned int y, unsigned int w, unsigned int h, int changed)
{
    int i;

    (void)x; (void)w;

    for (i = 0; i < MAX_RT; i++)
    {
        RenderTarget* rt = &rts[i];

        if (rt->used && (changed || rt->rendered) && rt_overlaps(rt, bp, bw, psm, y, h))
        {
            /* the rows of the render target the transfer touches (32-row bands of fbw pages) */
            int band = rt->fbw * 32;
            int lo = (int)(bp + (y / 32) * bw * 32) - (int)(rt->fbp * 32);
            int hi = (int)(bp + ((y + h + 31) / 32) * (bw ? bw : 1) * 32) - (int)(rt->fbp * 32);
            int y0 = (lo <= 0) ? 0 : (lo / band) * 32;
            int y1 = ((hi + band - 1) / band) * 32;

            if (y1 > rt->h)
            {
                y1 = rt->h;
            }

            /* GS memory changed only below what was drawn (a palette or texture stored right
             * after a small render target, such as the in-game monitors' camera view): reload
             * those rows instead of throwing the drawn picture away */
            if (rt->rendered && (y0 >= rt->drawn_h) && (y1 > y0))
            {
                if (rt->load_y1 > rt->load_y0)
                {
                    rt->load_y0 = (y0 < rt->load_y0) ? y0 : rt->load_y0;
                    rt->load_y1 = (y1 > rt->load_y1) ? y1 : rt->load_y1;
                }
                else
                {
                    rt->load_y0 = y0;
                    rt->load_y1 = y1;
                }
            }
            else
            {
                rt->vram_dirty = 1;
            }
        }
    }
}

/* Before the GS memory of a render target is read (local->local / local->host transfers),
 * copy the rendered pixels back into GS memory. Only what was drawn since the buffer last matched
 * GS memory is read, and the GPU first shrinks it to the PS2 resolution: reading the whole
 * upscaled buffer back (tens of MB at 8x) made effects that copy the screen into a texture every
 * frame, such as the water ripple, run at a few frames per second. */
void gsFlushLocalToLocal(unsigned int bp, unsigned int bw, unsigned int psm, unsigned int x, unsigned int y, unsigned int w, unsigned int h)
{
    static GLuint down_fbo, down_tex;
    static int down_w, down_h;
    int i;

    gsRendererFlush();

    for (i = 0; i < MAX_RT; i++)
    {
        RenderTarget* rt = &rts[i];

        if (rt->used && rt_overlaps(rt, bp, bw, psm, y, h))
        {
            unsigned int* buf;
            int px, py, rows;
            double t0;
            int read_x0, read_y0, read_x1, read_y1, read_w, read_h;
            int exact_mapping = 0, full_sync;

            rt_sync(rt);

            /* nothing drawn since it matched GS memory (or GS memory was newer and was just loaded) */
            if (!rt->rendered)
            {
                continue;
            }

            t0 = pcOsTime();
            rows = (rt->drawn_h > 0) ? rt->drawn_h : rt->h;

            /* Fast path: for aligned PSMCT32 transfers with the same framebuffer pitch,
             * map the requested GS rectangle into render-target coordinates. This avoids
             * reading back pixels which the current GS transfer will not consume.
             *
             * The generic full-render-target path remains below for all layouts where that
             * address mapping cannot be proven safe. */
            read_x0 = 0;
            read_y0 = 0;
            read_x1 = rt->w;
            read_y1 = rows;

            if ((psm == GS_PSMCT32) && (rt->psm == GS_PSMCT32) &&
                (bw != 0) && (bw == rt->fbw) &&
                (rt->w == (int)(bw * 64u)) && ((bp & 31u) == 0) &&
                ((unsigned long long)x + (unsigned long long)w <= (unsigned long long)bw * 64ULL))
            {
                int page_delta = (int)rt->fbp - (int)(bp / 32u);

                if ((page_delta % (int)bw) == 0)
                {
                    int rt_source_y = (page_delta / (int)bw) * 32;
                    int req_x0 = (int)x;
                    int req_x1 = (int)(x + w);
                    int req_y0 = (int)y;
                    int req_y1 = (int)(y + h);
                    int clip_y0 = (req_y0 > rt_source_y) ? req_y0 : rt_source_y;
                    int clip_y1 = (req_y1 < rt_source_y + rows) ? req_y1 : rt_source_y + rows;

                    read_x0 = (req_x0 > 0) ? req_x0 : 0;
                    read_x1 = (req_x1 < rt->w) ? req_x1 : rt->w;
                    read_y0 = clip_y0 - rt_source_y;
                    read_y1 = clip_y1 - rt_source_y;

                    if ((read_x0 >= read_x1) || (clip_y0 >= clip_y1))
                    {
                        /* Page-level overlap does not guarantee that this exact rectangle
                         * consumes any pixels from this render target. */
                        continue;
                    }

                    exact_mapping = 1;
                }
            }

            if (!exact_mapping)
            {
                read_x0 = 0;
                read_y0 = 0;
                read_x1 = rt->w;
                read_y1 = rows;
            }

            read_w = read_x1 - read_x0;
            read_h = read_y1 - read_y0;
            if ((read_w <= 0) || (read_h <= 0))
            {
                continue;
            }

            buf = (unsigned int*)malloc((size_t)read_w * (size_t)read_h * sizeof(*buf));
            if (buf == NULL)
            {
                pcLog("gs: could not allocate %d x %d pixels for render-target readback", read_w, read_h);
                continue;
            }

            if ((down_w != rt->w) || (down_h != rt->h))
            {
                if (down_fbo != 0)
                {
                    pcGsDeleteFramebuffers(1, &down_fbo);
                    pcGsDeleteTextures(1, &down_tex);
                }

                down_tex = make_texture(rt->w, rt->h, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_NEAREST);
                glGenFramebuffers(1, &down_fbo);
                pcGsBindFramebuffer(GL_FRAMEBUFFER, down_fbo);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, down_tex, 0);
                down_w = rt->w;
                down_h = rt->h;
            }

            /* Downsample only the source rectangle that the proven-safe transfer consumes. */
            pcGsDisable(GL_SCISSOR_TEST);
            pcGsBindFramebuffer(GL_READ_FRAMEBUFFER, rt->fbo);
            pcGsBindFramebuffer(GL_DRAW_FRAMEBUFFER, down_fbo);
            glBlitFramebuffer(gs_scale_x(read_x0), gs_scale_y(read_y0),
                              gs_scale_x(read_x1), gs_scale_y(read_y1),
                              read_x0, read_y0, read_x1, read_y1,
                              GL_COLOR_BUFFER_BIT, GL_NEAREST);

            pcGsBindFramebuffer(GL_FRAMEBUFFER, down_fbo);
            glPixelStorei(GL_PACK_ALIGNMENT, 4);
            glReadPixels(read_x0, read_y0, read_w, read_h, GL_RGBA, GL_UNSIGNED_BYTE, buf);
            pcGsBindFramebuffer(GL_FRAMEBUFFER, rt->fbo);

            for (py = read_y0; py < read_y1; py++)
            {
                for (px = read_x0; px < read_x1; px++)
                {
                    unsigned int c = buf[(py - read_y0) * read_w + (px - read_x0)];
                    unsigned int a = ((c >> 24) * 128 + 127) / 255;

                    gsMemWrite(GS_PSMCT32, rt->fbp * 32, rt->fbw, px, py, (c & 0xFFFFFF) | (a << 24));
                }
            }

            free(buf);

            full_sync = (read_x0 == 0) && (read_x1 >= rt->w) &&
                        (read_y0 == 0) && (read_y1 >= rows);
            rt->vram_dirty = 0;

            /* A partial readback cannot claim that every pixel in this RT matches GS memory. */
            if (full_sync)
            {
                rt->rendered = 0;
                rt->drawn_h = 0;
            }

            stats.rt_reads++;
            stats.t_read += pcOsTime() - t0;
        }
    }
}
/* ------------------------------------------------------------------------------------------- */

void gsRendererInit(void)
{
    texture_cache_init();
    pcGsGlStateResetCache();

    if (!pcGlLoad())
    {
        pcPlatformFatal("The graphics driver does not support OpenGL 3.3, which the game needs.\n\n"
                        "Updating the graphics driver usually fixes this.");
    }

    decode_buf = malloc(1024 * 1024 * 4);
    index_buf = malloc(1024 * 1024);

    prog = link(vs_src, fs_src);
    present_prog = link(present_vs, present_fs);

#define UL(f, n) U.f = glGetUniformLocation(prog, n)
    UL(target, "uTarget"); UL(tme, "uTme"); UL(fst, "uFst"); UL(tfx, "uTfx"); UL(tcc, "uTcc"); UL(iip, "uIip");
    UL(texrt, "uTexRT"); UL(texsize, "uTexSize"); UL(rtsize, "uRTSize"); UL(wrap, "uWrap"); UL(region, "uRegion");
    UL(linear, "uLinear"); UL(ate, "uAte"); UL(atst, "uAtst"); UL(aref, "uAref"); UL(atinv, "uAtestInvert");
    UL(fge, "uFge"); UL(fogcol, "uFogCol"); UL(tex, "uTex"); UL(palette, "uPalette"); UL(indexed, "uIndexed");
    UL(texa, "uTexa"); UL(hd, "uHD");
    UL(wireframe, "uWireframe");
#undef UL

    glGenVertexArrays(1, &vao);
    pcGsBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    pcGsBindBuffer(GL_ARRAY_BUFFER, vbo);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GlVertex), (void*)offsetof(GlVertex, x));
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(GlVertex), (void*)offsetof(GlVertex, r));
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(GlVertex), (void*)offsetof(GlVertex, s));
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(GlVertex), (void*)offsetof(GlVertex, u));
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(GlVertex), (void*)offsetof(GlVertex, fog));
    glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, sizeof(GlVertex), (void*)offsetof(GlVertex, cu0));

    for (int i = 0; i < 6; i++)
    {
        glEnableVertexAttribArray(i);
    }

    {
        extern void gsReplaceInit(void);

        gsReplaceInit();
    }

    pcLog("gs: renderer ready (internal resolution %dx%d for 640x480 logical output)", gs_scale_x(640), gs_scale_y(480));
}

void gsRendererFlush(void)
{
    extern void gsFlushPrims(void);

    gsFlushPrims();
}

static float z_scale(u64 zbuf)
{
    switch (GSF(zbuf, 24, 4))
    {
    case 0: return 1.0f / 4294967296.0f;   /* Z32 */
    case 1: return 1.0f / 16777216.0f;     /* Z24 */
    default: return 1.0f / 65536.0f;       /* Z16 / Z16S */
    }
}

/* Opt-in diagnostic for one-unit Z precision; no render state is changed. */
#define GS_Z_PRECISION_DIAG_GROUPS 32

typedef struct {
    int used;
    unsigned int fbp, fbw, zbp, psm;
    unsigned int draws, vertices;
    unsigned int min_z, max_z;
    uint64_t same_float, same_clip;
    unsigned int first_same_float, first_same_clip;
} GsZPrecisionDiagGroup;

static GsZPrecisionDiagGroup gsZPrecisionDiag[GS_Z_PRECISION_DIAG_GROUPS];
static unsigned int gsZPrecisionDiagFrames;
static unsigned int gsZPrecisionDiagOverflowDraws;

static int gs_z_precision_diag_enabled(void)
{
    static int enabled = -1;

    if (enabled < 0)
    {
        const char* value = getenv("RECVX_Z_PRECISION_DIAG");
        enabled = (value != NULL && value[0] == '1' && value[1] == '\0') ? 1 : 0;
    }

    return enabled;
}

static const char* gs_z_precision_psm_name(unsigned int psm)
{
    switch (psm)
    {
    case 0: return "Z32";
    case 1: return "Z24";
    case 2: return "Z16";
    case 10: return "Z16S";
    default: return "unknown";
    }
}

static GsZPrecisionDiagGroup* gs_z_precision_diag_group(unsigned int fbp, unsigned int fbw,
                                                         unsigned int zbp, unsigned int psm)
{
    int i;
    GsZPrecisionDiagGroup* free_group = NULL;

    for (i = 0; i < GS_Z_PRECISION_DIAG_GROUPS; i++)
    {
        GsZPrecisionDiagGroup* g = &gsZPrecisionDiag[i];

        if (g->used && g->fbp == fbp && g->fbw == fbw && g->zbp == zbp && g->psm == psm)
            return g;
        if (!g->used && free_group == NULL)
            free_group = g;
    }

    if (free_group == NULL)
    {
        gsZPrecisionDiagOverflowDraws++;
        return NULL;
    }

    memset(free_group, 0, sizeof(*free_group));
    free_group->used = 1;
    free_group->fbp = fbp;
    free_group->fbw = fbw;
    free_group->zbp = zbp;
    free_group->psm = psm;
    free_group->min_z = 0xFFFFFFFFu;
    return free_group;
}

static void gs_z_precision_diag_draw(unsigned int fbp, unsigned int fbw, unsigned int zbp,
                                     unsigned int psm, const GsVertex* v, int nverts, float zs)
{
    GsZPrecisionDiagGroup* g = gs_z_precision_diag_group(fbp, fbw, zbp, psm);
    int i;

    if (g == NULL)
        return;

    g->draws++;
    g->vertices += (unsigned int)nverts;

    for (i = 0; i < nverts; i++)
    {
        unsigned int raw_z = v[i].z;
        float z0 = v[i].z * zs;
        float z1 = (float)((uint64_t)v[i].z + 1ULL) * zs;
        float clip0 = z0 * 2.0f - 1.0f;
        float clip1 = z1 * 2.0f - 1.0f;

        if (raw_z < g->min_z) g->min_z = raw_z;
        if (raw_z > g->max_z) g->max_z = raw_z;

        if (z0 == z1)
        {
            if (g->same_float == 0)
                g->first_same_float = raw_z;
            g->same_float++;
        }
        if (clip0 == clip1)
        {
            if (g->same_clip == 0)
                g->first_same_clip = raw_z;
            g->same_clip++;
        }
    }
}

static void gs_z_precision_diag_report(void)
{
    unsigned int i;
    unsigned int first_frame = gsZPrecisionDiagFrames - 119;

    for (i = 0; i < GS_Z_PRECISION_DIAG_GROUPS; i++)
    {
        const GsZPrecisionDiagGroup* g = &gsZPrecisionDiag[i];

        if (!g->used)
            continue;

        pcLog("gs zdiag frames %u-%u FBP=%u FBW=%u ZBP=%u ZBUF.PSM=%u(%s): draws=%u verts=%u rawZ=%08x..%08x; Z/Z+1 same-float=%llu first=%08x; same-clipZ=%llu first=%08x",
              first_frame, gsZPrecisionDiagFrames, g->fbp, g->fbw, g->zbp, g->psm,
              gs_z_precision_psm_name(g->psm), g->draws, g->vertices, g->min_z, g->max_z,
              (unsigned long long)g->same_float, g->first_same_float,
              (unsigned long long)g->same_clip, g->first_same_clip);
    }

    if (gsZPrecisionDiagOverflowDraws != 0)
        pcLog("gs zdiag: skipped %u draws because more than %u FBP/FBW/ZBP/PSM groups were seen",
              gsZPrecisionDiagOverflowDraws, GS_Z_PRECISION_DIAG_GROUPS);

    memset(gsZPrecisionDiag, 0, sizeof(gsZPrecisionDiag));
    gsZPrecisionDiagOverflowDraws = 0;
}

static GLenum blend_factor(int c, int src_side)
{
    (void)src_side;

    switch (c)
    {
    case 0: return GL_SRC_ALPHA;
    case 1: return GL_DST_ALPHA;
    default: return GL_CONSTANT_ALPHA;
    }
}

static GLenum one_minus(GLenum f)
{
    switch (f)
    {
    case GL_SRC_ALPHA: return GL_ONE_MINUS_SRC_ALPHA;
    case GL_DST_ALPHA: return GL_ONE_MINUS_DST_ALPHA;
    default: return GL_ONE_MINUS_CONSTANT_ALPHA;
    }
}

/* GS blending: ((A - B) * C >> 7) + D with A, B, D in {Cs, Cd, 0}, C in {As, Ad, FIX}.
 * Expanded, the result is Cs * ks + Cd * kd where each k is a combination 1 * k1 + C * kc with
 * k1, kc in {-1, 0, 1}; those map onto GL blend factors and the add/subtract equations.
 * Only k = 1 + C (A = Cs, D = Cs or A = Cd, D = Cd) has no GL factor and is approximated by 1. */
typedef struct { int k1, kc; } BlendCoef;

static BlendCoef blend_coef(int A, int B, int D, int which)
{
    BlendCoef k;

    k.k1 = (D == which);
    k.kc = (A == which) - (B == which);
    return k;
}

static GLenum coef_factor(BlendCoef k, GLenum f)
{
    if ((k.k1 == 0) && (k.kc == 0)) return GL_ZERO;
    if ((k.k1 == 0) && (k.kc != 0)) return f;                 /* +-C */
    if ((k.kc == 0)) return GL_ONE;                            /* +-1 */
    if (k.k1 != k.kc) return one_minus(f);                     /* +-(1 - C) */
    return GL_ONE;                                             /* 1 + C: approximation */
}

static int coef_negative(BlendCoef k)
{
    /* sign of the coefficient for C in 0..1 (0 counts as positive) */
    if (k.k1 != 0) return k.k1 < 0;
    return k.kc < 0;
}

static void setup_blend(const GsDrawState* st)
{
    int A = GSF(st->alpha, 0, 2), B = GSF(st->alpha, 2, 2), C = GSF(st->alpha, 4, 2), D = GSF(st->alpha, 6, 2);
    GLenum f = blend_factor(C, 1);
    BlendCoef ks, kd;
    GLenum src, dst, eq;
    int ns, nd;

    if (!st->abe)
    {
        pcGsDisable(GL_BLEND);
        return;
    }

    pcGsEnable(GL_BLEND);
    pcGsBlendColor(0, 0, 0, GSF(st->alpha, 32, 8) / 128.0f);

    {
        static unsigned char seen[256];
        int m = A | (B << 2) | (C << 4) | (D << 6);

        if (!seen[m])
        {
            seen[m] = 1;
            pcLog("gs: blend mode (%c%c)*%c+%c fix %u", "sdz?"[A], "sdz?"[B], "sdf?"[C], "sdz?"[D], GSF(st->alpha, 32, 8));
        }
    }

    /* A or B = 3 is reserved; treat as 0 */
    if (A == 3) A = 2;
    if (B == 3) B = 2;
    if (D == 3) D = 2;

    ks = blend_coef(A, B, D, 0);
    kd = blend_coef(A, B, D, 1);
    ns = coef_negative(ks);
    nd = coef_negative(kd);

    if (ns) { ks.k1 = -ks.k1; ks.kc = -ks.kc; }
    if (nd) { kd.k1 = -kd.k1; kd.kc = -kd.kc; }

    src = coef_factor(ks, f);
    dst = coef_factor(kd, f);

    if (!ns && !nd)
    {
        eq = GL_FUNC_ADD;
    }
    else if (!ns && nd)
    {
        eq = GL_FUNC_SUBTRACT;          /* Cs*ks - Cd*|kd| */
    }
    else if (ns && !nd)
    {
        eq = GL_FUNC_REVERSE_SUBTRACT;  /* Cd*kd - Cs*|ks| */
    }
    else
    {
        eq = GL_FUNC_ADD;               /* both negative: clamps to 0 */
        src = GL_ZERO;
        dst = GL_ZERO;
    }

    /* the written alpha is As (never blended) */
    pcGsBlendEquationSeparate(eq, GL_FUNC_ADD);
    pcGsBlendFuncSeparate(src, dst, GL_ONE, GL_ZERO);
}

/* how many texture rows (vertical = 1) or columns a draw can sample, or size when it may wrap or the
 * vertices do not tell */
static int texture_extent(const GsDrawState* st, const GsVertex* v, int nverts, int size, int vertical)
{
    int wrap = vertical ? GSF(st->clamp, 2, 2) : GSF(st->clamp, 0, 2);
    float lo = 1e30f, hi = -1e30f;
    int i;

    if (nverts == 0)
    {
        return size;
    }

    for (i = 0; i < nverts; i++)
    {
        float c;

        if (st->fst)
        {
            c = vertical ? v[i].v : v[i].u;
        }
        else
        {
            if (v[i].q <= 0)
            {
                return size;
            }

            c = (vertical ? v[i].t : v[i].s) / v[i].q * size;
        }

        if (c < lo) lo = c;
        if (c > hi) hi = c;
    }

    /* repeating textures can reach anything */
    if (((wrap == 0) || (wrap == 3)) && ((lo < 0) || (hi > size)))
    {
        return size;
    }

    if (wrap == 2)   /* region clamp: up to MAXU / MAXV */
    {
        int maxc = (int)(vertical ? GSF(st->clamp, 34, 10) : GSF(st->clamp, 14, 10)) + 1;

        if ((float)maxc < hi)
        {
            hi = (float)maxc;
        }
    }

    /* one more for bilinear filtering */
    if (hi + 2 >= size)
    {
        return size;
    }

    return (int)hi + 2;
}

void gsRendererDraw(const GsDrawState* st, const GsVertex* v, int nverts)
{
    unsigned int fbp = GSF(st->frame, 0, 9);
    unsigned int fbw = GSF(st->frame, 16, 6);
    unsigned int fbmsk = GSF(st->frame, 32, 32);
    unsigned int fpsm = GSF(st->frame, 24, 6);
    RenderTarget* rt = get_rt(fbp, fbw);
    int ztst = GSF(st->test, 17, 2);
    int zmsk = GSF(st->zbuf, 32, 1);
    int use_depth = (ztst != 1) || !zmsk;
    unsigned int zpsm = GSF(st->zbuf, 24, 4);
    float zs = z_scale(st->zbuf);
    int afail = GSF(st->test, 12, 2);
    int ate = GSF(st->test, 0, 1);
    int atst = GSF(st->test, 1, 3);
    GLenum mode;
    int n = 0, i;
    int indexed_texture = 0;
    GLuint palette_texture = 0;
    int perf_timing = gs_perf_timing_enabled();
    double perf_total_start = perf_timing ? pcOsTime() : 0.0;
    double perf_phase_start = perf_total_start;

    rt->last_use = frame_counter;
    stats.draws++;
    stats.verts += nverts;

    if (gs_z_precision_diag_enabled())
        gs_z_precision_diag_draw(fbp, fbw, GSF(st->zbuf, 0, 9), zpsm, v, nverts, zs);

    if (dump_frame)
    {
        float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
        unsigned int z0 = 0xFFFFFFFF, z1 = 0;

        for (i = 0; i < nverts; i++)
        {
            if (v[i].x < x0) x0 = v[i].x;
            if (v[i].x > x1) x1 = v[i].x;
            if (v[i].y < y0) y0 = v[i].y;
            if (v[i].y > y1) y1 = v[i].y;
            if (v[i].z < z0) z0 = v[i].z;
            if (v[i].z > z1) z1 = v[i].z;
        }

        /* Optional filter to skip screen-space masks/overlays when looking for 3D decal geometry. */
        if (!gs_dump_3d_only_enabled() ||
            (st->tme && !st->fst && (st->type >= 3) && (st->type <= 5) &&
             !((z0 == 0xFFFE) && (z1 == 0xFFFE))))
        {
            dump_draw_index++;
            pcLog("draw fb[%u] %u/%u psm %u msk %08x type %d n %d iip %d tme %d fst %d abe %d fge %d | tex0 %016llx tex1 %llx clamp %llx"
                  " | alpha %010llx test %05llx zbuf %llx sc %016llx texa %llx | x %.1f..%.1f y %.1f..%.1f z %u..%u c0 %02x%02x%02x%02x q %g st %g,%g",
                  dump_draw_index, fbp, fbw, fpsm, fbmsk, st->type, nverts, st->iip, st->tme, st->fst, st->abe, st->fge,
                  st->tex0, st->tex1, st->clamp, st->alpha, st->test, st->zbuf, st->scissor, st->texa,
                  x0, x1, y0, y1, z0, z1, v[0].r, v[0].g, v[0].b, v[0].a, v[0].q, v[0].s, v[0].t);
    
            /* Print exact vertex positions for the dump frame so overlapping geometry can be compared. */
            for (i = 0; i < nverts; i++)
            {
                if (nverts <= 96 || i < 48 || i >= nverts - 48)
                {
                    float z = v[i].z * zs;
                    pcLog("drawv fb[%u] v=%d/%d xy=(%.4f,%.4f) rawZ=%08x normZ=%.9g clipZ=%.9g",
                          dump_draw_index, i, nverts, v[i].x, v[i].y, v[i].z, z, z * 2.0f - 1.0f);
                }
                else if (i == 48)
                {
                    pcLog("drawv fb[%u]: omitted %d middle vertices (showing first/last 48)",
                          dump_draw_index, nverts - 96);
                }
            }
        }
    
        }
    }

    if (rt->vram_dirty)
    {
        /* a full-screen opaque untextured sprite overwrites everything anyway (the shadow
         * buffer is cleared like this every frame after textures were streamed over it) */
        int full_clear = (st->type == 6) && !st->tme && !st->abe && (fbmsk == 0) && (nverts == 2) &&
                         (fminf(v[0].x, v[1].x) <= 0) && (fminf(v[0].y, v[1].y) <= 0) &&
                         (fmaxf(v[0].x, v[1].x) >= 640) && (fmaxf(v[0].y, v[1].y) >= 480);

        if (full_clear)
        {
            rt->vram_dirty = 0;
            rt->load_y0 = rt->load_y1 = 0;
        }
    }

    rt_sync(rt);

    rt->rendered = 1;
    rt->psm = fpsm;

    /* how far down this buffer has been drawn (within the scissor) */
    {
        int bottom = (int)GSF(st->scissor, 48, 11) + 1;
        float ymax = 0;

        for (i = 0; i < nverts; i++)
        {
            if (v[i].y > ymax)
            {
                ymax = v[i].y;
            }
        }

        if ((int)ymax + 1 < bottom)
        {
            bottom = (int)ymax + 1;
        }

        if (bottom > rt->h)
        {
            bottom = rt->h;
        }

        if (bottom > rt->drawn_h)
        {
            rt->drawn_h = bottom;
        }
    }

    if (perf_timing)
    {
        double now = pcOsTime();
        stats.t_pre += now - perf_total_start;
        perf_phase_start = now;
    }

    /* build GL vertices */
    if (st->type == 6)
    {
        for (i = 0; i + 1 < nverts && n + 6 <= MAX_GL_VERTS; i += 2)
        {
            const GsVertex* a = &v[i];
            const GsVertex* b = &v[i + 1];
            GlVertex c[4];
            int k;

            for (k = 0; k < 4; k++)
            {
                const GsVertex* sx = (k & 1) ? b : a;
                const GsVertex* sy = (k & 2) ? b : a;

                c[k].x = sx->x;
                c[k].y = sy->y;
                c[k].z = b->z * zs;
                c[k].r = b->r; c[k].g = b->g; c[k].b = b->b; c[k].a = b->a;
                c[k].s = sx->s; c[k].t = sy->t; c[k].q = b->q;
                c[k].u = sx->u; c[k].v = sy->v;
                c[k].fog = b->f / 255.0f;
                c[k].cu0 = b->cu0; c[k].cv0 = b->cv0; c[k].cu1 = b->cu1; c[k].cv1 = b->cv1;
            }

            glverts[n++] = c[0]; glverts[n++] = c[1]; glverts[n++] = c[2];
            glverts[n++] = c[1]; glverts[n++] = c[3]; glverts[n++] = c[2];
        }

        mode = GL_TRIANGLES;
    }
    else
    {
        for (i = 0; i < nverts && n < MAX_GL_VERTS; i++)
        {
            GlVertex* o = &glverts[n++];

            o->x = v[i].x;
            o->y = v[i].y;
            o->z = v[i].z * zs;
            o->r = v[i].r; o->g = v[i].g; o->b = v[i].b; o->a = v[i].a;
            o->s = v[i].s; o->t = v[i].t; o->q = v[i].q;
            o->u = v[i].u; o->v = v[i].v;
            o->fog = v[i].f / 255.0f;
            o->cu0 = v[i].cu0; o->cv0 = v[i].cv0; o->cu1 = v[i].cu1; o->cv1 = v[i].cv1;
        }

        mode = (st->type == 0) ? GL_POINTS : ((st->type <= 2) ? GL_LINES : GL_TRIANGLES);
    }

    for (i = 0; i < n; i++)
    {
        if (glverts[i].z > 1.0f) glverts[i].z = 1.0f;
    }

    if (perf_timing)
    {
        double now = pcOsTime();
        stats.t_geom += now - perf_phase_start;
        perf_phase_start = now;
    }

    pcGsBindFramebuffer(GL_FRAMEBUFFER, rt->fbo);
    pcGsViewport(0, 0, gs_scale_x(rt->w), gs_scale_y(rt->h));

    /* depth */
    if (use_depth)
    {
        DepthBuffer* db = get_db(GSF(st->zbuf, 0, 9), zpsm, rt->w, rt->h);

        if (rt->depth_attached != db->tex)
        {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, db->tex, 0);
            rt->depth_attached = db->tex;
        }

        pcGsEnable(GL_DEPTH_TEST);
        pcGsDepthFunc((ztst == 0) ? GL_NEVER : (ztst == 1) ? GL_ALWAYS : (ztst == 2) ? GL_GEQUAL : GL_GREATER);
        pcGsDepthMask(zmsk ? GL_FALSE : GL_TRUE);
    }
    else
    {
        pcGsDisable(GL_DEPTH_TEST);
        pcGsDepthMask(GL_FALSE);
    }

    /* scissor */
    {
        int x0 = GSF(st->scissor, 0, 11), x1 = GSF(st->scissor, 16, 11);
        int y0 = GSF(st->scissor, 32, 11), y1 = GSF(st->scissor, 48, 11);

        int sx0 = gs_scale_x(x0), sy0 = gs_scale_y(y0);
        int sx1 = gs_scale_x(x1 + 1), sy1 = gs_scale_y(y1 + 1);
        pcGsEnable(GL_SCISSOR_TEST);
        pcGsScissor(sx0, sy0, sx1 - sx0, sy1 - sy0);
    }

    pcGsColorMask((fbmsk & 0xFF) != 0xFF, (fbmsk & 0xFF00) != 0xFF00, (fbmsk & 0xFF0000) != 0xFF0000,
                ((fbmsk & 0xFF000000) != 0xFF000000) && (fpsm != GS_PSMCT24));

    setup_blend(st);

    pcGsUseProgram(prog);
    glUniform2f(U.target, (float)rt->w, (float)rt->h);
    glUniform1i(U.iip, (st->type == 6) ? 0 : st->iip);
    glUniform1i(U.tme, st->tme);
    glUniform1i(U.fst, st->fst);
    glUniform1i(U.fge, st->fge);
    glUniform3f(U.fogcol, (float)GSF(st->fogcol, 0, 8), (float)GSF(st->fogcol, 8, 8), (float)GSF(st->fogcol, 16, 8));
    glUniform1i(U.ate, ate);
    glUniform1i(U.atst, atst);
    glUniform1i(U.aref, GSF(st->test, 4, 8));
    glUniform1i(U.atinv, 0);
    glUniform1i(U.tex, 0);

    if (st->tme)
    {
        int tw = 1 << GSF(st->tex0, 26, 4), th = 1 << GSF(st->tex0, 30, 4);
        unsigned int tbp = GSF(st->tex0, 0, 14);
        unsigned int tpsm = GSF(st->tex0, 20, 6);
        RenderTarget* src = NULL;
        int linear = GSF(st->tex1, 5, 1);

        if (tw > 1024) tw = 1024;
        if (th > 1024) th = 1024;

        if ((tbp & 31) == 0)
        {
            src = find_rt(tbp >> 5);

            /* a render target is sampled directly when the texture reads it in its own pixel size */
            if ((src != NULL) && ((gsPsmBits(tpsm) != gsPsmBits(src->psm)) ||
                                  ((tpsm != GS_PSMCT32) && (tpsm != GS_PSMCT24) && (tpsm != GS_PSMCT16) && (tpsm != GS_PSMCT16S))))
            {
                src = NULL;
            }
        }

        pcGsActiveTexture(GL_TEXTURE0);

        if ((src != NULL) && (src != rt))
        {
            if (src->vram_dirty || (src->load_y1 > src->load_y0))
            {
                rt_sync(src);
                pcGsBindFramebuffer(GL_FRAMEBUFFER, rt->fbo);
            }

            pcGsBindTexture(GL_TEXTURE_2D, src->color);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
            glUniform1i(U.texrt, (gsPsmBits(tpsm) == 16) ? 2 : 1);
            glUniform1i(U.hd, 0);
            glUniform2f(U.rtsize, (float)src->w, (float)src->h);
            glUniform3f(U.texa, (float)GSF(st->texa, 0, 8), (float)GSF(st->texa, 32, 8), (float)GSF(st->texa, 15, 1));
        }
        else if (movie_on && (GSF(st->tex0, 0, 14) == movie_tbp))
        {
            pcGsBindTexture(GL_TEXTURE_2D, movie_tex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glUniform1i(U.texrt, 0);
            glUniform1i(U.hd, 1);
        }
        else
        {
            int hd = 0;
            GLuint gt = get_texture(st->texture_asset_id, st->texture_asset_valid, st->texture_asset_content_hash,
                                     st->tex0, st->texa, st->texclut,
                                     tw, th, texture_extent(st, v, nverts, th, 1),
                                    texture_extent(st, v, nverts, tw, 0), &hd, &indexed_texture, &palette_texture);

            pcGsBindTexture(GL_TEXTURE_2D, gt);
            glUniform1i(U.texrt, 0);
            glUniform1i(U.hd, hd);

            if (hd)
            {
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (GSF(st->clamp, 0, 2) == 0) ? GL_REPEAT : GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (GSF(st->clamp, 2, 2) == 0) ? GL_REPEAT : GL_CLAMP_TO_EDGE);
            }
        }

        if (indexed_texture && (palette_texture != 0))
        {
            pcGsActiveTexture(GL_TEXTURE1);
            pcGsBindTexture(GL_TEXTURE_2D, palette_texture);
            pcGsActiveTexture(GL_TEXTURE0);
        }
        glUniform1i(U.palette, 1);
        glUniform2f(U.texsize, (float)tw, (float)th);
        glUniform1i(U.tfx, GSF(st->tex0, 35, 2));
        glUniform1i(U.tcc, GSF(st->tex0, 34, 1));
        glUniform2i(U.wrap, GSF(st->clamp, 0, 2), GSF(st->clamp, 2, 2));
        glUniform4i(U.region, GSF(st->clamp, 4, 10), GSF(st->clamp, 14, 10), GSF(st->clamp, 24, 10), GSF(st->clamp, 34, 10));
        glUniform1i(U.linear, linear);
    }

    /* Do not let render-target, movie, untextured, or direct-color draws inherit
     * indexed lookup mode from a previous draw. */
    glUniform1i(U.indexed, indexed_texture);

    pcGsBindVertexArray(vao);
    pcGsBindBuffer(GL_ARRAY_BUFFER, vbo);

    if (perf_timing)
    {
        double now = pcOsTime();
        stats.t_setup += now - perf_phase_start;
        perf_phase_start = now;
    }

    glBufferData(GL_ARRAY_BUFFER, n * sizeof(GlVertex), glverts, GL_STREAM_DRAW);

    if (perf_timing)
    {
        double now = pcOsTime();
        stats.t_upload += now - perf_phase_start;
        perf_phase_start = now;
    }

    glUniform1i(U.wireframe, 0);

    if (ate && (afail != 0) && (atst != 1))
    {
        /* alpha test with a fail action: pass 1 normal, pass 2 the failing pixels with the
         * buffers AFAIL still allows (FB_ONLY: color, ZB_ONLY: depth, RGB_ONLY: rgb) */
        glDrawArrays(mode, 0, n);

        glUniform1i(U.atinv, 1);

        if (afail == 1)
        {
            pcGsDepthMask(GL_FALSE);
        }
        else if (afail == 2)
        {
            pcGsColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        }
        else
        {
            pcGsDepthMask(GL_FALSE);
            pcGsColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
        }

        glDrawArrays(mode, 0, n);
    }
    else
    {
        glDrawArrays(mode, 0, n);
    }

    /* Diagnostic overlay only: fixed-UV textured primitives (the usual 2D UI/text path).
     * Force a solid color so transparency and the suspect UVs cannot hide the geometry. */
    if (ui_wireframe && st->tme && st->fst && (mode == GL_TRIANGLES) && (n > 0))
    {
        pcGsColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        pcGsDepthMask(GL_FALSE);
        pcGsDisable(GL_DEPTH_TEST);
        pcGsDisable(GL_BLEND);
        glUniform1i(U.wireframe, 1);
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        glDrawArrays(mode, 0, n);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glUniform1i(U.wireframe, 0);

        /* Restore the GS state for the next primitive. */
        pcGsColorMask((fbmsk & 0xFF) != 0xFF, (fbmsk & 0xFF00) != 0xFF00, (fbmsk & 0xFF0000) != 0xFF0000,
                    ((fbmsk & 0xFF000000) != 0xFF000000) && (fpsm != GS_PSMCT24));
        setup_blend(st);
        if (use_depth)
        {
            pcGsEnable(GL_DEPTH_TEST);
            pcGsDepthFunc((ztst == 0) ? GL_NEVER : (ztst == 1) ? GL_ALWAYS : (ztst == 2) ? GL_GEQUAL : GL_GREATER);
            pcGsDepthMask(zmsk ? GL_FALSE : GL_TRUE);
        }
        else
        {
            pcGsDisable(GL_DEPTH_TEST);
            pcGsDepthMask(GL_FALSE);
        }
    }

    if (perf_timing)
    {
        double now = pcOsTime();
        stats.t_submit += now - perf_phase_start;
        stats.t_draw += now - perf_total_start;
    }
}

/* ------------------------------------------------------------------------------------------- */

/* draws an RGBA image (top row first) from the PC port's overlays (pc_overlay.c) with its top left
 * corner at x, y pixels of the current viewport (vw x vh); the present program is bound */
static void draw_overlay(const unsigned char* img, int ow, int oh, int x, int y, int vw, int vh)
{
    static GLuint overlay_tex;
    GlVertex q[6];
    float x0 = 2.0f * x / vw - 1.0f, x1 = x0 + 2.0f * ow / vw;
    float y0 = 1.0f - 2.0f * y / vh, y1 = y0 - 2.0f * oh / vh;
    float px[4], py[4], ts[4] = { 0, 1, 0, 1 }, tt[4] = { 0, 0, 1, 1 };
    int idx[6] = { 0, 1, 2, 1, 3, 2 };
    int i;

    px[0] = px[2] = x0;
    px[1] = px[3] = x1;
    py[0] = py[1] = y0;
    py[2] = py[3] = y1;

    memset(q, 0, sizeof(q));

    for (i = 0; i < 6; i++)
    {
        q[i].x = px[idx[i]];
        q[i].y = py[idx[i]];
        q[i].s = ts[idx[i]];
        q[i].t = tt[idx[i]];
    }

    if (overlay_tex == 0)
    {
        glGenTextures(1, &overlay_tex);
    }

    pcGsBindTexture(GL_TEXTURE_2D, overlay_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, ow, oh, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBufferData(GL_ARRAY_BUFFER, sizeof(q), q, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void gsRendererPresent(unsigned int dispfb_fbp, unsigned int dispfb_fbw)
{
    RenderTarget* rt = find_rt(dispfb_fbp);
    int ww, wh, vw, vh;
    GlVertex q[6];
    float tw, th;
    int i;
    int diagnostics = gsTextureDiagnosticsEnabled();

    unsigned int clut_loads, clut_flushes, clut_vertices, clut_draw_calls;
    double clut_draw_seconds;

    gsRendererFlush();
    frame_counter++;
    stats.frames++;

    if (gs_z_precision_diag_enabled())
    {
        gsZPrecisionDiagFrames++;
        if ((gsZPrecisionDiagFrames % 120) == 0)
            gs_z_precision_diag_report();
    }

    gsTakeClutFrameStats(&clut_loads, &clut_flushes, &clut_vertices, &clut_draw_calls, &clut_draw_seconds);
    if (diagnostics && (texFrameStats.misses > 0 || clut_flushes >= 4 || clut_loads >= 32 || clut_draw_seconds >= 0.0005))
    {
        pcLog("gs first-use frame %u: tex hit=%u miss=%u native asset hits=%u atlas hit=%u atlas miss=%u palette hit=%u palette miss=%u decoded=%u texels=%u decode=%.2fms create=%.2fms upload=%.2fms palette create=%.2fms palette upload=%.2fms replacement=%.2fms; CLUT loads=%u pending flushes=%u verts=%u flush draws=%u flush draw time=%.2fms",
              frame_counter, texFrameStats.hits, texFrameStats.misses, texFrameStats.asset_hits,
              texFrameStats.atlas_hits, texFrameStats.atlas_misses, texFrameStats.palette_hits, texFrameStats.palette_misses,
              texFrameStats.decoded, texFrameStats.texels,
              texFrameStats.decode_seconds * 1000.0, texFrameStats.create_seconds * 1000.0,
              texFrameStats.upload_seconds * 1000.0, texFrameStats.palette_create_seconds * 1000.0,
              texFrameStats.palette_upload_seconds * 1000.0, texFrameStats.replacement_seconds * 1000.0,
              clut_loads, clut_flushes, clut_vertices, clut_draw_calls, clut_draw_seconds * 1000.0);
    }
    memset(&texFrameStats, 0, sizeof(texFrameStats));

    /* debug: RECVX_GSDUMP=<frame> logs every draw of that frame (and the next one) */
    {
        static int dump_at = -2;

        if (dump_at == -2)
        {
            const char* e = SDL_getenv("RECVX_GSDUMP");

            dump_at = (e != NULL) ? atoi(e) : -1;
        }

        if (dump_frame)
        {
            extern void pcGsScreenshot(const char* path, unsigned int fbp);

            pcLog("present fbp %u fbw %u", dispfb_fbp, dispfb_fbw);
            pcGsScreenshot("gsdump.png", dispfb_fbp);
        }

        {
            extern int gsDumpFrame;

            gsDumpFrame = (dump_at >= 0) && ((frame_counter == (unsigned int)dump_at) || (frame_counter == (unsigned int)dump_at + 1));
        }

        dump_draw_index = 0;
        dump_frame = (dump_at >= 0) && ((frame_counter == (unsigned int)dump_at) || (frame_counter == (unsigned int)dump_at + 1));
    }

    SDL_GetWindowSizeInPixels(SDL_GL_GetCurrentWindow(), &ww, &wh);

    pcGsBindFramebuffer(GL_FRAMEBUFFER, 0);
    pcGsViewport(0, 0, ww, wh);
    pcGsDisable(GL_SCISSOR_TEST);
    pcGsDisable(GL_DEPTH_TEST);
    pcGsDisable(GL_BLEND);
    pcGsColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    if (rt == NULL)
    {
        return;
    }

    if (rt->vram_dirty || (rt->load_y1 > rt->load_y0))
    {
        rt_sync(rt);
        pcGsBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    /* 640x480 shown at the display aspect (4:3, or stretched for widescreen) */
    {
        float aspect = pcVideoAspect();

        vh = wh;
        vw = (int)(wh * aspect + 0.5f);

        if (vw > ww)
        {
            vw = ww;
            vh = (int)(ww / aspect + 0.5f);
        }
    }

    pcGsViewport((ww - vw) / 2, (wh - vh) / 2, vw, vh);

    tw = 640.0f / rt->w;
    th = 480.0f / rt->h;

    memset(q, 0, sizeof(q));
    {
        float px[4] = { -1, 1, -1, 1 }, py[4] = { 1, 1, -1, -1 };
        float ts[4] = { 0, tw, 0, tw }, tt[4] = { 0, 0, th, th };
        int idx[6] = { 0, 1, 2, 1, 3, 2 };

        for (i = 0; i < 6; i++)
        {
            q[i].x = px[idx[i]];
            q[i].y = py[idx[i]];
            q[i].s = ts[idx[i]];
            q[i].t = tt[idx[i]];
        }
    }

    pcGsUseProgram(present_prog);
    pcGsActiveTexture(GL_TEXTURE0);
    pcGsBindVertexArray(vao);
    pcGsBindBuffer(GL_ARRAY_BUFFER, vbo);

    /* a notice from the PC port (pc_overlay.c) in the middle of a black screen, instead of the
     * picture */
    {
        extern const unsigned char* pcOverlayImage(int height, int* w, int* h);
        const unsigned char* img;
        int ow, oh;

        img = pcOverlayImage(vh / 14, &ow, &oh);

        if ((img != NULL) && (ow <= vw) && (oh <= vh))
        {
            draw_overlay(img, ow, oh, (vw - ow) / 2, (vh - oh) / 2, vw, vh);
            return;
        }
    }

    pcGsBindTexture(GL_TEXTURE_2D, rt->color);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    pcGsBindVertexArray(vao);
    pcGsBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(q), q, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    /* the build number at the bottom of the title screen (pc_overlay.c), below the copyright
     * lines (they end at line 450 of 480) */
    {
        extern const unsigned char* pcOverlayBuildImage(int height, int* w, int* h);
        const unsigned char* img;
        int ow, oh;

        img = pcOverlayBuildImage(vh / 26, &ow, &oh);

        if ((img != NULL) && (ow <= vw) && (oh <= vh))
        {
            /* the present shader writes alpha 1, so blend the premultiplied text as a screen:
             * text + picture * (1 - text) */
            pcGsEnable(GL_BLEND);
            pcGsBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
            pcGsBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_COLOR, GL_ZERO, GL_ONE);
            draw_overlay(img, ow, oh, (vw - ow) / 2, vh * 465 / 480 - oh / 2, vw, vh);
            pcGsDisable(GL_BLEND);
        }
    }

    (void)dispfb_fbw;
}

/* Debug: save a render target (PS2 resolution, top row first) as PNG. */
int pcWritePng(const char* path, const unsigned char* rgb, int w, int h);

void pcGsScreenshot(const char* path, unsigned int fbp)
{
    RenderTarget* rt = find_rt(fbp);
    unsigned char* px;
    unsigned char* rgb;
    int W, H, x, y;

    if (rt == NULL)
    {
        return;
    }

    gsRendererFlush();

    W = gs_scale_x(rt->w);
    H = gs_scale_y(rt->h);
    px = (unsigned char*)malloc((size_t)W * H * 4);
    rgb = (unsigned char*)malloc(640 * 480 * 3);

    if (px == NULL || rgb == NULL)
    {
        free(px);
        free(rgb);
        return;
    }

    pcGsBindFramebuffer(GL_FRAMEBUFFER, rt->fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);

    /* RECVX_SHOTS_FULL=1: save the visible portion at the actual internal render resolution. */
    if (getenv("RECVX_SHOTS_FULL") != NULL)
    {
        int w = gs_scale_x(640), h = gs_scale_y(480);
        if (w > W) w = W;
        if (h > H) h = H;

        free(rgb);
        rgb = (unsigned char*)malloc((size_t)w * h * 3);
        if (rgb != NULL)
        {
            for (y = 0; y < h; y++)
            {
                for (x = 0; x < w; x++)
                {
                    memcpy(&rgb[(y * w + x) * 3], &px[(y * W + x) * 4], 3);
                }
            }
            pcWritePng(path, rgb, w, h);
        }
        free(px);
        free(rgb);
        return;
    }

    for (y = 0; y < 480; y++)
    {
        int sy = gs_scale_y(y);
        if (sy >= H) sy = H - 1;

        for (x = 0; x < 640; x++)
        {
            int sx = gs_scale_x(x);
            const unsigned char* src;
            if (sx >= W) sx = W - 1;
            src = &px[((size_t)sy * W + sx) * 4];
            rgb[(y * 640 + x) * 3 + 0] = src[0];
            rgb[(y * 640 + x) * 3 + 1] = src[1];
            rgb[(y * 640 + x) * 3 + 2] = src[2];
        }
    }

    pcWritePng(path, rgb, 640, 480);
    free(px);
    free(rgb);
}

unsigned int pcGsDisplayFbp(void);
