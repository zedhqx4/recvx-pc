/* Native OpenGL renderer: view-space geometry, GPU projection, rasterization and presentation. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>

#include "pc_gl.h"
#include "pc_renderer.h"
#include "pc_platform.h"

typedef struct {
    float x, y, z;
    float r, g, b, a;
    float u, v;
    float q;
    float fog;
} NativeGpuVertex;

/* Estado de mezcla equivalente al registro ALPHA de la GS (ver pcRendererSetGsAlpha). */
typedef struct {
    GLenum equation;           /* GL_FUNC_ADD / GL_FUNC_SUBTRACT / GL_FUNC_REVERSE_SUBTRACT */
    GLenum src, dst;           /* factores sobre Cs y Cd */
    float fix;                 /* ALPHA.FIX normalizado (128 = 1.0) */
} GsBlend;
typedef struct {
    float projection, aspect_w, aspect_h, offset_x, offset_y, near_clip, far_clip;
    int fog_enabled;
    float fog_color[3];
    GsBlend blend;
    int scissor_enabled, scissor_rect[4];
    int depth_test, depth_write;
} NativeFrameState;

typedef struct {
    PcNativeVertex* vertices;
    int count;
    NativeFrameState frame;
    PcNativeDrawState state;
    float sort_depth;
    GLenum topology;
    unsigned long long sequence;
} NativeQueuedDraw;

typedef struct { GLuint id; int width, height; } NativeTextureInfo;
static NativeQueuedDraw* transparent_queue;
static int transparent_count, transparent_capacity;
static unsigned long long transparent_sequence;
static NativeTextureInfo* native_textures;
static int native_texture_count, native_texture_capacity;
static float current_texture_width = 1.0f, current_texture_height = 1.0f;
static int current_texture_function, current_texture_alpha = 1;

static GLuint framebuffer;
static GLuint render_texture, render_texture_fbo;   /* copia reducida de la pantalla usada como textura */
static GLuint color_target;
static GLuint depth_target;
static GLuint mask_target, mask_framebuffer;   /* contexto 2 de la GS: mascara de sombras, comparte la profundidad */
enum { TARGET_SCREEN = 0, TARGET_MASK = 1 };
static int active_target = TARGET_SCREEN;
static GLuint vertex_array;
static GLuint vertex_buffer;
static GLuint program;
static GLint u_target, u_projection, u_aspect, u_offset, u_near, u_far;
static GLint u_textured, u_texture, u_fog_enabled, u_fog_color, u_alpha_test, u_alpha_ref;
static GLint u_screen_space, u_texture_size, u_texture_function, u_texture_alpha, u_texture_perspective;
static int output_width = 640;
static int output_height = 480;
static int render_width = 640;
static int render_height = 480;
static float projection_value = 1.0f;
static float aspect_w = 1.0f, aspect_h = 1.0f;
static float offset_x, offset_y;
static float near_clip = 1.0f, far_clip = 65535.0f;
static float clear_color[4] = { 0, 0, 0, 1 };
static int global_fog_enabled;
static float global_fog_color[3];
static int ready;
static GLuint current_texture;
static int depth_override_test = -1;
static int depth_override_write = -1;
static GsBlend gs_blend = { GL_FUNC_ADD, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, 1.0f };    /* ALPHA_1 */
static GsBlend gs_blend2 = { GL_FUNC_ADD, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, 1.0f };   /* ALPHA_2 */
static int scissor_enabled;
static int scissor_rect[4];
static unsigned long long frame_number;
static void flush_pending_deletes(void);
static unsigned stat_immediate, stat_queued, stat_vertices, stat_screen_space;
static int debug_magenta = -1;

/* CLAUDE-NOTE[N5][CORREGIDO]: vertex shader con problemas de clipping.
 *  - max(z, 0.00001) MUEVE los vertices con z<=0 en vez de recortarlos: un triangulo que cruza el plano de la
 *    camara cambia de forma. El recorte original (ScissorTriangle de la VU1) tampoco se ejecuta en la ruta nativa.
 *  - Correcto: no dividir aqui. clip.xy = (x*proj*aspect)*2/target + (offset*2/target - 1)*z ; clip.z lineal en z ;
 *    clip.w = z. La GPU recorta contra near/far sola.
 * CLAUDE-NOTE[N12]: la formula de proyeccion en si SI coincide con la de la VU1 (x*proj/z*aspect + offset). */
static const char* vertex_shader =
    "#version 330 core\n"
    "layout(location=0) in vec3 aViewPosition;\n"
    "layout(location=1) in vec4 aColor;\n"
    "layout(location=2) in vec2 aTexCoord;\n"
    "layout(location=3) in float aFog;\n"
    "layout(location=4) in float aQ;\n"
    "uniform vec2 uTarget; uniform float uProjection; uniform vec2 uAspect; uniform vec2 uOffset;\n"
    "uniform float uNear; uniform float uFar; uniform int uScreenSpace;\n"
    "out vec4 vColor; out vec2 vTexCoord; out float vFog; out float vQ;\n"
    "void main(){\n"
    "  vColor=aColor; vTexCoord=aTexCoord; vFog=aFog; vQ=aQ;\n"
    "  if(uScreenSpace!=0){ vec2 n=vec2(aViewPosition.x/uTarget.x*2.0-1.0, 1.0-aViewPosition.y/uTarget.y*2.0);\n"
    "    gl_Position=vec4(n,aViewPosition.z*2.0-1.0,1.0); return; }\n"
    "  float z=aViewPosition.z;\n"
    "  vec2 sp=vec2(aViewPosition.x*uProjection*uAspect.x, aViewPosition.y*uProjection*uAspect.y);\n"
    "  float cx=sp.x*2.0/uTarget.x + (uOffset.x*2.0/uTarget.x-1.0)*z;\n"
    "  float cy=z*(1.0-uOffset.y*2.0/uTarget.y) - sp.y*2.0/uTarget.y;\n"
    "  float k=uFar/(uFar-uNear);\n"
    "  float cz=2.0*(k*z-k*uNear)-z;\n"
    "  gl_Position=vec4(cx,cy,cz,z);\n"
    "}\n";

static const char* fragment_shader =
    "#version 330 core\n"
    "in vec4 vColor; in vec2 vTexCoord; in float vFog; in float vQ;\n"
    "uniform sampler2D uTexture; uniform int uTextured; uniform int uFogEnabled;\n"
    "uniform vec3 uFogColor; uniform int uAlphaTest; uniform float uAlphaRef; uniform vec2 uTextureSize; uniform int uTextureFunction, uTextureAlpha, uTexturePerspective, uScreenSpace;\n"
    "out vec4 oColor;\n"
    "void main(){ vec4 c=vColor; if(uTextured!=0){ vec2 uv=vTexCoord; if(uScreenSpace!=0 && uTexturePerspective!=0)uv/=max(vQ,0.000001); vec4 t=texture(uTexture,uv/uTextureSize);\n"
    "    if(uTextureFunction==0){ c.rgb=t.rgb*c.rgb*1.9921875; if(uTextureAlpha!=0)c.a=t.a*c.a*1.9921875; }\n"
    "    else if(uTextureFunction==1){ c.rgb=t.rgb; if(uTextureAlpha!=0)c.a=t.a*1.9921875; }\n"
    "    else if(uTextureFunction==2){ c.rgb=clamp(t.rgb*c.rgb*1.9921875+vec3(c.a*0.5019608),0.0,1.0); if(uTextureAlpha!=0)c.a=clamp(t.a*1.9921875+c.a,0.0,1.0); }\n"
    "    else if(uTextureFunction==3){ c.rgb=clamp(t.rgb*c.rgb*1.9921875+vec3(c.a*0.5019608),0.0,1.0); if(uTextureAlpha!=0)c.a=t.a*1.9921875; }\n"
    "    else { c=t; } }\n"
    "  if(uAlphaTest!=0 && c.a<uAlphaRef)discard;\n"
    "  if(uFogEnabled!=0)c.rgb=mix(uFogColor,c.rgb,clamp(vFog,0.0,1.0));\n"
    "  oColor=c; }\n";

