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

typedef struct {
    PcNativeVertex* vertices;
    int count;
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
static GLuint color_target;
static GLuint depth_target;
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
static GLenum blend_src_rgb = GL_SRC_ALPHA;
static GLenum blend_dst_rgb = GL_ONE_MINUS_SRC_ALPHA;
static GLenum blend_src_alpha = GL_ONE;
static GLenum blend_dst_alpha = GL_ONE_MINUS_SRC_ALPHA;
static int scissor_enabled;
static int scissor_rect[4];
static unsigned long long frame_number;

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
    "  float z=max(aViewPosition.z,0.00001);\n"
    "  vec2 s=vec2(aViewPosition.x*uProjection*uAspect.x/z, aViewPosition.y*uProjection*uAspect.y/z)+uOffset;\n"
    "  vec2 n=vec2(s.x/uTarget.x*2.0-1.0, 1.0-s.y/uTarget.y*2.0);\n"
    "  float depth=(uFar/(uFar-uNear))-((uFar*uNear)/(uFar-uNear))/z;\n"
    "  gl_Position=vec4(n*z,(depth*2.0-1.0)*z,z);\n"
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
    if (color_target) glDeleteTextures(1, &color_target);
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
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        pcLog("native renderer: framebuffer incomplete (0x%04x)", (unsigned int)status);
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

void pcRendererShutdown(void)
{
    if (!ready) return;
    glDeleteBuffers(1, &vertex_buffer);
    glDeleteFramebuffers(1, &framebuffer);
    glDeleteTextures(1, &color_target);
    glDeleteTextures(1, &depth_target);
    vertex_buffer = framebuffer = color_target = depth_target = 0;
    ready = 0;
}

void pcRendererBeginFrame(void)
{
    if (!ready) return;
    frame_number++;
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glViewport(0, 0, render_width, render_height);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(clear_color[0], clear_color[1], clear_color[2], clear_color[3]);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

float pcRendererDepthFromViewZ(float z)
{
    float depth;
    if (z <= near_clip) return 0.0f;
    if (z >= far_clip) return 1.0f;
    depth = (far_clip / (far_clip - near_clip)) -
            ((far_clip * near_clip) / (far_clip - near_clip)) / z;
    return depth < 0.0f ? 0.0f : depth > 1.0f ? 1.0f : depth;
}

float pcRendererDepthFromReciprocalZ(float reciprocal_z)
{
    float depth;
    if (reciprocal_z <= 0.0f) return 1.0f;
    depth = (far_clip / (far_clip - near_clip)) -
            ((far_clip * near_clip) / (far_clip - near_clip)) * reciprocal_z;
    return depth < 0.0f ? 0.0f : depth > 1.0f ? 1.0f : depth;
}

void pcRendererSetProjection(float projection, float aw, float ah,
                             float ox, float oy, float near_value, float far_value)
{
    projection_value = projection;
    aspect_w = aw;
    aspect_h = ah;
    offset_x = ox;
    offset_y = oy;
    near_clip = (near_value > 0.0f) ? near_value : 1.0f;
    far_clip = (far_value > near_clip) ? far_value : near_clip + 1.0f;
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

void pcRendererSetBlendFactors(int sr, int dr, int sa, int da)
{
    static const GLenum map[] = {
        GL_ZERO, GL_ONE, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
        GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA, GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA
    };
    if (sr < 0 || sr >= (int)(sizeof(map) / sizeof(map[0]))) sr = 2;
    if (dr < 0 || dr >= (int)(sizeof(map) / sizeof(map[0]))) dr = 3;
    if (sa < 0 || sa >= (int)(sizeof(map) / sizeof(map[0]))) sa = 1;
    if (da < 0 || da >= (int)(sizeof(map) / sizeof(map[0]))) da = 3;
    blend_src_rgb = map[sr];
    blend_dst_rgb = map[dr];
    blend_src_alpha = map[sa];
    blend_dst_alpha = map[da];
}

static void apply_state(const PcNativeDrawState* st)
{
    PcNativeDrawState d;
    if (!st) {
        memset(&d, 0, sizeof(d));
        d.depth_test = 1;
        d.depth_write = 1;
        st = &d;
    }

    if ((depth_override_test >= 0 ? depth_override_test : st->depth_test) != 0) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    glDepthMask((depth_override_write >= 0 ? depth_override_write : st->depth_write) ? GL_TRUE : GL_FALSE);
    if (st->blend) {
        glEnable(GL_BLEND);
        glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
        if (st->premultiplied_alpha)
            glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        else
            glBlendFuncSeparate(blend_src_rgb, blend_dst_rgb, blend_src_alpha, blend_dst_alpha);
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
    glUniform1f(u_near, near_clip);
    glUniform1f(u_far, far_clip);
    glUniform1i(u_textured, st->textured != 0);
    glUniform1i(u_texture, 0);
    glUniform1i(u_fog_enabled, global_fog_enabled);
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
    apply_state(state);
    glBindVertexArray(vertex_array);
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)count * sizeof(*upload)), upload, GL_STREAM_DRAW);
    glDrawArrays(topology, 0, count);
    glBindVertexArray(0);
    free(upload);
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
    item = &transparent_queue[transparent_count];
    memset(item, 0, sizeof(*item));
    item->vertices = (PcNativeVertex*)malloc((size_t)count * sizeof(*vertices));
    if (!item->vertices) return;
    memcpy(item->vertices, vertices, (size_t)count * sizeof(*vertices));
    item->count = count;
    item->state = *state;
    item->topology = topology;
    item->sequence = transparent_sequence++;
    for (i = 0; i < count; i++) depth += vertices[i].z;
    item->sort_depth = depth / (float)count;
    transparent_count++;
}

static int compare_transparent(const void* a, const void* b)
{
    const NativeQueuedDraw* x = (const NativeQueuedDraw*)a;
    const NativeQueuedDraw* y = (const NativeQueuedDraw*)b;
    if (x->sort_depth > y->sort_depth) return -1;
    if (x->sort_depth < y->sort_depth) return 1;
    if (x->sequence < y->sequence) return -1;
    if (x->sequence > y->sequence) return 1;
    return 0;
}

static void draw(const PcNativeVertex* v, int n, const PcNativeDrawState* state, GLenum topology)
{
    if (state && state->blend) queue_transparent(v, n, state, topology);
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
    if (!ready || transparent_count <= 0) return;
    qsort(transparent_queue, (size_t)transparent_count, sizeof(*transparent_queue), compare_transparent);
    for (i = 0; i < transparent_count; i++) {
        NativeQueuedDraw* item = &transparent_queue[i];
        draw_immediate(item->vertices, item->count, &item->state, item->topology);
        free(item->vertices);
        item->vertices = NULL;
    }
    transparent_count = 0;
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

void pcRendererDeleteTexture(unsigned int texture)
{
    GLuint t = (GLuint)texture;
    int i;
    if (t) glDeleteTextures(1, &t);
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
