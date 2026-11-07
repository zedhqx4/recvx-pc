/* Deterministic tests for the first native CPU rasterizer milestone.
 *
 *   cc -std=c99 -O2 -Wall -Wextra gs_native_rasterizer_test.c -lm -o gs-native-test
 *   ./gs-native-test
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define GS_NATIVE_TEST 1
#include "../src/gs/gs_native.c"

void pcLog(const char* fmt, ...)
{
    (void)fmt;
}

static GsDrawState base_state(void)
{
    GsDrawState st;
    memset(&st, 0, sizeof(st));
    st.frame = ((u64)1 << 16) | ((u64)GS_PSMCT32 << 24);
    st.scissor = ((u64)63 << 16) | ((u64)63 << 48);
    st.test = (u64)1 << 17;
    st.zbuf = (u64)1 << 32;
    return st;
}

static void test_sprite_and_scissor(void)
{
    GsDrawState st = base_state();
    GsVertex v[2];

    memset(v, 0, sizeof(v));
    st.type = 6;
    st.scissor = (u64)2 | ((u64)5 << 16) | ((u64)2 << 32) | ((u64)5 << 48);
    v[0].x = 0.0f; v[0].y = 0.0f;
    v[1].x = 10.0f; v[1].y = 10.0f;
    v[1].r = 200; v[1].g = 120; v[1].b = 40; v[1].a = 128;

    gsNativeDraw(&st, v, 2);
    assert(native_targets[0].used);
    assert(native_targets[0].pixels[3 * native_targets[0].width + 3] == 0xFF2878C8u);
    assert(native_targets[0].pixels[3 * native_targets[0].width + 1] == 0u);
    assert(native_targets[0].pixels[3 * native_targets[0].width + 6] == 0u);
}

static void test_gouraud_triangle(void)
{
    GsDrawState st = base_state();
    GsVertex v[3];
    unsigned int pixel;

    memset(v, 0, sizeof(v));
    st.type = 3;
    st.iip = 1;
    v[0].x = 8.0f;  v[0].y = 8.0f;  v[0].r = 255; v[0].a = 128;
    v[1].x = 24.0f; v[1].y = 8.0f; v[1].g = 255; v[1].a = 128;
    v[2].x = 8.0f;  v[2].y = 24.0f; v[2].b = 255; v[2].a = 128;
    gsNativeDraw(&st, v, 3);
    pixel = native_targets[0].pixels[12 * native_targets[0].width + 12];
    assert(pixel != 0u);
    assert((pixel & 0x00FFFFFFu) != 0u);
}

static void test_point_and_line(void)
{
    GsDrawState st = base_state();
    GsVertex v[2];

    memset(v, 0, sizeof(v));
    st.type = 0;
    v[0].x = 30.0f; v[0].y = 30.0f; v[0].b = 255; v[0].a = 128;
    gsNativeDraw(&st, v, 1);
    assert(native_targets[0].pixels[30 * native_targets[0].width + 30] == 0xFFFF0000u);

    st.type = 1;
    v[0].x = 10.0f; v[0].y = 40.0f; v[0].r = 255; v[0].b = 0; v[0].a = 128;
    v[1].x = 20.0f; v[1].y = 40.0f; v[1].r = 255; v[1].a = 128;
    gsNativeDraw(&st, v, 2);
    assert(native_targets[0].pixels[40 * native_targets[0].width + 15] == 0xFF0000FFu);
}

static void test_unsupported_texture_is_counted_and_ignored(void)
{
    GsDrawState st = base_state();
    GsVertex v[3];
    unsigned int before;

    memset(v, 0, sizeof(v));
    st.type = 3;
    st.tme = 1;
    v[0].x = 1.0f; v[0].y = 1.0f;
    v[1].x = 30.0f; v[1].y = 1.0f;
    v[2].x = 1.0f; v[2].y = 30.0f;
    v[0].r = v[1].r = v[2].r = 255;
    v[0].a = v[1].a = v[2].a = 128;

    before = native_targets[0].pixels[2 * native_targets[0].width + 2];
    gsNativeDraw(&st, v, 3);
    assert(native_stats.skipped[GS_NATIVE_SKIP_TEXTURE] == 1);
    assert(native_targets[0].pixels[2 * native_targets[0].width + 2] == before);
}

static void test_independent_framebuffers(void)
{
    GsDrawState st = base_state();
    GsVertex v[2];

    memset(v, 0, sizeof(v));
    st.type = 6;
    st.frame = (u64)2 | ((u64)1 << 16) | ((u64)GS_PSMCT32 << 24);
    v[0].x = 2.0f; v[0].y = 2.0f;
    v[1].x = 4.0f; v[1].y = 4.0f;
    v[1].g = 255; v[1].a = 128;
    gsNativeDraw(&st, v, 2);

    assert(native_targets[1].used);
    assert(native_targets[1].fbp == 2);
    assert(native_targets[1].fbw == 1);
    assert(native_targets[1].pixels[2 * native_targets[1].width + 2] == 0xFF00FF00u);
}

int main(void)
{
    gsNativeInit();
    test_sprite_and_scissor();
    test_gouraud_triangle();
    test_point_and_line();
    test_unsupported_texture_is_counted_and_ignored();
    test_independent_framebuffers();
    puts("native renderer tests: ok (sprite/scissor, triangle interpolation, point/line, unsupported-state isolation, independent buffers)");
    gsNativeFreeTargets();
    return 0;
}