static GLuint compile_shader(GLenum type, const char* source)
{
    GLuint shader = glCreateShader(type);
    GLint ok = 0;
    GLchar log[2048];
    GLsizei length = 0;

    if (!shader) return 0;
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        glGetShaderInfoLog(shader, (GLsizei)sizeof(log), &length, log);
        pcLog("native renderer: shader compilation failed: %.*s", (int)length, log);
        return 0;
    }
    return shader;
}

static GLuint create_program(void)
{
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vertex_shader);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fragment_shader);
    GLuint p;
    GLint ok = 0;
    GLchar log[2048];
    GLsizei length = 0;

    if (!vs || !fs) return 0;
    p = glCreateProgram();
    if (!p) return 0;
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glBindAttribLocation(p, 0, "aViewPosition");
    glBindAttribLocation(p, 1, "aColor");
    glBindAttribLocation(p, 2, "aTexCoord");
    glBindAttribLocation(p, 3, "aFog");
    glLinkProgram(p);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        glGetProgramInfoLog(p, (GLsizei)sizeof(log), &length, log);
        pcLog("native renderer: program link failed: %.*s", (int)length, log);
        return 0;
    }
    return p;
}

static int create_targets(int width, int height)
{
    GLenum status;
    render_width = width;
    render_height = height;

    if (framebuffer) glDeleteFramebuffers(1, &framebuffer);
    if (mask_framebuffer) glDeleteFramebuffers(1, &mask_framebuffer);
    if (color_target) glDeleteTextures(1, &color_target);
    if (mask_target) glDeleteTextures(1, &mask_target);
    if (depth_target) glDeleteTextures(1, &depth_target);

    glGenTextures(1, &color_target);
    glBindTexture(GL_TEXTURE_2D, color_target);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenTextures(1, &depth_target);
    glBindTexture(GL_TEXTURE_2D, depth_target);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_target, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth_target, 0);
    status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        pcLog("native renderer: framebuffer incomplete (0x%04x)", (unsigned int)status);
        return 0;
    }

    /* Mascara de sombras: mismo tamano que la escena y la MISMA textura de profundidad (ZBUF_2 = ZBUF_1 en la GS). */
    glGenTextures(1, &mask_target);
    glBindTexture(GL_TEXTURE_2D, mask_target);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &mask_framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, mask_framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mask_target, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth_target, 0);
    status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        pcLog("native renderer: shadow mask framebuffer incomplete (0x%04x)", (unsigned int)status);
        return 0;
    }
    return 1;
}

int pcRendererInit(int width, int height)
{
    GLuint vs;
    if (ready) return 1;
    if (!pcGlLoad()) {
        pcPlatformFatal("The graphics driver does not expose the OpenGL 3.3 functions required by the native renderer.");
        return 0;
    }
    if (width < 1 || height < 1) return 0;

    output_width = width;
    output_height = height;
    offset_x = (float)width * 0.5f;
    offset_y = (float)height * 0.5f;

    program = create_program();
    if (!program) return 0;

    u_target = glGetUniformLocation(program, "uTarget");
    u_projection = glGetUniformLocation(program, "uProjection");
    u_aspect = glGetUniformLocation(program, "uAspect");
    u_offset = glGetUniformLocation(program, "uOffset");
    u_near = glGetUniformLocation(program, "uNear");
    u_far = glGetUniformLocation(program, "uFar");
    u_textured = glGetUniformLocation(program, "uTextured");
    u_texture = glGetUniformLocation(program, "uTexture");
    u_fog_enabled = glGetUniformLocation(program, "uFogEnabled");
    u_fog_color = glGetUniformLocation(program, "uFogColor");
    u_alpha_test = glGetUniformLocation(program, "uAlphaTest");
    u_alpha_ref = glGetUniformLocation(program, "uAlphaRef");
    u_screen_space = glGetUniformLocation(program, "uScreenSpace");
    u_texture_size = glGetUniformLocation(program, "uTextureSize");
    u_texture_function = glGetUniformLocation(program, "uTextureFunction");
    u_texture_alpha = glGetUniformLocation(program, "uTextureAlpha");
    u_texture_perspective = glGetUniformLocation(program, "uTexturePerspective");

    glGenVertexArrays(1, &vertex_array);
    glBindVertexArray(vertex_array);
    glGenBuffers(1, &vertex_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(NativeGpuVertex), (void*)offsetof(NativeGpuVertex, x));
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(NativeGpuVertex), (void*)offsetof(NativeGpuVertex, r));
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(NativeGpuVertex), (void*)offsetof(NativeGpuVertex, u));
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(NativeGpuVertex), (void*)offsetof(NativeGpuVertex, fog));
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(NativeGpuVertex), (void*)offsetof(NativeGpuVertex, q));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glEnableVertexAttribArray(3);
    glEnableVertexAttribArray(4);
    glBindVertexArray(0);

    if (!create_targets(width, height)) return 0;
    ready = 1;
    pcLog("native renderer: OpenGL 3.3 initialized (%dx%d render target)", width, height);
    return 1;
}

/* CLAUDE-NOTE[N11]: no borra vertex_array, program ni las texturas de native_textures (fugas al cerrar/recrear). */
void pcRendererShutdown(void)
{
    if (!ready) return;
    glDeleteBuffers(1, &vertex_buffer);
    glDeleteVertexArrays(1, &vertex_array);
    if (render_texture_fbo) glDeleteFramebuffers(1, &render_texture_fbo);
    if (render_texture) glDeleteTextures(1, &render_texture);
    render_texture = render_texture_fbo = 0;
    glDeleteFramebuffers(1, &framebuffer);
    glDeleteFramebuffers(1, &mask_framebuffer);
    glDeleteTextures(1, &color_target);
    glDeleteTextures(1, &mask_target);
    glDeleteTextures(1, &depth_target);
    vertex_buffer = framebuffer = color_target = depth_target = mask_framebuffer = mask_target = 0;
    ready = 0;
}

/* CLAUDE-NOTE[N1]: para distinguir "no se dibuja nada" de "se dibuja negro", poner temporalmente clear_color a magenta
 * (1,0,1,1) aqui. Si la pantalla sale magenta el blit funciona y el problema esta en lo que se dibuja o en quien limpia
 * despues (ver N2). Tambien conviene contar draws por frame y hacer glGetError()/glReadPixels() en debug=1. */
