/* Experimental PC-native geometry probe.
 *
 * This path consumes post-VU1 screen-space vertices directly, expands primitive kicks into a
 * triangle list on the CPU, then rasterizes that list with OpenGL 3.3 on the GPU. It is deliberately
 * separate from gs.c/gs_gl.c and writes into its own FBO. The initial output is cyan wireframe:
 * it validates geometry capture and ordering, not textures, materials, depth or visual parity.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gs/gl_funcs.h"
#include "../include/pc_native_renderer.h"
#include "../include/pc_platform.h"
#include "gs/gs_internal.h"

#define PC_NATIVE_OT_CAPACITY 8192u
#define PC_NATIVE_MAX_FRAME_VERTICES 3000000u
#define PC_NATIVE_WIDTH 640
#define PC_NATIVE_HEIGHT 480
#define PC_NATIVE_VERTEX_STRIDE 48u
#define PC_NATIVE_DUMP_INTERVAL 60u

typedef struct { unsigned char* source; unsigned int count, primitive_type; int used; } PcNativeCommand;
typedef struct { float x, y; } PcNativeGpuVertex;

static PcNativeCommand commands[PC_NATIVE_OT_CAPACITY];
static unsigned int active_indices[PC_NATIVE_OT_CAPACITY], active_count;
static PcNativeGpuVertex* frame_vertices;
static unsigned int frame_vertex_count, frame_vertex_capacity, frame_number;
static unsigned int frame_captured_draws, frame_consumed_draws, frame_triangles, frame_dropped_triangles;
static GLuint native_fbo, native_color, native_vao, native_vbo, native_program;
static int native_enabled, native_initialized, native_failed;

extern void pcGsScreenshot(const char* path, unsigned int fbp);
extern int pcWritePng(const char* path, const unsigned char* rgb, int w, int h);

static const char* native_vs =
    "#version 330 core\n"
    "layout(location=0) in vec2 aPosition;\n"
    "void main() {\n"
    "  vec2 p = vec2(aPosition.x / 640.0 * 2.0 - 1.0,\n"
    "                1.0 - aPosition.y / 480.0 * 2.0);\n"
    "  gl_Position = vec4(p, 0.0, 1.0);\n"
    "}\n";
static const char* native_fs =
    "#version 330 core\n"
    "out vec4 oColor;\n"
    "void main() { oColor = vec4(0.02, 0.88, 1.0, 1.0); }\n";

static GLuint native_compile_shader(GLenum type, const char* source)
{
    GLuint shader = glCreateShader(type);
    GLint ok = 0;
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(shader, (GLsizei)sizeof(log), NULL, log);
        pcLog("pc-native-gpu: shader compile failed: %s", log);
        return 0;
    }
    return shader;
}

static GLuint native_create_program(void)
{
    GLuint vs = native_compile_shader(GL_VERTEX_SHADER, native_vs);
    GLuint fs = native_compile_shader(GL_FRAGMENT_SHADER, native_fs);
    GLuint program;
    GLint ok = 0;
    if (!vs || !fs) return 0;
    program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(program, (GLsizei)sizeof(log), NULL, log);
        pcLog("pc-native-gpu: program link failed: %s", log);
        return 0;
    }
    return program;
}

void pcNativeRendererInit(void)
{
    const char* enabled = getenv("RECVX_NATIVE_GEOMETRY_DUMP");
    if (!enabled || strcmp(enabled, "1") != 0) return;
    native_enabled = 1;
    if (native_initialized || native_failed) return;

    native_program = native_create_program();
    if (!native_program) {
        native_failed = 1; native_enabled = 0;
        pcLog("pc-native-gpu: disabled (could not create OpenGL 3.3 shader)");
        return;
    }

    glGenTextures(1, &native_color);
    glBindTexture(GL_TEXTURE_2D, native_color);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, PC_NATIVE_WIDTH, PC_NATIVE_HEIGHT, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    glGenFramebuffers(1, &native_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, native_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, native_color, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        native_failed = 1; native_enabled = 0;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        gsRendererInvalidateGlStateCache();
        pcLog("pc-native-gpu: disabled (diagnostic framebuffer is incomplete)");
        return;
    }

    glGenVertexArrays(1, &native_vao);
    glGenBuffers(1, &native_vbo);
    glBindVertexArray(native_vao);
    glBindBuffer(GL_ARRAY_BUFFER, native_vbo);
    glBufferData(GL_ARRAY_BUFFER, 0, NULL, GL_STREAM_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(PcNativeGpuVertex), (const void*)0);
    glEnableVertexAttribArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    gsRendererInvalidateGlStateCache();

    native_initialized = 1;
    pcLog("pc-native-gpu: OpenGL 3.3 geometry probe enabled (640x480 cyan wireframe; no materials/textures)");
}

void pcNativeRendererBeginFrame(void)
{
    unsigned int i;
    if (!native_enabled || !native_initialized) return;
    for (i = 0; i < active_count; i++) {
        unsigned int index = active_indices[i];
        free(commands[index].source);
        memset(&commands[index], 0, sizeof(commands[index]));
    }
    active_count = 0;
    frame_vertex_count = 0;
    frame_number++;
    frame_captured_draws = frame_consumed_draws = frame_triangles = frame_dropped_triangles = 0;
}

void pcNativeRendererCapture(unsigned int ot_index, const void* vertices,
                             unsigned int count, unsigned int primitive_type)
{
    PcNativeCommand* command;
    size_t bytes;
    if (!native_enabled || !native_initialized || !vertices || ot_index >= PC_NATIVE_OT_CAPACITY ||
        count < 3 || count > PC_NATIVE_MAX_FRAME_VERTICES / 3u)
        return;
    if (primitive_type != 3u && primitive_type != 4u && primitive_type != 5u) {
        frame_dropped_triangles++;
        return;
    }
    command = &commands[ot_index];
    if (command->used) {
        free(command->source);
        command->source = NULL;
        command->used = 0;
    }
    bytes = (size_t)count * PC_NATIVE_VERTEX_STRIDE;
    command->source = (unsigned char*)malloc(bytes);
    if (!command->source) { frame_dropped_triangles++; return; }
    memcpy(command->source, vertices, bytes);
    command->count = count;
    command->primitive_type = primitive_type;
    command->used = 1;
    if (active_count < PC_NATIVE_OT_CAPACITY) active_indices[active_count++] = ot_index;
    frame_captured_draws++;
}

static int native_append_vertex(const unsigned char* source)
{
    const float* values = (const float*)source;
    float x = values[8] - 2048.0f, y = values[9] - 2048.0f;
    if (x != x || y != y || x < -4096.0f || x > 4096.0f || y < -4096.0f || y > 4096.0f)
        return 0;
    if (frame_vertex_count >= PC_NATIVE_MAX_FRAME_VERTICES) return 0;
    if (frame_vertex_count >= frame_vertex_capacity) {
        unsigned int next = frame_vertex_capacity ? frame_vertex_capacity * 2u : 4096u;
        PcNativeGpuVertex* resized;
        if (next > PC_NATIVE_MAX_FRAME_VERTICES) next = PC_NATIVE_MAX_FRAME_VERTICES;
        resized = (PcNativeGpuVertex*)realloc(frame_vertices, (size_t)next * sizeof(*frame_vertices));
        if (!resized) return 0;
        frame_vertices = resized;
        frame_vertex_capacity = next;
    }
    frame_vertices[frame_vertex_count].x = x;
    frame_vertices[frame_vertex_count].y = y;
    frame_vertex_count++;
    return 1;
}

static void native_append_triangle(const unsigned char* a, const unsigned char* b, const unsigned char* c)
{
    unsigned int before = frame_vertex_count;
    if (!native_append_vertex(a) || !native_append_vertex(b) || !native_append_vertex(c)) {
        frame_vertex_count = before;
        frame_dropped_triangles++;
        return;
    }
    frame_triangles++;
}

void pcNativeRendererAccumulate(unsigned int ot_index)
{
    PcNativeCommand* command;
    unsigned int i;
    if (!native_enabled || !native_initialized || ot_index >= PC_NATIVE_OT_CAPACITY) return;
    command = &commands[ot_index];
    if (!command->used || !command->source) return;

    frame_consumed_draws++;
    if (command->primitive_type == 3u) {
        for (i = 2; i < command->count; i += 3) {
            const unsigned char* a = command->source + (size_t)(i - 2u) * PC_NATIVE_VERTEX_STRIDE;
            const unsigned char* b = command->source + (size_t)(i - 1u) * PC_NATIVE_VERTEX_STRIDE;
            const unsigned char* c = command->source + (size_t)i * PC_NATIVE_VERTEX_STRIDE;
            uint32_t kick = *(const uint32_t*)(c + 12);
            if ((kick & 0x8000u) == 0) native_append_triangle(a, b, c);
        }
    } else {
        for (i = 2; i < command->count; i++) {
            const unsigned char* a;
            const unsigned char* b;
            const unsigned char* c = command->source + (size_t)i * PC_NATIVE_VERTEX_STRIDE;
            uint32_t kick = *(const uint32_t*)(c + 12);
            if (kick & 0x8000u) continue;
            a = (command->primitive_type == 5u) ? command->source :
                command->source + (size_t)(i - 2u) * PC_NATIVE_VERTEX_STRIDE;
            b = command->source + (size_t)(i - 1u) * PC_NATIVE_VERTEX_STRIDE;
            native_append_triangle(a, b, c);
        }
    }
}

void pcNativeRendererEndFrame(void)
{
    if (!native_enabled || !native_initialized) return;
    glBindFramebuffer(GL_FRAMEBUFFER, native_fbo);
    glViewport(0, 0, PC_NATIVE_WIDTH, PC_NATIVE_HEIGHT);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glClearColor(0.015f, 0.02f, 0.025f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (frame_vertex_count) {
        glUseProgram(native_program);
        glBindVertexArray(native_vao);
        glBindBuffer(GL_ARRAY_BUFFER, native_vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)frame_vertex_count * sizeof(*frame_vertices)),
                     frame_vertices, GL_STREAM_DRAW);
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)frame_vertex_count);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    gsRendererInvalidateGlStateCache();
}

static int native_write_probe_png(const char* filename)
{
    char directory[1024], path[1200];
    unsigned char* rgba = (unsigned char*)malloc((size_t)PC_NATIVE_WIDTH * PC_NATIVE_HEIGHT * 4u);
    unsigned char* rgb = (unsigned char*)malloc((size_t)PC_NATIVE_WIDTH * PC_NATIVE_HEIGHT * 3u);
    int x, y, ok;
    if (!rgba || !rgb) { free(rgba); free(rgb); return 0; }

    glBindFramebuffer(GL_FRAMEBUFFER, native_fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, PC_NATIVE_WIDTH, PC_NATIVE_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    for (y = 0; y < PC_NATIVE_HEIGHT; y++) {
        int source_y = PC_NATIVE_HEIGHT - 1 - y;
        for (x = 0; x < PC_NATIVE_WIDTH; x++) {
            const unsigned char* s = &rgba[((size_t)source_y * PC_NATIVE_WIDTH + x) * 4u];
            unsigned char* d = &rgb[((size_t)y * PC_NATIVE_WIDTH + x) * 3u];
            d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
        }
    }
    pcPlatformExeDir(directory, (int)sizeof(directory));
    snprintf(path, sizeof(path), "%s/%s", directory, filename);
    ok = pcWritePng(path, rgb, PC_NATIVE_WIDTH, PC_NATIVE_HEIGHT);
    free(rgba);
    free(rgb);
    return ok;
}

void pcNativeRendererPresent(unsigned int display_fbp)
{
    char directory[1024], gs_path[1200];
    if (!native_enabled || !native_initialized || frame_number == 0) return;
    if (frame_number != 1 && (frame_number % PC_NATIVE_DUMP_INTERVAL) != 0) return;
    if (!native_write_probe_png("recvx-native-geometry.png"))
        pcLog("pc-native-gpu: could not write recvx-native-geometry.png");
    pcPlatformExeDir(directory, (int)sizeof(directory));
    snprintf(gs_path, sizeof(gs_path), "%s/recvx-gs-reference.png", directory);
    pcGsScreenshot(gs_path, display_fbp);
    gsRendererInvalidateGlStateCache();
    pcLog("pc-native-gpu: frame=%u captured=%u consumed=%u triangles=%u vertices=%u dropped=%u; paired GS/native PNGs saved",
          frame_number, frame_captured_draws, frame_consumed_draws, frame_triangles,
          frame_vertex_count, frame_dropped_triangles);
}