void pcRendererBeginFrame(void)
{
    if (!ready) return;
    frame_number++;
    if (debug_magenta < 0) { const char* e = getenv("RECVX_CLEAR_DEBUG"); debug_magenta = (e && *e && *e != '0'); }
    if (frame_number % 120 == 0) {
        /* A frame with 0 draws means the problem is before the renderer; >0 means it is in the renderer. */
        pcLog("native renderer: last frame draws=%u (2D/screen-space=%u) vertices=%u queued_blend=%u",
              stat_immediate, stat_screen_space, stat_vertices, stat_queued);
    }
    stat_immediate = stat_queued = stat_vertices = stat_screen_space = 0;
    flush_pending_deletes();
    active_target = TARGET_SCREEN;
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glViewport(0, 0, render_width, render_height);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    if (debug_magenta) glClearColor(1.0f, 0.0f, 1.0f, 1.0f); /* RECVX_CLEAR_DEBUG=1: magenta = blit works, nothing drawn */
    else glClearColor(clear_color[0], clear_color[1], clear_color[2], clear_color[3]);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

/* CLAUDE-FIX[N8/N10]: la GS usaba UN solo espacio Z (near 1, far 65535, ver CalcPs2ZbuffAB) para 3D y 2D. Aqui cada draw
 * usaba el near/far de su camara (2/20000, 10/1000...), asi que 3D y 2D no eran comparables. Todo usa ahora near=1, far=65535. */
#define GS_NEAR 1.0f
#define GS_FAR 65535.0f
float pcRendererDepthFromViewZ(float z)
{
    float depth;
    if (z <= GS_NEAR) return 0.0f;
    if (z >= GS_FAR) return 1.0f;
    depth = (GS_FAR / (GS_FAR - GS_NEAR)) -
            ((GS_FAR * GS_NEAR) / (GS_FAR - GS_NEAR)) / z;
    return depth < 0.0f ? 0.0f : depth > 1.0f ? 1.0f : depth;
}

float pcRendererDepthFromGsZ(float gs_z)
{
    float depth = 1.0f - gs_z / GS_FAR;
    return depth < 0.0f ? 0.0f : depth > 1.0f ? 1.0f : depth;
}

float pcRendererDepthFromReciprocalZ(float reciprocal_z)
{
    float depth;
    if (reciprocal_z <= 0.0f) return 1.0f;
    depth = (GS_FAR / (GS_FAR - GS_NEAR)) -
            ((GS_FAR * GS_NEAR) / (GS_FAR - GS_NEAR)) * reciprocal_z;
    return depth < 0.0f ? 0.0f : depth > 1.0f ? 1.0f : depth;
}

void pcRendererSetProjection(float projection, float aw, float ah,
                             float ox, float oy, float near_value, float far_value)
{
    static int projection_log_valid;
    static float logged_projection, logged_aspect_w, logged_aspect_h;
    static float logged_offset_x, logged_offset_y, logged_near, logged_far;

    projection_value = projection;
    aspect_w = aw;
    aspect_h = ah;

    /* Ninja/GS projection offsets are in the GS 4096x4096 coordinate space.
       The native renderer works in the game's 640x480 pixel space, just like
       the native 2D path (which subtracts 1728,1808 from its screen vertices). */
    offset_x = ox - 1728.0f;
    offset_y = oy - 1808.0f;
    near_clip = (near_value > 0.0f) ? near_value : 1.0f;
    far_clip = (far_value > near_clip) ? far_value : near_clip + 1.0f;

    if (!projection_log_valid ||
        fabsf(logged_projection - projection_value) > 0.001f ||
        fabsf(logged_aspect_w - aspect_w) > 0.001f ||
        fabsf(logged_aspect_h - aspect_h) > 0.001f ||
        fabsf(logged_offset_x - offset_x) > 0.5f ||
        fabsf(logged_offset_y - offset_y) > 0.5f ||
        fabsf(logged_near - near_clip) > 0.5f ||
        fabsf(logged_far - far_clip) > 0.5f) {
        pcLog("native projection: dist=%.3f aspect=(%.4f,%.4f) GS offset=(%.2f,%.2f) -> pixel offset=(%.2f,%.2f) clip=(%.2f,%.2f)",
              projection_value, aspect_w, aspect_h, ox, oy, offset_x, offset_y,
              near_clip, far_clip);
        logged_projection = projection_value;
        logged_aspect_w = aspect_w;
        logged_aspect_h = aspect_h;
        logged_offset_x = offset_x;
        logged_offset_y = offset_y;
        logged_near = near_clip;
        logged_far = far_clip;
        projection_log_valid = 1;
    }
}

void pcRendererSetRenderScale(float sx, float sy)
{
    int w = (int)(output_width * sx + 0.5f);
    int h = (int)(output_height * sy + 0.5f);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (ready && (w != render_width || h != render_height)) {
        if (!create_targets(w, h)) pcPlatformFatal("The native renderer could not create its %dx%d render targets.", w, h);
    }
}

void pcRendererSetClearColor(float r, float g, float b, float a)
{
    clear_color[0] = r; clear_color[1] = g; clear_color[2] = b; clear_color[3] = a;
}

/* CLAUDE-NOTE[N2]: esta funcion limpia el FBO AL INSTANTE. Ps2ScreenClear()/Ps2DispScreenClear() la llaman, y en PS2 esos
 * clears se ejecutaban antes de que el OT diferido dibujara. Aqui el dibujo ya es inmediato, asi que un clear a mitad de
 * frame (p.ej. al final de StoreRenderTex / PS2_Render_Tex_Sub) borra todo lo dibujado antes: pantalla negra. */
void pcRendererClear(float r, float g, float b, float a, int clear_depth)
{
    GLbitfield mask = GL_COLOR_BUFFER_BIT;
    if (!ready) return;
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glViewport(0, 0, render_width, render_height);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(r, g, b, a);
    if (clear_depth) {
        glClearDepth(1.0);
        mask |= GL_DEPTH_BUFFER_BIT;
    }
    glClear(mask);
}

/* CLAUDE-NOTE[N10][PARCIAL]: override GLOBAL y persistente (Ps2ZbuffOff/Off2/On). Se queda "pegado" si nadie lo restaura. Debe ser
 * parte del estado de cada draw/pasada, no un global que pisa al estado del draw. */
void pcRendererSetDepthMode(int depth_test, int depth_write)
{
    depth_override_test = depth_test < 0 ? -1 : (depth_test != 0);
    depth_override_write = depth_write < 0 ? -1 : (depth_write != 0);
}


void pcRendererSetScissor(int enabled, int x, int y, int width, int height)
{
    scissor_enabled = enabled != 0;
    scissor_rect[0] = x; scissor_rect[1] = y; scissor_rect[2] = width; scissor_rect[3] = height;
}

void pcRendererSetGlobalFog(int enabled, float r, float g, float b)
{
    global_fog_enabled = enabled != 0;
    global_fog_color[0] = r; global_fog_color[1] = g; global_fog_color[2] = b;
}

/* Coeficiente de un termino de la ecuacion de la GS sobre Cs o Cd: coef = ka*C + kb. */
typedef struct { int ka, kb; } GsCoef;

/* Traduce un coeficiente a un factor de glBlendFunc. c: 0=As, 1=Ad, 2=FIX. Devuelve 1 si el coeficiente es negativo
 * (se resuelve con SUBTRACT/REVERSE_SUBTRACT). */
static int gs_coef_to_factor(GsCoef k, int c, GLenum* factor)
{
    GLenum plain = (c == 1) ? GL_DST_ALPHA : (c == 2) ? GL_CONSTANT_ALPHA : GL_SRC_ALPHA;
    GLenum inverse = (c == 1) ? GL_ONE_MINUS_DST_ALPHA : (c == 2) ? GL_ONE_MINUS_CONSTANT_ALPHA : GL_ONE_MINUS_SRC_ALPHA;
    if (k.ka == 0) { *factor = k.kb ? GL_ONE : GL_ZERO; return 0; }
    if (k.ka > 0) { *factor = k.kb ? GL_ONE : plain; return 0; }          /* C  (1+C se satura a 1) */
    if (k.kb) { *factor = inverse; return 0; }                           /* 1-C */
    *factor = plain; return 1;                                           /* -C  */
}

/* ALPHA = (A - B) * C / 128 + D con A,B,D en {Cs,Cd,0} y C en {As,Ad,FIX}. La suma de a lo mas dos terminos (uno por Cs y
 * otro por Cd) cabe en la mezcla fija de OpenGL: cada coeficiente es ka*C + kb con ka en {-1,0,1} y kb en {0,1}. El canal
 * alfa se escribe tal cual (la GS guarda As en el framebuffer; Ad lo lee de ahi). */
static void gs_alpha_decode(uint64_t reg, GsBlend* out)
{
    int a = (int)(reg & 3u), b = (int)((reg >> 2) & 3u), c = (int)((reg >> 4) & 3u), d = (int)((reg >> 6) & 3u);
    unsigned int fix = (unsigned int)((reg >> 32) & 0xFFu);
    GsCoef cs, cd;
    int neg_s, neg_d;
    if (c > 2) c = 0;
    cs.ka = (a == 0) - (b == 0); cs.kb = (d == 0);
    cd.ka = (a == 1) - (b == 1); cd.kb = (d == 1);
    neg_s = gs_coef_to_factor(cs, c, &out->src);
    neg_d = gs_coef_to_factor(cd, c, &out->dst);
    out->equation = GL_FUNC_ADD;
    if (neg_d && !neg_s) out->equation = GL_FUNC_SUBTRACT;          /* Cs*f - Cd*g */
    else if (neg_s && !neg_d) out->equation = GL_FUNC_REVERSE_SUBTRACT; /* Cd*g - Cs*f */
    out->fix = (float)fix / 128.0f;
}

void pcRendererSetGsAlpha(uint64_t reg) { gs_alpha_decode(reg, &gs_blend); }
void pcRendererSetGsAlpha2(uint64_t reg) { gs_alpha_decode(reg, &gs_blend2); }

/* CLAUDE-NOTE[N3][CORREGIDO]: apply_state() lee GLOBALES (projection_value, aspect, offset, near/far, global_fog_*,
 * blend_*_rgb/alpha, scissor_*, depth_override_*), no el estado del draw. Para los draws opacos da igual (se dibujan al
 * momento), pero las transparencias se encolan y se dibujan en pcRendererFlushTransparent() con el estado de ESE instante.
 * Cualquier cambio de camara/fog/blend/scissor entre el envio y el flush las estropea. Hay que guardar todo esto dentro
 * de NativeQueuedDraw.
 * CLAUDE-NOTE[N10][PARCIAL]: depth_override_* gana siempre sobre st->depth_*; Ps2ZbuffOn() lo deja en (1,1) y fuerza escritura de
 * profundidad tambien en transparentes y test de profundidad en 2D. */
static void apply_state(const PcNativeDrawState* st)
{
    PcNativeDrawState d;
    if (!st) {
        memset(&d, 0, sizeof(d));
        d.depth_test = 1;
        d.depth_write = 1;
        st = &d;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, active_target == TARGET_MASK ? mask_framebuffer : framebuffer);
    glViewport(0, 0, render_width, render_height);
    {
        /* La GS siempre hacia test de Z (ZTE=1, GEQUAL); Ps2ZbuffOn/Off solo cambian la MASCARA de escritura (ZMSK).
         * Los draws de pantalla sin depth (overlays propios) quedan sin test ni escritura. */
        int test, write;
        if (active_target == TARGET_MASK) { test = 1; write = 0; }          /* TEST_2: ZTE=1 GEQUAL, ZBUF_2.ZMSK=1 */
        else if (st->screen_space && !st->depth_test) { test = 0; write = 0; }
        else {
            test = st->depth_test;
            write = st->depth_write;
            if (!st->screen_space && depth_override_test >= 0) test = depth_override_test;
            if (depth_override_write >= 0) write = depth_override_write;
            if (st->blend && !st->immediate) write = 0;
        }
        if (test) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
        glDepthMask(write ? GL_TRUE : GL_FALSE);
    }
    if (st->blend) {
        const GsBlend* b = (active_target == TARGET_MASK) ? &gs_blend2 : &gs_blend;
        glEnable(GL_BLEND);
        glBlendColor(0.0f, 0.0f, 0.0f, b->fix);
        if (st->premultiplied_alpha) {
            glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
            glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        } else {
            glBlendEquationSeparate(b->equation, GL_FUNC_ADD);
            glBlendFuncSeparate(b->src, b->dst, GL_ONE, GL_ZERO);   /* alfa: lo escribe la fuente, como la GS */
        }
    } else glDisable(GL_BLEND);
    if (st->cull_backfaces) {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
    } else glDisable(GL_CULL_FACE);

    if (scissor_enabled) {
        glEnable(GL_SCISSOR_TEST);
        int sx = (int)((float)scissor_rect[0] * (float)render_width / (float)output_width + 0.5f);
        int sy = (int)((float)scissor_rect[1] * (float)render_height / (float)output_height + 0.5f);
        int sw = (int)((float)scissor_rect[2] * (float)render_width / (float)output_width + 0.5f);
        int sh = (int)((float)scissor_rect[3] * (float)render_height / (float)output_height + 0.5f);
        glScissor(sx, render_height - sy - sh, sw, sh);
    } else glDisable(GL_SCISSOR_TEST);

    glUseProgram(program);
    glUniform2f(u_target, (GLfloat)output_width, (GLfloat)output_height);
    glUniform1f(u_projection, projection_value);
    glUniform2f(u_aspect, aspect_w, aspect_h);
    glUniform2f(u_offset, offset_x, offset_y);
    glUniform1f(u_near, GS_NEAR);
    glUniform1f(u_far, GS_FAR);
    glUniform1i(u_textured, st->textured != 0);
    glUniform1i(u_texture, 0);
    glUniform1i(u_fog_enabled, global_fog_enabled && st->fog);   /* la GS solo aplica niebla con PRIM.FGE */
    glUniform3f(u_fog_color, global_fog_color[0], global_fog_color[1], global_fog_color[2]);
    glUniform1i(u_alpha_test, st->alpha_test != 0);
    glUniform1f(u_alpha_ref, st->alpha_ref);
    glUniform1i(u_screen_space, st->screen_space != 0);
    glUniform2f(u_texture_size, st->texture_width > 0.0f ? st->texture_width : 1.0f,
                st->texture_height > 0.0f ? st->texture_height : 1.0f);
    glUniform1i(u_texture_function, st->texture_function);
    glUniform1i(u_texture_alpha, st->texture_alpha != 0);
    glUniform1i(u_texture_perspective, st->texture_perspective != 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, st->texture);
}

static void draw_immediate(const PcNativeVertex* vertices, int count, const PcNativeDrawState* state, GLenum topology)
{
    NativeGpuVertex* upload;
    int i;
    if (!ready || vertices == NULL || count <= 0) return;
    upload = (NativeGpuVertex*)malloc((size_t)count * sizeof(*upload));
    if (!upload) {
        pcLog("native renderer: vertex upload allocation failed for %d vertices", count);
        return;
    }
    for (i = 0; i < count; i++) {
        upload[i].x = vertices[i].x;
        upload[i].y = vertices[i].y;
        upload[i].z = vertices[i].z;
        upload[i].r = vertices[i].r;
        upload[i].g = vertices[i].g;
        upload[i].b = vertices[i].b;
        upload[i].a = vertices[i].a;
        upload[i].u = vertices[i].u;
        upload[i].v = vertices[i].v;
        upload[i].q = vertices[i].q;
        upload[i].fog = vertices[i].fog;
    }
    stat_immediate++; stat_vertices += (unsigned)count; if (state && state->screen_space) stat_screen_space++;
    if (frame_number % 120 == 119 && stat_immediate <= 40 && state) {
        const PcNativeVertex* last = &vertices[count - 1];
        pcLog("native draw #%u: topo=0x%x n=%d tex=%u(%gx%g) fn=%d ta=%d textured=%d blend=%d screen=%d depth=%d/%d scissor=%d v0=(%.1f,%.1f,%.3f) vN=(%.1f,%.1f) uv0=(%.1f,%.1f) uvN=(%.1f,%.1f) rgba=(%.2f,%.2f,%.2f,%.2f)",
              stat_immediate, (unsigned)topology, count, state->texture, state->texture_width, state->texture_height,
              state->texture_function, state->texture_alpha, state->textured, state->blend, state->screen_space,
              state->depth_test, state->depth_write, scissor_enabled, vertices[0].x, vertices[0].y, vertices[0].z,
              last->x, last->y, vertices[0].u, vertices[0].v, last->u, last->v,
              vertices[0].r, vertices[0].g, vertices[0].b, vertices[0].a);
    }
    apply_state(state);
    glBindVertexArray(vertex_array);
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)count * sizeof(*upload)), upload, GL_STREAM_DRAW);
    glDrawArrays(topology, 0, count);
    glBindVertexArray(0);
    free(upload);
}

/* CLAUDE-NOTE[N8]: toda primitiva con blend se difiere hasta el flush, incluido el 2D (fades, paneles, UI translucida), por
 * lo que se pinta DESPUES de todo el 2D opaco aunque el juego la enviara antes. En PS2 el orden lo daba el OT. */
static void capture_frame_state(NativeFrameState* f)
{
    f->projection = projection_value; f->aspect_w = aspect_w; f->aspect_h = aspect_h;
    f->offset_x = offset_x; f->offset_y = offset_y; f->near_clip = near_clip; f->far_clip = far_clip;
    f->fog_enabled = global_fog_enabled;
    memcpy(f->fog_color, global_fog_color, sizeof(f->fog_color));
    f->blend = gs_blend;
    f->scissor_enabled = scissor_enabled; memcpy(f->scissor_rect, scissor_rect, sizeof(f->scissor_rect));
    f->depth_test = depth_override_test; f->depth_write = depth_override_write;
}

static void restore_frame_state(const NativeFrameState* f)
{
    projection_value = f->projection; aspect_w = f->aspect_w; aspect_h = f->aspect_h;
    offset_x = f->offset_x; offset_y = f->offset_y; near_clip = f->near_clip; far_clip = f->far_clip;
    global_fog_enabled = f->fog_enabled;
    memcpy(global_fog_color, f->fog_color, sizeof(global_fog_color));
    gs_blend = f->blend;
    scissor_enabled = f->scissor_enabled; memcpy(scissor_rect, f->scissor_rect, sizeof(scissor_rect));
    depth_override_test = f->depth_test; depth_override_write = f->depth_write;
}

static void queue_transparent(const PcNativeVertex* vertices, int count, const PcNativeDrawState* state, GLenum topology)
{
    NativeQueuedDraw* item;
    float depth = 0.0f;
    int i;
    if (!ready || !vertices || count <= 0 || !state) return;
    if (transparent_count == transparent_capacity) {
        int cap = transparent_capacity ? transparent_capacity * 2 : 128;
        NativeQueuedDraw* grown = (NativeQueuedDraw*)realloc(transparent_queue, (size_t)cap * sizeof(*grown));
        if (!grown) { pcLog("native renderer: transparent queue allocation failed"); return; }
        transparent_queue = grown;
        transparent_capacity = cap;
    }
    stat_queued++;
    item = &transparent_queue[transparent_count];
    memset(item, 0, sizeof(*item));
    item->vertices = (PcNativeVertex*)malloc((size_t)count * sizeof(*vertices));
    if (!item->vertices) return;
    memcpy(item->vertices, vertices, (size_t)count * sizeof(*vertices));
    item->count = count;
    item->state = *state;
    capture_frame_state(&item->frame);
    item->topology = topology;
    item->sequence = transparent_sequence++;
    /* Clave del OT original (Ps2AddOT): bucket = (int)sz >> 4 (0..4095), sz = Z de la GS del ULTIMO vertice, mayor = mas cerca.
     * Se pinta de bucket menor (lejos) a mayor (cerca); dentro de un bucket, por orden de envio. */
    {
        float sz = 0.0f;
        if (state->ot_z_valid) sz = state->ot_z;
        else if (!state->screen_space) sz = (vertices[count - 1].z > 1.0f) ? (65537.0f / vertices[count - 1].z + 1.0f) : 65534.0f;
        if (sz < 0.0f) sz = 0.0f;
        if (sz > 65534.0f) sz = 65534.0f;
        depth = (float)((int)sz >> (state->ot_coarse ? 12 : 4));
        if (depth > 4095.0f) depth = 4095.0f;
    }
    (void)i;
    item->sort_depth = depth;
    transparent_count++;
}

/* CLAUDE-NOTE[N8]: sort_depth mezcla espacios distintos: z de vista (unidades de mundo, 3D por VU1) contra profundidad
 * 0..1 (screen-space: 2D z=0.5 y 3D preparado). Los 2D translucidos siempre caen "delante" de los 3D y entre ambos grupos
 * el orden relativo no significa nada. Convertir todo a un unico espacio antes de ordenar. */
static int compare_transparent(const void* a, const void* b)
{
    const NativeQueuedDraw* x = (const NativeQueuedDraw*)a;
    const NativeQueuedDraw* y = (const NativeQueuedDraw*)b;
    if (x->sort_depth < y->sort_depth) return -1;
    if (x->sort_depth > y->sort_depth) return 1;
    if (x->sequence < y->sequence) return -1;
    if (x->sequence > y->sequence) return 1;
    return 0;
}

static void draw(const PcNativeVertex* v, int n, const PcNativeDrawState* state, GLenum topology)
{
    if (state && state->blend && !state->immediate && active_target == TARGET_SCREEN) queue_transparent(v, n, state, topology);
    else draw_immediate(v, n, state, topology);
}

void pcRendererDrawTriangleStrip(const PcNativeVertex* v, int n, const PcNativeDrawState* s)
{
    draw(v, n, s, GL_TRIANGLE_STRIP);
}

void pcRendererDrawTriangles(const PcNativeVertex* v, int n, const PcNativeDrawState* s)
{
    draw(v, n, s, GL_TRIANGLES);
}

void pcRendererDrawPoints(const PcNativeVertex* v, int n, const PcNativeDrawState* s)
{
    draw(v, n, s, GL_POINTS);
}

void pcRendererDrawLines(const PcNativeVertex* v, int n, const PcNativeDrawState* s)
{
    draw(v, n, s, GL_LINES);
}

void pcRendererDrawLineStrip(const PcNativeVertex* v, int n, const PcNativeDrawState* s)
{
    draw(v, n, s, GL_LINE_STRIP);
}

void pcRendererDrawScreenTriangles(const PcNativeVertex* v, int n, const PcNativeDrawState* state)
{
    PcNativeDrawState ss;
    if (state) ss = *state; else memset(&ss, 0, sizeof(ss));
    ss.screen_space = 1;
    draw(v, n, &ss, GL_TRIANGLES);
}

void pcRendererFlushTransparent(void)
{
    int i;
    int saved_target = active_target;
    if (!ready || transparent_count <= 0) return;
    NativeFrameState live;
    active_target = TARGET_SCREEN;   /* el OT siempre dibuja en la pantalla, nunca en la mascara de sombras */
    capture_frame_state(&live);
    qsort(transparent_queue, (size_t)transparent_count, sizeof(*transparent_queue), compare_transparent);
    for (i = 0; i < transparent_count; i++) {
        NativeQueuedDraw* item = &transparent_queue[i];
        restore_frame_state(&item->frame);
        depth_override_test = 1;   /* Ps2DrawOTag original: Ps2ZbuffOff = test de Z activo, escritura enmascarada */
        depth_override_write = 0;
        draw_immediate(item->vertices, item->count, &item->state, item->topology);
        free(item->vertices);
        item->vertices = NULL;
    }
    restore_frame_state(&live);
    transparent_count = 0;
    active_target = saved_target;
}

/* ---- Sombras: volumen (prisma) dibujado en la mascara, luego oscurecimiento de la pantalla ---------------------------- */

void pcRendererShadowBegin(void)
{
    if (!ready) return;
    active_target = TARGET_MASK;
    glBindFramebuffer(GL_FRAMEBUFFER, mask_framebuffer);
    glViewport(0, 0, render_width, render_height);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);   /* sprite RGBAQ=0 sobre toda la pantalla en FRAME_2 */
}

void pcRendererShadowEnd(void)
{
    if (!ready) return;
    active_target = TARGET_SCREEN;
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
}

/* Ps2ShadowDraw: sprite con la mascara como textura (MODULATE con RGB = shade, TCC=1, alfa de 1 bit expandido por TEXA), prueba
 * de alfa GREATER 0, sin Z, mezcla ALPHA_2 = (Cd - Cs); despues el rectangulo de la mascara se limpia a 0. */
void pcRendererShadowCompose(float x0, float y0, float x1, float y1, int shade)
{
    PcNativeVertex v[6];
    PcNativeDrawState st;
    GsBlend saved_blend = gs_blend;
    int saved_target = active_target;
    int cx0, cy0, cx1, cy1, i;
    static const unsigned char corner[6] = { 0, 1, 2, 0, 2, 3 };
    if (!ready || !(x1 > x0) || !(y1 > y0)) return;
    if (shade > 255) shade = 255;

    if (shade > 0) {
        memset(v, 0, sizeof(v));
        for (i = 0; i < 6; i++) {
            float x = (corner[i] == 0 || corner[i] == 3) ? x0 : x1;
            float y = (corner[i] < 2) ? y0 : y1;
            v[i].x = x; v[i].y = y; v[i].z = 0.0f;
            v[i].r = v[i].g = v[i].b = (float)shade / 255.0f;
            v[i].a = 1.0f;
            v[i].u = x;                                  /* texels de una mascara de 640x480 ... */
            v[i].v = (float)output_height - y;           /* ... con el origen de GL abajo */
            v[i].q = 1.0f;
        }
        memset(&st, 0, sizeof(st));
        st.texture = mask_target;
        st.texture_width = (float)output_width;
        st.texture_height = (float)output_height;
        st.texture_function = 0;
        st.texture_alpha = 1;
        st.textured = 1;
        st.blend = 1;
        st.immediate = 1;
        st.alpha_test = 1;
        st.alpha_ref = 0.004f;
        st.screen_space = 1;                             /* depth_test = 0: ZTST ALWAYS, ZMSK = 1 */
        gs_alpha_decode(0x80000000A1ULL, &gs_blend);     /* (Cd - Cs) * FIX/128 */
        active_target = TARGET_SCREEN;
        draw_immediate(v, 6, &st, GL_TRIANGLES);
        gs_blend = saved_blend;
        active_target = saved_target;
    }

    /* limpiar el rectangulo usado (sprite RGBAQ=0 sobre FRAME_2) */
    cx0 = (int)floorf(x0 * (float)render_width / (float)output_width);
    cx1 = (int)ceilf(x1 * (float)render_width / (float)output_width);
    cy0 = (int)floorf(y0 * (float)render_height / (float)output_height);
    cy1 = (int)ceilf(y1 * (float)render_height / (float)output_height);
    if (cx0 < 0) cx0 = 0;
    if (cy0 < 0) cy0 = 0;
    if (cx1 > render_width) cx1 = render_width;
    if (cy1 > render_height) cy1 = render_height;
    if (cx1 > cx0 && cy1 > cy0) {
        glBindFramebuffer(GL_FRAMEBUFFER, mask_framebuffer);
        glViewport(0, 0, render_width, render_height);
        glEnable(GL_SCISSOR_TEST);
        glScissor(cx0, render_height - cy1, cx1 - cx0, cy1 - cy0);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);
        glBindFramebuffer(GL_FRAMEBUFFER, saved_target == TARGET_MASK ? mask_framebuffer : framebuffer);
    }
}

static void remember_texture(GLuint id, int width, int height)
{
    int i;
    for (i = 0; i < native_texture_count; i++) {
        if (native_textures[i].id == id) { native_textures[i].width = width; native_textures[i].height = height; return; }
    }
    if (native_texture_count == native_texture_capacity) {
        int cap = native_texture_capacity ? native_texture_capacity * 2 : 64;
        NativeTextureInfo* grown = (NativeTextureInfo*)realloc(native_textures, (size_t)cap * sizeof(*grown));
        if (!grown) return;
        native_textures = grown;
        native_texture_capacity = cap;
    }
    native_textures[native_texture_count].id = id;
    native_textures[native_texture_count].width = width;
    native_textures[native_texture_count].height = height;
    native_texture_count++;
}

unsigned int pcRendererLoadTextureRGBA(const unsigned char* rgba, int width, int height, int linear)
{
    GLuint texture = 0;
    if (!ready || !rgba || width <= 0 || height <= 0) return 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    remember_texture(texture, width, height);
    return texture;
}

/* ---- Superficie de render-to-texture -------------------------------------------------------------------------------
 * En la PS2 el juego copiaba la pantalla (reducida) a otra zona de VRAM (FRAME_2 / FBP 476) y despues la usaba como textura
 * de 256x256. Aqui esa zona es una textura propia con su FBO: capturar = blit con escala desde el FBO de la escena. */
#define RENDER_TEXTURE_SIZE 256

static int ensure_render_texture(void)
{
    if (render_texture) return 1;
    if (!ready) return 0;
    glGenTextures(1, &render_texture);
    glBindTexture(GL_TEXTURE_2D, render_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, RENDER_TEXTURE_SIZE, RENDER_TEXTURE_SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    remember_texture(render_texture, RENDER_TEXTURE_SIZE, RENDER_TEXTURE_SIZE);
    glGenFramebuffers(1, &render_texture_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, render_texture_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, render_texture, 0);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    return 1;
}

/* Copia el rectangulo (sx0,sy0)-(sx1,sy1) de la pantalla de juego (640x480, y hacia abajo) al rectangulo (dx0,dy0)-(dx1,dy1)
 * de la textura (texels, fila 0 arriba), con filtro lineal. Los trozos fuera de la textura se recortan como en la GS. */
void pcRendererCopyScreenToRenderTexture(int sx0, int sy0, int sx1, int sy1, int dx0, int dy0, int dx1, int dy1)
{
    float kx, ky;
    if (!ensure_render_texture()) return;
    kx = (float)render_width / 640.0f;
    ky = (float)render_height / 480.0f;
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, render_texture_fbo);
    glBlitFramebuffer((GLint)(sx0 * kx), render_height - (GLint)(sy0 * ky), (GLint)(sx1 * kx), render_height - (GLint)(sy1 * ky),
                      dx0, dy0, dx1, dy1, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
}

void pcRendererBindRenderTexture(void)
{
    if (!ensure_render_texture()) return;
    pcRendererBindTexture(render_texture);
}

int pcRendererUpdateTextureRGBA(unsigned int texture, const unsigned char* rgba, int width, int height)
{
    int i;
    if (!ready || texture == 0 || rgba == NULL || width <= 0 || height <= 0)
        return 0;

    glBindTexture(GL_TEXTURE_2D, (GLuint)texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (i = 0; i < native_texture_count; i++) {
        if (native_textures[i].id == (GLuint)texture) {
            if (native_textures[i].width != width || native_textures[i].height != height) {
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
                native_textures[i].width = width;
                native_textures[i].height = height;
            } else {
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
            }
            if (current_texture == (GLuint)texture) {
                current_texture_width = (float)width;
                current_texture_height = (float)height;
            }
            return 1;
        }
    }
    return 0;
}

/* CLAUDE-FIX: las transparencias se encolan y se pintan en el flush; si una textura se reemplazaba (misma Gindex con otro
 * contenido, p.ej. 2222000) antes del flush, glDeleteTextures dejaba al draw encolado con un id muerto o reutilizado
 * (sin textura / textura equivocada). Ahora el borrado real se difiere al inicio del siguiente frame. */
static GLuint* pending_delete;
static int pending_delete_count, pending_delete_capacity;

static void flush_pending_deletes(void)
{
    if (pending_delete_count > 0) glDeleteTextures(pending_delete_count, pending_delete);
    pending_delete_count = 0;
}

void pcRendererDeleteTexture(unsigned int texture)
{
    GLuint t = (GLuint)texture;
    int i;
    if (t) {
        if (pending_delete_count == pending_delete_capacity) {
            int cap = pending_delete_capacity ? pending_delete_capacity * 2 : 64;
            GLuint* grown = (GLuint*)realloc(pending_delete, (size_t)cap * sizeof(*grown));
            if (grown) { pending_delete = grown; pending_delete_capacity = cap; }
        }
        if (pending_delete_count < pending_delete_capacity) pending_delete[pending_delete_count++] = t;
        else glDeleteTextures(1, &t);
    }
    for (i = 0; i < native_texture_count; i++) {
        if (native_textures[i].id == t) {
            native_textures[i] = native_textures[--native_texture_count];
            break;
        }
    }
    if (current_texture == t) pcRendererBindTexture(0);
}

void pcRendererBindTexture(unsigned int texture)
{
    int i;
    current_texture = (GLuint)texture;
    current_texture_width = current_texture_height = 1.0f;
    for (i = 0; i < native_texture_count; i++) {
        if (native_textures[i].id == current_texture) {
            current_texture_width = (float)native_textures[i].width;
            current_texture_height = (float)native_textures[i].height;
            break;
        }
    }
}

unsigned int pcRendererCurrentTexture(void)
{
    return (unsigned int)current_texture;
}

void pcRendererCurrentTextureSize(float* width, float* height)
{
    if (width) *width = current_texture_width;
    if (height) *height = current_texture_height;
}

int pcRendererGetOutputSize(int* w, int* h)
{
    if (w) *w = output_width;
    if (h) *h = output_height;
    return ready;
}

/* CLAUDE-NOTE[N1]: el blit de abajo copia el FBO tal cual; no hay ninguna comprobacion (glGetError, glReadPixels) que diga
 * si el FBO tiene contenido. Es el mejor sitio para volcar un pixel/frame cuando se depure la pantalla negra. */
/* Diagnostico: cada 240 frames mide el color medio del FBO y guarda los 3 primeros volcados como recvx_frame_N.bmp. */
static void debug_dump_frame(void)
{
    static int dumps;
    static int max_dumps = -1;
    unsigned char* px;
    size_t n, i;
    double sum[3] = {0, 0, 0};
    unsigned nonblack = 0;
    if (max_dumps < 0) {   /* RECVX_FRAME_DUMP=N: guarda hasta N capturas (una cada 120 frames); por defecto 3 cada 240 */
        const char* env = getenv("RECVX_FRAME_DUMP");
        max_dumps = env ? atoi(env) : 3;
        if (max_dumps < 0) max_dumps = 0;
    }
    if (frame_number % (getenv("RECVX_FRAME_DUMP") ? 120u : 240u) != (getenv("RECVX_FRAME_DUMP") ? 119u : 239u)) return;
    n = (size_t)render_width * (size_t)render_height;
    px = (unsigned char*)malloc(n * 4);
    if (!px) return;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, render_width, render_height, GL_RGBA, GL_UNSIGNED_BYTE, px);
    for (i = 0; i < n; i++) {
        sum[0] += px[i*4]; sum[1] += px[i*4+1]; sum[2] += px[i*4+2];
        if (px[i*4] | px[i*4+1] | px[i*4+2]) nonblack++;
    }
    pcLog("native frame dump: frame %llu avg rgb=(%.1f,%.1f,%.1f) nonblack=%.1f%% size=%dx%d",
          (unsigned long long)frame_number, sum[0]/(double)n, sum[1]/(double)n, sum[2]/(double)n,
          100.0 * (double)nonblack / (double)n, render_width, render_height);
    if (dumps < max_dumps) {
        char name[64];
        FILE* f;
        int row_bytes = render_width * 3, pad = (4 - (row_bytes & 3)) & 3, y;
        unsigned int fsize = (unsigned int)(54 + (row_bytes + pad) * render_height);
        unsigned char hdr[54];
        memset(hdr, 0, sizeof(hdr));
        hdr[0] = 'B'; hdr[1] = 'M';
        memcpy(hdr + 2, &fsize, 4);
        hdr[10] = 54; hdr[14] = 40;
        memcpy(hdr + 18, &render_width, 4);
        memcpy(hdr + 22, &render_height, 4);
        hdr[26] = 1; hdr[28] = 24;
        snprintf(name, sizeof(name), "recvx_frame_%d.bmp", dumps);
        f = fopen(name, "wb");
        if (f) {
            unsigned char zero[4] = {0, 0, 0, 0};
            fwrite(hdr, 1, 54, f);
            for (y = 0; y < render_height; y++) {   /* GL rows are bottom-up, like BMP */
                int x;
                const unsigned char* row = px + (size_t)y * (size_t)render_width * 4;
                for (x = 0; x < render_width; x++) {
                    unsigned char bgr[3] = { row[x*4+2], row[x*4+1], row[x*4] };
                    fwrite(bgr, 1, 3, f);
                }
                if (pad) fwrite(zero, 1, (size_t)pad, f);
            }
            fclose(f);
            pcLog("native frame dump: wrote %s", name);
        }
        dumps++;
    }
    free(px);
}

void pcRendererPresent(void)
{
    /* Native GDI overlays are composed into the same framebuffer before the final blit. */
    {
        extern const unsigned char* pcOverlayImage(int height, int* width, int* image_height);
        extern const unsigned char* pcOverlayBuildImage(int height, int* width, int* image_height);
        static unsigned int notice_texture, build_texture;
        const unsigned char* rgba;
        int iw = 0, ih = 0;
        PcNativeVertex v[6];
        PcNativeDrawState state;
        int image_index;
        int saved_scissor = scissor_enabled;
        int saved_scissor_rect[4] = { scissor_rect[0], scissor_rect[1], scissor_rect[2], scissor_rect[3] };
        int saved_depth_test = depth_override_test;
        int saved_depth_write = depth_override_write;

        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glViewport(0, 0, render_width, render_height);
        scissor_enabled = 0;
        depth_override_test = -1;
        depth_override_write = -1;

        for (image_index = 0; image_index < 2; image_index++) {
            int transparent = (image_index == 1);
            unsigned int* texture = transparent ? &build_texture : &notice_texture;
            rgba = transparent ? pcOverlayBuildImage(14, &iw, &ih) : pcOverlayImage(24, &iw, &ih);
            if (rgba == NULL || iw <= 0 || ih <= 0)
                continue;
            if (*texture == 0) {
                *texture = pcRendererLoadTextureRGBA(rgba, iw, ih, 1);
            } else {
                pcRendererUpdateTextureRGBA(*texture, rgba, iw, ih);
            }
            if (*texture == 0)
                continue;

            {
                float x0 = transparent ? (float)(output_width - iw - 8) : (float)((output_width - iw) / 2);
                float y0 = transparent ? (float)(output_height - ih - 8) : (float)((output_height - ih) / 2);
                float x1 = x0 + (float)iw;
                float y1 = y0 + (float)ih;
                static const unsigned char corner[6] = { 0, 1, 2, 0, 2, 3 };
                int k;
                for (k = 0; k < 6; k++) {
                    unsigned int c = corner[k];
                    v[k].x = (c == 0 || c == 3) ? x0 : x1;
                    v[k].y = (c < 2) ? y0 : y1;
                    v[k].z = 0.0f;
                    v[k].r = v[k].g = v[k].b = v[k].a = 1.0f;
                    v[k].u = (c == 0 || c == 3) ? 0.0f : (float)iw;
                    v[k].v = (c == 0 || c == 1) ? 0.0f : (float)ih;
                    v[k].q = 1.0f;
                    v[k].fog = 1.0f;
                }
            }

            memset(&state, 0, sizeof(state));
            state.texture = *texture;
            state.texture_width = (float)iw;
            state.texture_height = (float)ih;
            state.texture_function = transparent ? 4 : 1;
            state.texture_alpha = transparent;
            state.textured = 1;
            state.blend = transparent;
            state.premultiplied_alpha = transparent;
            state.depth_test = 0;
            state.depth_write = 0;
            state.alpha_test = 0;
            state.linear_filter = 1;
            state.screen_space = 1;
            pcRendererDrawScreenTriangles(v, 6, &state);
        }
        pcRendererFlushTransparent();
        scissor_enabled = saved_scissor;
        scissor_rect[0] = saved_scissor_rect[0];
        scissor_rect[1] = saved_scissor_rect[1];
        scissor_rect[2] = saved_scissor_rect[2];
        scissor_rect[3] = saved_scissor_rect[3];
        depth_override_test = saved_depth_test;
        depth_override_write = saved_depth_write;
    }

    SDL_Window* window;
    int ww = 0, wh = 0;
    float aspect;
    if (ready) debug_dump_frame();
    int vw, vh, x, y;
    if (!ready) return;

    window = SDL_GL_GetCurrentWindow();
    if (window) SDL_GetWindowSizeInPixels(window, &ww, &wh);
    if (ww <= 0 || wh <= 0) return;

    glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    aspect = pcVideoAspect();
    vw = ww;
    vh = (int)(ww / aspect + 0.5f);
    if (vh > wh) {
        vh = wh;
        vw = (int)(wh * aspect + 0.5f);
    }
    x = (ww - vw) / 2;
    y = (wh - vh) / 2;
    glBlitFramebuffer(0, 0, render_width, render_height,
                      x, y, x + vw, y + vh, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void pcRendererSetTextureCombine(int function, int texture_alpha)
{
    current_texture_function = (function >= 0 && function <= 3) ? function : 0;
    current_texture_alpha = texture_alpha != 0;
}

void pcRendererCurrentTextureCombine(int* function, int* texture_alpha)
{
    if (function) *function = current_texture_function;
    if (texture_alpha) *texture_alpha = current_texture_alpha;
}

void pcRendererConfigureTexture(unsigned int texture, int linear, int wrap_u, int wrap_v)
{
    GLenum wu = (wrap_u == 1 || wrap_u == 2) ? GL_CLAMP_TO_EDGE : GL_REPEAT;
    GLenum wv = (wrap_v == 1 || wrap_v == 2) ? GL_CLAMP_TO_EDGE : GL_REPEAT;
    if (!ready || texture == 0) return;
    glBindTexture(GL_TEXTURE_2D, (GLuint)texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (GLint)wu);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (GLint)wv);
}
