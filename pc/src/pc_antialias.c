/*
 * Optional post-process anti-aliasing for the native OpenGL renderer.
 * FXAA: NVIDIA FXAA-style single pass implementation.
 * SMAA 1x: GLSL port based on dmnsgn/shaders-smaa, which follows the
 * reference implementation by Jorge Jimenez et al. See pc/res/SMAA-LICENSE.txt.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pc_antialias.h"
#include "pc_gl.h"
#include "pc_platform.h"

#ifndef GL_RG8
#define GL_RG8 0x822B
#endif
#ifndef GL_RG
#define GL_RG 0x8227
#endif
#ifndef GL_R8
#define GL_R8 0x8229
#endif
#ifndef GL_RED
#define GL_RED 0x1903
#endif

enum { PC_AA_FXAA = 1, PC_AA_SMAA = 2 };

typedef struct {
    GLuint fxaa_program, edges_program, weights_program, blend_program;
    GLuint vao;
    GLuint color_texture[2], color_fbo[2];
    GLuint edges_texture, edges_fbo, weights_texture, weights_fbo;
    GLuint area_texture, search_texture;
    int width, height, flags;
    GLint fxaa_color, fxaa_texel;
    GLint edges_color, edges_texel;
    GLint weights_edges, weights_area, weights_search, weights_viewport, weights_texel, weights_subsample;
    GLint blend_color, blend_weights, blend_texel;
} PcAaState;

static PcAaState aa;

static const char* shader_fxaa_vert =
    "#version 330 core\n"
    "out vec2 vTexCoord0;\n"
    "void main() {\n"
    "    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
    "    vTexCoord0 = p;\n"
    "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n"
    "\n";

static const char* shader_fxaa_frag =
    "#version 330 core\n"
    "in vec2 vTexCoord0;\n"
    "out vec4 oColor;\n"
    "uniform sampler2D uColorTexture;\n"
    "uniform vec2 uTexelSize;\n"
    "\n"
    "float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }\n"
    "\n"
    "void main() {\n"
    "    vec3 rgbM  = texture(uColorTexture, vTexCoord0).rgb;\n"
    "    vec3 rgbNW = texture(uColorTexture, vTexCoord0 + vec2(-1.0, -1.0) * uTexelSize).rgb;\n"
    "    vec3 rgbNE = texture(uColorTexture, vTexCoord0 + vec2( 1.0, -1.0) * uTexelSize).rgb;\n"
    "    vec3 rgbSW = texture(uColorTexture, vTexCoord0 + vec2(-1.0,  1.0) * uTexelSize).rgb;\n"
    "    vec3 rgbSE = texture(uColorTexture, vTexCoord0 + vec2( 1.0,  1.0) * uTexelSize).rgb;\n"
    "\n"
    "    float lumaM  = luma(rgbM);\n"
    "    float lumaNW = luma(rgbNW);\n"
    "    float lumaNE = luma(rgbNE);\n"
    "    float lumaSW = luma(rgbSW);\n"
    "    float lumaSE = luma(rgbSE);\n"
    "\n"
    "    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));\n"
    "    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));\n"
    "\n"
    "    vec2 dir;\n"
    "    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));\n"
    "    dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));\n"
    "\n"
    "    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 / 8.0), 1.0 / 128.0);\n"
    "    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);\n"
    "    dir = clamp(dir * rcpDirMin, vec2(-8.0), vec2(8.0)) * uTexelSize;\n"
    "\n"
    "    vec3 rgbA = 0.5 * (\n"
    "        texture(uColorTexture, vTexCoord0 + dir * (1.0 / 3.0 - 0.5)).rgb +\n"
    "        texture(uColorTexture, vTexCoord0 + dir * (2.0 / 3.0 - 0.5)).rgb);\n"
    "    vec3 rgbB = rgbA * 0.5 + 0.25 * (\n"
    "        texture(uColorTexture, vTexCoord0 + dir * -0.5).rgb +\n"
    "        texture(uColorTexture, vTexCoord0 + dir *  0.5).rgb);\n"
    "\n"
    "    float lumaB = luma(rgbB);\n"
    "    vec3 result = (lumaB < lumaMin || lumaB > lumaMax) ? rgbA : rgbB;\n"
    "    float alpha = texture(uColorTexture, vTexCoord0).a;\n"
    "    oColor = vec4(result, alpha);\n"
    "}\n"
    "\n";

static const char* shader_smaa_edges_vert =
    "#version 330 core\n"
    "\n"
    "#define mad(a, b, c) (a * b + c)\n"
    "\n"
    "#define aPosition (vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)) * 2.0 - 1.0)\n"
    "\n"
    "uniform vec2 uTexelSize;\n"
    "\n"
    "out vec2 vTexCoord0;\n"
    "out vec4 vOffset[3];\n"
    "\n"
    "void main() {\n"
    "\tvTexCoord0 = vec2((aPosition + 1.0) / 2.0);\n"
    "\n"
    "  vOffset[0] = mad(vec4(uTexelSize, uTexelSize), vec4(-1.0, 0.0, 0.0, -1.0), vTexCoord0.xyxy);\n"
    "  vOffset[1] = mad(vec4(uTexelSize, uTexelSize), vec4( 1.0, 0.0, 0.0,  1.0), vTexCoord0.xyxy);\n"
    "  vOffset[2] = mad(vec4(uTexelSize, uTexelSize), vec4(-2.0, 0.0, 0.0, -2.0), vTexCoord0.xyxy);\n"
    "\n"
    "  gl_Position = vec4(aPosition, 0.0, 1.0);\n"
    "}\n"
    "\n";

static const char* shader_smaa_weights_vert =
    "#version 330 core\n"
    "#define SMAA_PRESET_HIGH\n"
    "\n"
    "#define mad(a, b, c) (a * b + c)\n"
    "\n"
    "#if defined(SMAA_PRESET_LOW)\n"
    "#define SMAA_MAX_SEARCH_STEPS 4\n"
    "#elif defined(SMAA_PRESET_MEDIUM)\n"
    "#define SMAA_MAX_SEARCH_STEPS 8\n"
    "#elif defined(SMAA_PRESET_HIGH)\n"
    "#define SMAA_MAX_SEARCH_STEPS 16\n"
    "#elif defined(SMAA_PRESET_ULTRA)\n"
    "#define SMAA_MAX_SEARCH_STEPS 32\n"
    "#endif\n"
    "\n"
    "#ifndef SMAA_MAX_SEARCH_STEPS\n"
    "#define SMAA_MAX_SEARCH_STEPS 16\n"
    "#endif\n"
    "\n"
    "#define aPosition (vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)) * 2.0 - 1.0)\n"
    "\n"
    "uniform vec2 uViewportSize;\n"
    "uniform vec2 uTexelSize;\n"
    "\n"
    "out vec2 vTexCoord0;\n"
    "out vec2 vPixCoord;\n"
    "out vec4 vOffset[3];\n"
    "\n"
    "void main() {\n"
    "\tvTexCoord0 = vec2((aPosition + 1.0) / 2.0);\n"
    "\n"
    "  vPixCoord = vTexCoord0 * uViewportSize;\n"
    "\n"
    "  // We will use these offsets for the searches later on (see @PSEUDO_GATHER4):\n"
    "  vOffset[0] = mad(vec4(uTexelSize, uTexelSize), vec4(-0.25, -0.125,  1.25, -0.125), vTexCoord0.xyxy);\n"
    "  vOffset[1] = mad(vec4(uTexelSize, uTexelSize), vec4(-0.125, -0.25, -0.125,  1.25), vTexCoord0.xyxy);\n"
    "\n"
    "  // And these for the searches, they indicate the ends of the loops:\n"
    "  vOffset[2] = mad(\n"
    "    vec4(uTexelSize.x, uTexelSize.x, uTexelSize.y, uTexelSize.y),\n"
    "    vec4(-2.0, 2.0, -2.0, 2.0) * float(SMAA_MAX_SEARCH_STEPS),\n"
    "    vec4(vOffset[0].xz, vOffset[1].yw)\n"
    "  );\n"
    "\n"
    "  gl_Position = vec4(aPosition, 0.0, 1.0);\n"
    "}\n"
    "\n";

static const char* shader_smaa_blend_vert =
    "#version 330 core\n"
    "\n"
    "#define mad(a, b, c) (a * b + c)\n"
    "\n"
    "#define aPosition (vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)) * 2.0 - 1.0)\n"
    "\n"
    "uniform vec2 uTexelSize;\n"
    "\n"
    "out vec2 vTexCoord0;\n"
    "out vec4 vOffset;\n"
    "\n"
    "void main() {\n"
    "\tvTexCoord0 = vec2((aPosition + 1.0) / 2.0);\n"
    "\tvOffset = mad(vec4(uTexelSize, uTexelSize), vec4(1.0, 0.0, 0.0,  1.0), vTexCoord0.xyxy);\n"
    "\n"
    "  gl_Position = vec4(aPosition, 0.0, 1.0);\n"
    "}\n"
    "\n";

static const char* shader_smaa_edges_frag =
    "#version 330 core\n"
    "#define SMAA_EDGES_LUMA\n"
    "out vec4 oColor;\n"
    "\n"
    "#ifndef SMAA_THRESHOLD\n"
    "  #define SMAA_THRESHOLD 0.1\n"
    "#endif\n"
    "\n"
    "#ifndef SMAA_PREDICATION\n"
    "  #define SMAA_PREDICATION 0\n"
    "#endif\n"
    "\n"
    "/**\n"
    " * Threshold to be used in the additional predication buffer.\n"
    " *\n"
    " * Range: depends on the input, so you'll have to find the magic number that\n"
    " * works for you.\n"
    " */\n"
    "#ifndef SMAA_PREDICATION_THRESHOLD\n"
    "  #define SMAA_PREDICATION_THRESHOLD 0.01\n"
    "#endif\n"
    "\n"
    "/**\n"
    " * How much to scale the global threshold used for luma or color edge\n"
    " * detection when using predication.\n"
    " *\n"
    " * Range: [1, 5]\n"
    " */\n"
    "#ifndef SMAA_PREDICATION_SCALE\n"
    "  #define SMAA_PREDICATION_SCALE 2.0\n"
    "#endif\n"
    "\n"
    "/**\n"
    " * How much to locally decrease the threshold.\n"
    " *\n"
    " * Range: [0, 1]\n"
    " */\n"
    "#ifndef SMAA_PREDICATION_STRENGTH\n"
    "  #define SMAA_PREDICATION_STRENGTH 0.4\n"
    "#endif\n"
    "\n"
    "#if defined(SMAA_EDGES_DEPTH) || SMAA_PREDICATION\n"
    "  uniform sampler2D uDepthTexture;\n"
    "#endif\n"
    "\n"
    "#ifdef SMAA_EDGES_DEPTH\n"
    "  #ifndef SMAA_DEPTH_THRESHOLD\n"
    "    #define SMAA_DEPTH_THRESHOLD (0.1 * SMAA_THRESHOLD)\n"
    "  #endif\n"
    "\n"
    "#else\n"
    "  #ifndef SMAA_LOCAL_CONTRAST_ADAPTATION_FACTOR\n"
    "    #define SMAA_LOCAL_CONTRAST_ADAPTATION_FACTOR 2.0\n"
    "  #endif\n"
    "\n"
    "  uniform sampler2D uColorTexture;\n"
    "\n"
    "  /**\n"
    "   * Edge detection expects gamma-encoded color: sampling an sRGB texture\n"
    "   * decodes it to linear, so it has to be re-encoded before thresholding.\n"
    "   */\n"
    "  vec3 SMAASampleColor(sampler2D tex, vec2 texcoord) {\n"
    "    vec3 color = texture(tex, texcoord).rgb;\n"
    "    #ifdef SMAA_SRGB_INPUT\n"
    "      return mix(\n"
    "        color * 12.92,\n"
    "        1.055 * pow(color, vec3(1.0 / 2.4)) - 0.055,\n"
    "        step(vec3(0.0031308), color)\n"
    "      );\n"
    "    #else\n"
    "      return color;\n"
    "    #endif\n"
    "  }\n"
    "#endif\n"
    "\n"
    "in vec2 vTexCoord0;\n"
    "in vec4 vOffset[3];\n"
    "\n"
    "#if defined(SMAA_EDGES_DEPTH) || SMAA_PREDICATION\n"
    "  /**\n"
    "  * Gathers current pixel, and the top-left neighbors.\n"
    "  */\n"
    "  vec3 SMAAGatherNeighbours(vec2 texcoord, vec4 offset[3], sampler2D tex) {\n"
    "    float P = texture(tex, texcoord).r;\n"
    "    float Pleft = texture(tex, offset[0].xy).r;\n"
    "    float Ptop  = texture(tex, offset[0].zw).r;\n"
    "\n"
    "    return vec3(P, Pleft, Ptop);\n"
    "  }\n"
    "#endif\n"
    "\n"
    "#if SMAA_PREDICATION\n"
    "  vec2 SMAACalculatePredicatedThreshold(vec2 texcoord, vec4 offset[3], sampler2D predicationTex) {\n"
    "    vec3 neighbours = SMAAGatherNeighbours(texcoord, offset, predicationTex);\n"
    "    vec2 delta = abs(neighbours.xx - neighbours.yz);\n"
    "    vec2 edges = step(SMAA_PREDICATION_THRESHOLD, delta);\n"
    "    return SMAA_PREDICATION_SCALE * SMAA_THRESHOLD * (1.0 - SMAA_PREDICATION_STRENGTH * edges);\n"
    "  }\n"
    "#endif\n"
    "\n"
    "#ifdef SMAA_EDGES_DEPTH\n"
    "  vec2 SMAADepthEdgeDetection(vec2 texcoord, vec4 offset[3], sampler2D depthTex) {\n"
    "    vec3 neighbours = SMAAGatherNeighbours(texcoord, offset, depthTex);\n"
    "    vec2 delta = abs(neighbours.xx - vec2(neighbours.y, neighbours.z));\n"
    "    vec2 edges = step(SMAA_DEPTH_THRESHOLD, delta);\n"
    "\n"
    "    if (dot(edges, vec2(1.0, 1.0)) == 0.0)\n"
    "        discard;\n"
    "\n"
    "    return edges;\n"
    "  }\n"
    "#endif\n"
    "\n"
    "#ifdef SMAA_EDGES_LUMA\n"
    "  vec2 SMAALumaEdgeDetection(vec2 texcoord, vec4 offset[3], sampler2D colorTex\n"
    "  #if SMAA_PREDICATION\n"
    "  , sampler2D predicationTex\n"
    "  #endif\n"
    "  ) {\n"
    "    #if SMAA_PREDICATION\n"
    "    vec2 threshold = SMAACalculatePredicatedThreshold(texcoord, offset, predicationTex);\n"
    "    #else\n"
    "    vec2 threshold = vec2(SMAA_THRESHOLD);\n"
    "    #endif\n"
    "\n"
    "    // Calculate lumas:\n"
    "    vec3 weights = vec3(0.2126, 0.7152, 0.0722);\n"
    "    float L = dot(SMAASampleColor(colorTex, texcoord), weights);\n"
    "\n"
    "    float Lleft = dot(SMAASampleColor(colorTex, offset[0].xy), weights);\n"
    "    float Ltop  = dot(SMAASampleColor(colorTex, offset[0].zw), weights);\n"
    "\n"
    "    // We do the usual threshold:\n"
    "    vec4 delta;\n"
    "    delta.xy = abs(L - vec2(Lleft, Ltop));\n"
    "    vec2 edges = step(threshold, delta.xy);\n"
    "\n"
    "    // Then discard if there is no edge:\n"
    "    if (dot(edges, vec2(1.0, 1.0)) == 0.0)\n"
    "        discard;\n"
    "\n"
    "    // Calculate right and bottom deltas:\n"
    "    float Lright = dot(SMAASampleColor(colorTex, offset[1].xy), weights);\n"
    "    float Lbottom  = dot(SMAASampleColor(colorTex, offset[1].zw), weights);\n"
    "    delta.zw = abs(L - vec2(Lright, Lbottom));\n"
    "\n"
    "    // Calculate the maximum delta in the direct neighborhood:\n"
    "    vec2 maxDelta = max(delta.xy, delta.zw);\n"
    "\n"
    "    // Calculate left-left and top-top deltas:\n"
    "    float Lleftleft = dot(SMAASampleColor(colorTex, offset[2].xy), weights);\n"
    "    float Ltoptop = dot(SMAASampleColor(colorTex, offset[2].zw), weights);\n"
    "    delta.zw = abs(vec2(Lleft, Ltop) - vec2(Lleftleft, Ltoptop));\n"
    "\n"
    "    // Calculate the final maximum delta:\n"
    "    maxDelta = max(maxDelta.xy, delta.zw);\n"
    "    float finalDelta = max(maxDelta.x, maxDelta.y);\n"
    "\n"
    "    // Local contrast adaptation:\n"
    "    edges.xy *= step(finalDelta, SMAA_LOCAL_CONTRAST_ADAPTATION_FACTOR * delta.xy);\n"
    "\n"
    "    return edges;\n"
    "  }\n"
    "#endif\n"
    "\n"
    "#ifdef SMAA_EDGES_COLOR\n"
    "  vec2 SMAAColorEdgeDetection(vec2 texcoord, vec4 offset[3], sampler2D colorTex\n"
    "  #if SMAA_PREDICATION\n"
    "  , sampler2D predicationTex\n"
    "  #endif\n"
    "  ) {\n"
    "    // Calculate the threshold:\n"
    "    #if SMAA_PREDICATION\n"
    "    vec2 threshold = SMAACalculatePredicatedThreshold(texcoord, offset, predicationTex);\n"
    "    #else\n"
    "    vec2 threshold = vec2(SMAA_THRESHOLD);\n"
    "    #endif\n"
    "\n"
    "    // Calculate color deltas:\n"
    "    vec4 delta;\n"
    "    vec3 c = SMAASampleColor(colorTex, texcoord);\n"
    "\n"
    "    vec3 cLeft = SMAASampleColor(colorTex, offset[0].xy);\n"
    "    vec3 t = abs(c - cLeft);\n"
    "    delta.x = max(max(t.r, t.g), t.b);\n"
    "\n"
    "    vec3 cTop  = SMAASampleColor(colorTex, offset[0].zw);\n"
    "    t = abs(c - cTop);\n"
    "    delta.y = max(max(t.r, t.g), t.b);\n"
    "\n"
    "    // We do the usual threshold:\n"
    "    vec2 edges = step(threshold, delta.xy);\n"
    "\n"
    "    // Then discard if there is no edge:\n"
    "    if (dot(edges, vec2(1.0, 1.0)) == 0.0)\n"
    "        discard;\n"
    "\n"
    "    // Calculate right and bottom deltas:\n"
    "    vec3 cRight = SMAASampleColor(colorTex, offset[1].xy);\n"
    "    t = abs(c - cRight);\n"
    "    delta.z = max(max(t.r, t.g), t.b);\n"
    "\n"
    "    vec3 cBottom  = SMAASampleColor(colorTex, offset[1].zw);\n"
    "    t = abs(c - cBottom);\n"
    "    delta.w = max(max(t.r, t.g), t.b);\n"
    "\n"
    "    // Calculate the maximum delta in the direct neighborhood:\n"
    "    vec2 maxDelta = max(delta.xy, delta.zw);\n"
    "\n"
    "    // Calculate left-left and top-top deltas:\n"
    "    vec3 cLeftLeft  = SMAASampleColor(colorTex, offset[2].xy);\n"
    "    t = abs(c - cLeftLeft);\n"
    "    delta.z = max(max(t.r, t.g), t.b);\n"
    "\n"
    "    vec3 cTopTop = SMAASampleColor(colorTex, offset[2].zw);\n"
    "    t = abs(c - cTopTop);\n"
    "    delta.w = max(max(t.r, t.g), t.b);\n"
    "\n"
    "    // Calculate the final maximum delta:\n"
    "    maxDelta = max(maxDelta.xy, delta.zw);\n"
    "    float finalDelta = max(maxDelta.x, maxDelta.y);\n"
    "\n"
    "    // Local contrast adaptation:\n"
    "    edges.xy *= step(finalDelta, SMAA_LOCAL_CONTRAST_ADAPTATION_FACTOR * delta.xy);\n"
    "\n"
    "    return edges;\n"
    "  }\n"
    "#endif\n"
    "\n"
    "void main() {\n"
    "  vec4 color = vec4(0.0);\n"
    "\n"
    "  #ifdef SMAA_EDGES_DEPTH\n"
    "    color.xy = SMAADepthEdgeDetection(vTexCoord0, vOffset, uDepthTexture);\n"
    "  #elif defined(SMAA_EDGES_LUMA)\n"
    "    color.xy = SMAALumaEdgeDetection(vTexCoord0, vOffset, uColorTexture\n"
    "      #if SMAA_PREDICATION\n"
    "      , uDepthTexture\n"
    "      #endif\n"
    "    );\n"
    "  #elif defined(SMAA_EDGES_COLOR)\n"
    "    color.xy = SMAAColorEdgeDetection(vTexCoord0, vOffset, uColorTexture\n"
    "      #if SMAA_PREDICATION\n"
    "      , uDepthTexture\n"
    "      #endif\n"
    "    );\n"
    "  #endif\n"
    "\n"
    "  oColor = color;\n"
    "}\n"
    "\n";

static const char* shader_smaa_weights_frag =
    "#version 330 core\n"
    "#define SMAA_PRESET_HIGH\n"
    "out vec4 oColor;\n"
    "\n"
    "\n"
    "// Defaults\n"
    "#ifndef SMAA_THRESHOLD\n"
    "#define SMAA_THRESHOLD 0.1\n"
    "#endif\n"
    "#ifndef SMAA_MAX_SEARCH_STEPS\n"
    "#define SMAA_MAX_SEARCH_STEPS 16\n"
    "#endif\n"
    "#ifndef SMAA_MAX_SEARCH_STEPS_DIAG\n"
    "#define SMAA_MAX_SEARCH_STEPS_DIAG 8\n"
    "#endif\n"
    "#ifndef SMAA_CORNER_ROUNDING\n"
    "#define SMAA_CORNER_ROUNDING 25\n"
    "#endif\n"
    "\n"
    "// Non-Configurable Defines\n"
    "#define SMAA_AREATEX_MAX_DISTANCE 16\n"
    "#define SMAA_AREATEX_MAX_DISTANCE_DIAG 20\n"
    "#define SMAA_AREATEX_PIXEL_SIZE (1.0 / vec2(160.0, 560.0))\n"
    "#define SMAA_AREATEX_SUBTEX_SIZE (1.0 / 7.0)\n"
    "#define SMAA_SEARCHTEX_SIZE vec2(66.0, 33.0)\n"
    "#define SMAA_SEARCHTEX_PACKED_SIZE vec2(64.0, 16.0)\n"
    "#define SMAA_CORNER_ROUNDING_NORM (float(SMAA_CORNER_ROUNDING) / 100.0)\n"
    "\n"
    "// Texture Access Defines\n"
    "#ifndef SMAA_AREATEX_SELECT\n"
    "#define SMAA_AREATEX_SELECT(sample) sample.rg\n"
    "#endif\n"
    "\n"
    "#ifndef SMAA_SEARCHTEX_SELECT\n"
    "#define SMAA_SEARCHTEX_SELECT(sample) sample.r\n"
    "#endif\n"
    "\n"
    "uniform vec2 uViewportSize;\n"
    "uniform vec2 uTexelSize;\n"
    "// Zero for SMAA 1x, see @SUBSAMPLE_INDICES in SMAA.hlsl for the temporal modes.\n"
    "uniform vec4 uSubsampleIndices;\n"
    "\n"
    "uniform sampler2D uEdgesTexture;\n"
    "uniform sampler2D uAreaTexture;\n"
    "uniform sampler2D uSearchTexture;\n"
    "\n"
    "in vec2 vTexCoord0;\n"
    "in vec4 vOffset[3];\n"
    "in vec2 vPixCoord;\n"
    "\n"
    "#define mad(a, b, c) (a * b + c)\n"
    "#define saturate(a) clamp(a, 0.0, 1.0)\n"
    "#define round(v) floor(v + 0.5)\n"
    "#define SMAASampleLevelZeroOffset(tex, coord, offset) texture(tex, coord + offset * uTexelSize)\n"
    "\n"
    "/**\n"
    " * Conditional move:\n"
    " */\n"
    "void SMAAMovc(bvec2 cond, inout vec2 variable, vec2 value) {\n"
    "  if (cond.x) variable.x = value.x;\n"
    "  if (cond.y) variable.y = value.y;\n"
    "}\n"
    "\n"
    "void SMAAMovc(bvec4 cond, inout vec4 variable, vec4 value) {\n"
    "  SMAAMovc(cond.xy, variable.xy, value.xy);\n"
    "  SMAAMovc(cond.zw, variable.zw, value.zw);\n"
    "}\n"
    "\n"
    "/**\n"
    " * Allows to decode two binary values from a bilinear-filtered access.\n"
    " */\n"
    "vec2 SMAADecodeDiagBilinearAccess(vec2 e) {\n"
    "  // Bilinear access for fetching 'e' have a 0.25 offset, and we are\n"
    "  // interested in the R and G edges:\n"
    "  //\n"
    "  // +---G---+-------+\n"
    "  // |   x o R   x   |\n"
    "  // +-------+-------+\n"
    "  //\n"
    "  // Then, if one of these edge is enabled:\n"
    "  //   Red:   (0.75 * X + 0.25 * 1) => 0.25 or 1.0\n"
    "  //   Green: (0.75 * 1 + 0.25 * X) => 0.75 or 1.0\n"
    "  //\n"
    "  // This function will unpack the values (mad + mul + round):\n"
    "  // wolframalpha.com: round(x * abs(5 * x - 5 * 0.75)) plot 0 to 1\n"
    "  e.r = e.r * abs(5.0 * e.r - 5.0 * 0.75);\n"
    "  return round(e);\n"
    "}\n"
    "\n"
    "vec4 SMAADecodeDiagBilinearAccess(vec4 e) {\n"
    "  e.rb = e.rb * abs(5.0 * e.rb - 5.0 * 0.75);\n"
    "  return round(e);\n"
    "}\n"
    "\n"
    "/**\n"
    " * These functions allows to perform diagonal pattern searches.\n"
    " */\n"
    "vec2 SMAASearchDiag1(sampler2D edgesTex, vec2 texcoord, vec2 dir, out vec2 e) {\n"
    "  vec4 coord = vec4(texcoord, -1.0, 1.0);\n"
    "  vec3 t = vec3(uTexelSize, 1.0);\n"
    "\n"
    "  for (int i = 0; i < SMAA_MAX_SEARCH_STEPS_DIAG; i++) {\n"
    "    if (!(coord.z < float(SMAA_MAX_SEARCH_STEPS_DIAG - 1) && coord.w > 0.9)) break;\n"
    "    coord.xyz = mad(t, vec3(dir, 1.0), coord.xyz);\n"
    "    e = texture(edgesTex, coord.xy).rg; // LinearSampler\n"
    "    coord.w = dot(e, vec2(0.5, 0.5));\n"
    "  }\n"
    "  return coord.zw;\n"
    "}\n"
    "\n"
    "vec2 SMAASearchDiag2(sampler2D edgesTex, vec2 texcoord, vec2 dir, out vec2 e) {\n"
    "  vec4 coord = vec4(texcoord, -1.0, 1.0);\n"
    "  coord.x += 0.25 * uTexelSize.x; // See @SearchDiag2Optimization\n"
    "  vec3 t = vec3(uTexelSize, 1.0);\n"
    "\n"
    "  for (int i = 0; i < SMAA_MAX_SEARCH_STEPS_DIAG; i++) {\n"
    "    if (!(coord.z < float(SMAA_MAX_SEARCH_STEPS_DIAG - 1) && coord.w > 0.9)) break;\n"
    "    coord.xyz = mad(t, vec3(dir, 1.0), coord.xyz);\n"
    "\n"
    "    // @SearchDiag2Optimization\n"
    "    // Fetch both edges at once using bilinear filtering:\n"
    "    e = texture(edgesTex, coord.xy).rg; // LinearSampler\n"
    "    e = SMAADecodeDiagBilinearAccess(e);\n"
    "\n"
    "    // Non-optimized version:\n"
    "    // e.g = texture(edgesTex, coord.xy).g; // LinearSampler\n"
    "    // e.r = SMAASampleLevelZeroOffset(edgesTex, coord.xy, vec2(1, 0)).r;\n"
    "\n"
    "    coord.w = dot(e, vec2(0.5, 0.5));\n"
    "  }\n"
    "  return coord.zw;\n"
    "}\n"
    "\n"
    "/**\n"
    " * Similar to SMAAArea, this calculates the area corresponding to a certain\n"
    " * diagonal distance and crossing edges 'e'.\n"
    " */\n"
    "vec2 SMAAAreaDiag(sampler2D areaTex, vec2 dist, vec2 e, float offset) {\n"
    "  vec2 texcoord = mad(vec2(SMAA_AREATEX_MAX_DISTANCE_DIAG, SMAA_AREATEX_MAX_DISTANCE_DIAG), e, dist);\n"
    "\n"
    "  // We do a scale and bias for mapping to texel space:\n"
    "  texcoord = mad(SMAA_AREATEX_PIXEL_SIZE, texcoord, 0.5 * SMAA_AREATEX_PIXEL_SIZE);\n"
    "\n"
    "  // Diagonal areas are on the second half of the texture:\n"
    "  texcoord.x += 0.5;\n"
    "\n"
    "  // Move to proper place, according to the subpixel offset:\n"
    "  texcoord.y += SMAA_AREATEX_SUBTEX_SIZE * offset;\n"
    "\n"
    "  // Do it!\n"
    "  return SMAA_AREATEX_SELECT(texture(areaTex, texcoord)); // LinearSampler\n"
    "}\n"
    "\n"
    "/**\n"
    " * This searches for diagonal patterns and returns the corresponding weights.\n"
    " */\n"
    "vec2 SMAACalculateDiagWeights(sampler2D edgesTex, sampler2D areaTex, vec2 texcoord, vec2 e, vec4 subsampleIndices) {\n"
    "  vec2 weights = vec2(0.0, 0.0);\n"
    "\n"
    "  // Search for the line ends:\n"
    "  vec4 d;\n"
    "  vec2 end;\n"
    "  if (e.r > 0.0) {\n"
    "      d.xz = SMAASearchDiag1(edgesTex, texcoord, vec2(-1.0,  1.0), end);\n"
    "      d.x += float(end.y > 0.9);\n"
    "  } else\n"
    "      d.xz = vec2(0.0, 0.0);\n"
    "  d.yw = SMAASearchDiag1(edgesTex, texcoord, vec2(1.0, -1.0), end);\n"
    "\n"
    "  if (d.x + d.y > 2.0) { // d.x + d.y + 1 > 3\n"
    "    // Fetch the crossing edges:\n"
    "    vec4 coords = mad(vec4(-d.x + 0.25, d.x, d.y, -d.y - 0.25), vec4(uTexelSize, uTexelSize), texcoord.xyxy);\n"
    "    vec4 c;\n"
    "    c.xy = SMAASampleLevelZeroOffset(edgesTex, coords.xy, vec2(-1,  0)).rg;\n"
    "    c.zw = SMAASampleLevelZeroOffset(edgesTex, coords.zw, vec2( 1,  0)).rg;\n"
    "    c.yxwz = SMAADecodeDiagBilinearAccess(c.xyzw);\n"
    "\n"
    "    // Non-optimized version:\n"
    "    // vec4 coords = mad(vec4(-d.x, d.x, d.y, -d.y), vec4(uTexelSize, uTexelSize), texcoord.xyxy);\n"
    "    // vec4 c;\n"
    "    // c.x = SMAASampleLevelZeroOffset(edgesTex, coords.xy, vec2(-1,  0)).g;\n"
    "    // c.y = SMAASampleLevelZeroOffset(edgesTex, coords.xy, vec2( 0,  0)).r;\n"
    "    // c.z = SMAASampleLevelZeroOffset(edgesTex, coords.zw, vec2( 1,  0)).g;\n"
    "    // c.w = SMAASampleLevelZeroOffset(edgesTex, coords.zw, vec2( 1, -1)).r;\n"
    "\n"
    "    // Merge crossing edges at each side into a single value:\n"
    "    vec2 cc = mad(vec2(2.0, 2.0), c.xz, c.yw);\n"
    "\n"
    "    // Remove the crossing edge if we didn't found the end of the line:\n"
    "    SMAAMovc(bvec2(step(0.9, d.zw)), cc, vec2(0.0, 0.0));\n"
    "\n"
    "    // Fetch the areas for this line:\n"
    "    weights += SMAAAreaDiag(areaTex, d.xy, cc, subsampleIndices.z);\n"
    "  }\n"
    "\n"
    "  // Search for the line ends:\n"
    "  d.xz = SMAASearchDiag2(edgesTex, texcoord, vec2(-1.0, -1.0), end);\n"
    "  if (SMAASampleLevelZeroOffset(edgesTex, texcoord, vec2(1, 0)).r > 0.0) {\n"
    "    d.yw = SMAASearchDiag2(edgesTex, texcoord, vec2(1.0, 1.0), end);\n"
    "    d.y += float(end.y > 0.9);\n"
    "  } else {\n"
    "    d.yw = vec2(0.0, 0.0);\n"
    "  }\n"
    "\n"
    "  if (d.x + d.y > 2.0) { // d.x + d.y + 1 > 3\n"
    "    // Fetch the crossing edges:\n"
    "    vec4 coords = mad(vec4(-d.x, -d.x, d.y, d.y), vec4(uTexelSize, uTexelSize), texcoord.xyxy);\n"
    "    vec4 c;\n"
    "    c.x  = SMAASampleLevelZeroOffset(edgesTex, coords.xy, vec2(-1,  0)).g;\n"
    "    c.y  = SMAASampleLevelZeroOffset(edgesTex, coords.xy, vec2( 0, -1)).r;\n"
    "    c.zw = SMAASampleLevelZeroOffset(edgesTex, coords.zw, vec2( 1,  0)).gr;\n"
    "    vec2 cc = mad(vec2(2.0, 2.0), c.xz, c.yw);\n"
    "\n"
    "    // Remove the crossing edge if we didn't found the end of the line:\n"
    "    SMAAMovc(bvec2(step(0.9, d.zw)), cc, vec2(0.0, 0.0));\n"
    "\n"
    "    // Fetch the areas for this line:\n"
    "    weights += SMAAAreaDiag(areaTex, d.xy, cc, subsampleIndices.w).gr;\n"
    "  }\n"
    "\n"
    "  return weights;\n"
    "}\n"
    "\n"
    "/**\n"
    " * This allows to determine how much length should we add in the last step\n"
    " * of the searches. It takes the bilinearly interpolated edge (see\n"
    " * @PSEUDO_GATHER4), and adds 0, 1 or 2, depending on which edges and\n"
    " * crossing edges are active.\n"
    " */\n"
    "float SMAASearchLength(sampler2D searchTex, vec2 e, float offset) {\n"
    "  // The texture is flipped vertically, with left and right cases taking half\n"
    "  // of the space horizontally:\n"
    "  vec2 scale = SMAA_SEARCHTEX_SIZE * vec2(0.5, -1.0);\n"
    "  vec2 bias = SMAA_SEARCHTEX_SIZE * vec2(offset, 1.0);\n"
    "\n"
    "  // Scale and bias to access texel centers:\n"
    "  scale += vec2(-1.0,  1.0);\n"
    "  bias  += vec2( 0.5, -0.5);\n"
    "\n"
    "  // Convert from pixel coordinates to texcoords:\n"
    "  // (We use SMAA_SEARCHTEX_PACKED_SIZE because the texture is cropped)\n"
    "  scale *= 1.0 / SMAA_SEARCHTEX_PACKED_SIZE;\n"
    "  bias *= 1.0 / SMAA_SEARCHTEX_PACKED_SIZE;\n"
    "\n"
    "  // Lookup the search texture:\n"
    "  return SMAA_SEARCHTEX_SELECT(texture(searchTex, mad(scale, e, bias))); // LinearSampler\n"
    "}\n"
    "\n"
    "/**\n"
    " * Horizontal/vertical search functions for the 2nd pass.\n"
    " */\n"
    "float SMAASearchXLeft(sampler2D edgesTex, sampler2D searchTex, vec2 texcoord, float end) {\n"
    "  /**\n"
    "    * @PSEUDO_GATHER4\n"
    "    * This texcoord has been offset by (-0.25, -0.125) in the vertex shader to\n"
    "    * sample between edge, thus fetching four edges in a row.\n"
    "    * Sampling with different offsets in each direction allows to disambiguate\n"
    "    * which edges are active from the four fetched ones.\n"
    "    */\n"
    "  vec2 e = vec2(0.0, 1.0);\n"
    "  for (int i = 0; i < SMAA_MAX_SEARCH_STEPS; i++) {\n"
    "    if (!(texcoord.x > end && e.g > 0.8281 && e.r == 0.0)) break;\n"
    "    e = texture(edgesTex, texcoord).rg; // LinearSampler\n"
    "    texcoord = mad(-vec2(2.0, 0.0), uTexelSize, texcoord);\n"
    "  }\n"
    "\n"
    "  float offset = mad(-(255.0 / 127.0), SMAASearchLength(searchTex, e, 0.0), 3.25);\n"
    "  return mad(uTexelSize.x, offset, texcoord.x);\n"
    "\n"
    "  // Non-optimized version:\n"
    "  // We correct the previous (-0.25, -0.125) offset we applied:\n"
    "  // texcoord.x += 0.25 * uTexelSize.x;\n"
    "\n"
    "  // The searches are bias by 1, so adjust the coords accordingly:\n"
    "  // texcoord.x += uTexelSize.x;\n"
    "\n"
    "  // Disambiguate the length added by the last step:\n"
    "  // texcoord.x += 2.0 * uTexelSize.x; // Undo last step\n"
    "  // texcoord.x -= uTexelSize.x * (255.0 / 127.0) * SMAASearchLength(searchTex, e, 0.0);\n"
    "  // return mad(uTexelSize.x, offset, texcoord.x);\n"
    "}\n"
    "\n"
    "float SMAASearchXRight(sampler2D edgesTex, sampler2D searchTex, vec2 texcoord, float end) {\n"
    "  vec2 e = vec2(0.0, 1.0);\n"
    "  for (int i = 0; i < SMAA_MAX_SEARCH_STEPS; i++) { if (!(texcoord.x < end && e.g > 0.8281 && e.r == 0.0)) break;\n"
    "    e = texture(edgesTex, texcoord).rg; // LinearSampler\n"
    "    texcoord = mad(vec2(2.0, 0.0), uTexelSize, texcoord);\n"
    "  }\n"
    "  float offset = mad(-(255.0 / 127.0), SMAASearchLength(searchTex, e, 0.5), 3.25);\n"
    "  return mad(-uTexelSize.x, offset, texcoord.x);\n"
    "}\n"
    "\n"
    "float SMAASearchYUp(sampler2D edgesTex, sampler2D searchTex, vec2 texcoord, float end) {\n"
    "  vec2 e = vec2(1.0, 0.0);\n"
    "  for (int i = 0; i < SMAA_MAX_SEARCH_STEPS; i++) { if (!(texcoord.y > end && e.r > 0.8281 && e.g == 0.0)) break;\n"
    "    e = texture(edgesTex, texcoord).rg; // LinearSampler\n"
    "    texcoord = mad(-vec2(0.0, 2.0), uTexelSize, texcoord);\n"
    "  }\n"
    "  float offset = mad(-(255.0 / 127.0), SMAASearchLength(searchTex, e.gr, 0.0), 3.25);\n"
    "  return mad(uTexelSize.y, offset, texcoord.y);\n"
    "}\n"
    "\n"
    "float SMAASearchYDown(sampler2D edgesTex, sampler2D searchTex, vec2 texcoord, float end) {\n"
    "  vec2 e = vec2(1.0, 0.0);\n"
    "  for (int i = 0; i < SMAA_MAX_SEARCH_STEPS; i++) { if (!(texcoord.y < end && e.r > 0.8281 && e.g == 0.0)) break;\n"
    "    e = texture(edgesTex, texcoord).rg; // LinearSampler\n"
    "    texcoord = mad(vec2(0.0, 2.0), uTexelSize, texcoord);\n"
    "  }\n"
    "  float offset = mad(-(255.0 / 127.0), SMAASearchLength(searchTex, e.gr, 0.5), 3.25);\n"
    "  return mad(-uTexelSize.y, offset, texcoord.y);\n"
    "}\n"
    "\n"
    "/**\n"
    " * Ok, we have the distance and both crossing edges. So, what are the areas\n"
    " * at each side of current edge?\n"
    " */\n"
    "vec2 SMAAArea(sampler2D areaTex, vec2 dist, float e1, float e2, float offset) {\n"
    "  // Rounding prevents precision errors of bilinear filtering:\n"
    "  vec2 texcoord = mad(vec2(SMAA_AREATEX_MAX_DISTANCE, SMAA_AREATEX_MAX_DISTANCE), round(4.0 * vec2(e1, e2)), dist);\n"
    "\n"
    "  // We do a scale and bias for mapping to texel space:\n"
    "  texcoord = mad(SMAA_AREATEX_PIXEL_SIZE, texcoord, 0.5 * SMAA_AREATEX_PIXEL_SIZE);\n"
    "\n"
    "  // Move to proper place, according to the subpixel offset:\n"
    "  texcoord.y = mad(SMAA_AREATEX_SUBTEX_SIZE, offset, texcoord.y);\n"
    "\n"
    "  // Do it!\n"
    "  return SMAA_AREATEX_SELECT(texture(areaTex, texcoord)); // LinearSampler\n"
    "}\n"
    "\n"
    "// Corner Detection Functions\n"
    "void SMAADetectHorizontalCornerPattern(sampler2D edgesTex, inout vec2 weights, vec4 texcoord, vec2 d) {\n"
    "  #if !defined(SMAA_DISABLE_CORNER_DETECTION)\n"
    "  vec2 leftRight = step(d.xy, d.yx);\n"
    "  vec2 rounding = (1.0 - SMAA_CORNER_ROUNDING_NORM) * leftRight;\n"
    "\n"
    "  rounding /= leftRight.x + leftRight.y; // Reduce blending for pixels in the center of a line.\n"
    "\n"
    "  vec2 factor = vec2(1.0, 1.0);\n"
    "  factor.x -= rounding.x * SMAASampleLevelZeroOffset(edgesTex, texcoord.xy, vec2(0,  1)).r;\n"
    "  factor.x -= rounding.y * SMAASampleLevelZeroOffset(edgesTex, texcoord.zw, vec2(1,  1)).r;\n"
    "  factor.y -= rounding.x * SMAASampleLevelZeroOffset(edgesTex, texcoord.xy, vec2(0, -2)).r;\n"
    "  factor.y -= rounding.y * SMAASampleLevelZeroOffset(edgesTex, texcoord.zw, vec2(1, -2)).r;\n"
    "\n"
    "  weights *= saturate(factor);\n"
    "  #endif\n"
    "}\n"
    "\n"
    "void SMAADetectVerticalCornerPattern(sampler2D edgesTex, inout vec2 weights, vec4 texcoord, vec2 d) {\n"
    "  #if !defined(SMAA_DISABLE_CORNER_DETECTION)\n"
    "  vec2 leftRight = step(d.xy, d.yx);\n"
    "  vec2 rounding = (1.0 - SMAA_CORNER_ROUNDING_NORM) * leftRight;\n"
    "\n"
    "  rounding /= leftRight.x + leftRight.y;\n"
    "\n"
    "  vec2 factor = vec2(1.0, 1.0);\n"
    "  factor.x -= rounding.x * SMAASampleLevelZeroOffset(edgesTex, texcoord.xy, vec2( 1, 0)).g;\n"
    "  factor.x -= rounding.y * SMAASampleLevelZeroOffset(edgesTex, texcoord.zw, vec2( 1, 1)).g;\n"
    "  factor.y -= rounding.x * SMAASampleLevelZeroOffset(edgesTex, texcoord.xy, vec2(-2, 0)).g;\n"
    "  factor.y -= rounding.y * SMAASampleLevelZeroOffset(edgesTex, texcoord.zw, vec2(-2, 1)).g;\n"
    "\n"
    "  weights *= saturate(factor);\n"
    "  #endif\n"
    "}\n"
    "\n"
    "void main() {\n"
    "  vec4 weights = vec4(0.0, 0.0, 0.0, 0.0);\n"
    "  vec2 e = texture(uEdgesTexture, vTexCoord0).rg;\n"
    "\n"
    "  if (e.g > 0.0) { // Edge at north\n"
    "\n"
    "    #if !defined(SMAA_DISABLE_DIAG_DETECTION)\n"
    "    // Diagonals have both north and west edges, so searching for them in\n"
    "    // one of the boundaries is enough.\n"
    "    weights.rg = SMAACalculateDiagWeights(uEdgesTexture, uAreaTexture, vTexCoord0, e, uSubsampleIndices);\n"
    "\n"
    "    // We give priority to diagonals, so if we find a diagonal we skip\n"
    "    // horizontal/vertical processing.\n"
    "    if (weights.r == -weights.g) { // weights.r + weights.g == 0.0\n"
    "    #endif\n"
    "\n"
    "    vec2 d;\n"
    "\n"
    "    // Find the distance to the left:\n"
    "    vec3 coords;\n"
    "    coords.x = SMAASearchXLeft(uEdgesTexture, uSearchTexture, vOffset[0].xy, vOffset[2].x);\n"
    "    coords.y = vOffset[1].y; // vOffset[1].y = vTexCoord0.y - 0.25 * uTexelSize.y (@CROSSING_OFFSET)\n"
    "    d.x = coords.x;\n"
    "\n"
    "    // Now fetch the left crossing edges, two at a time using bilinear\n"
    "    // filtering. Sampling at -0.25 (see @CROSSING_OFFSET) enables to\n"
    "    // discern what value each edge has:\n"
    "    float e1 = texture(uEdgesTexture, coords.xy).r; // LinearSampler\n"
    "\n"
    "    // Find the distance to the right:\n"
    "    coords.z = SMAASearchXRight(uEdgesTexture, uSearchTexture, vOffset[0].zw, vOffset[2].y);\n"
    "    d.y = coords.z;\n"
    "\n"
    "    // We want the distances to be in pixel units (doing this here allow to\n"
    "    // better interleave arithmetic and memory accesses):\n"
    "    d = abs(round(mad(uViewportSize.xx, d, -vPixCoord.xx)));\n"
    "\n"
    "    // SMAAArea below needs a sqrt, as the areas texture is compressed\n"
    "    // quadratically:\n"
    "    vec2 sqrt_d = sqrt(d);\n"
    "\n"
    "    // Fetch the right crossing edges:\n"
    "    float e2 = SMAASampleLevelZeroOffset(uEdgesTexture, coords.zy, vec2(1, 0)).r;\n"
    "\n"
    "    // Ok, we know how this pattern looks like, now it is time for getting\n"
    "    // the actual area:\n"
    "    weights.rg = SMAAArea(uAreaTexture, sqrt_d, e1, e2, uSubsampleIndices.y);\n"
    "\n"
    "    // Fix corners:\n"
    "    coords.y = vTexCoord0.y;\n"
    "    SMAADetectHorizontalCornerPattern(uEdgesTexture, weights.rg, coords.xyzy, d);\n"
    "\n"
    "    #if !defined(SMAA_DISABLE_DIAG_DETECTION)\n"
    "    } else\n"
    "    e.r = 0.0; // Skip vertical processing.\n"
    "    #endif\n"
    "  }\n"
    "\n"
    "  if (e.r > 0.0) { // Edge at west\n"
    "    vec2 d;\n"
    "\n"
    "    // Find the distance to the top:\n"
    "    vec3 coords;\n"
    "    coords.y = SMAASearchYUp(uEdgesTexture, uSearchTexture, vOffset[1].xy, vOffset[2].z);\n"
    "    coords.x = vOffset[0].x; // vOffset[1].x = vTexCoord0.x - 0.25 * uTexelSize.x;\n"
    "    d.x = coords.y;\n"
    "\n"
    "    // Fetch the top crossing edges:\n"
    "    float e1 = texture(uEdgesTexture, coords.xy).g; // LinearSampler\n"
    "\n"
    "    // Find the distance to the bottom:\n"
    "    coords.z = SMAASearchYDown(uEdgesTexture, uSearchTexture, vOffset[1].zw, vOffset[2].w);\n"
    "    d.y = coords.z;\n"
    "\n"
    "    // We want the distances to be in pixel units:\n"
    "    d = abs(round(mad(uViewportSize.yy, d, -vPixCoord.yy)));\n"
    "\n"
    "    // SMAAArea below needs a sqrt, as the areas texture is compressed\n"
    "    // quadratically:\n"
    "    vec2 sqrt_d = sqrt(d);\n"
    "\n"
    "    // Fetch the bottom crossing edges:\n"
    "    float e2 = SMAASampleLevelZeroOffset(uEdgesTexture, coords.xz, vec2(0, 1)).g;\n"
    "\n"
    "    // Get the area for this direction:\n"
    "    weights.ba = SMAAArea(uAreaTexture, sqrt_d, e1, e2, uSubsampleIndices.x);\n"
    "\n"
    "    // Fix corners:\n"
    "    coords.x = vTexCoord0.x;\n"
    "    SMAADetectVerticalCornerPattern(uEdgesTexture, weights.ba, coords.xyxz, d);\n"
    "  }\n"
    "\n"
    "  oColor = weights;\n"
    "}\n"
    "\n";

static const char* shader_smaa_blend_frag =
    "#version 330 core\n"
    "out vec4 oColor;\n"
    "\n"
    "#ifndef SMAA_REPROJECTION\n"
    "  #define SMAA_REPROJECTION 0\n"
    "#endif\n"
    "\n"
    "/**\n"
    " * Decodes the velocity buffer: override it when velocity is stored encoded.\n"
    " * Velocity is expected in texture coordinates, pointing from the previous frame\n"
    " * to the current one.\n"
    " */\n"
    "#ifndef SMAA_DECODE_VELOCITY\n"
    "  #define SMAA_DECODE_VELOCITY(sample) sample.rg\n"
    "#endif\n"
    "\n"
    "#define mad(a, b, c) (a * b + c)\n"
    "\n"
    "uniform sampler2D uColorTexture;\n"
    "uniform sampler2D uBlendTexture;\n"
    "\n"
    "#if SMAA_REPROJECTION\n"
    "  uniform sampler2D uVelocityTexture;\n"
    "#endif\n"
    "\n"
    "uniform vec2 uTexelSize;\n"
    "\n"
    "in vec2 vTexCoord0;\n"
    "in vec4 vOffset;\n"
    "\n"
    "/**\n"
    " * Conditional move:\n"
    " */\n"
    "void SMAAMovc(bvec2 cond, inout vec2 variable, vec2 value) {\n"
    "  if (cond.x) variable.x = value.x;\n"
    "  if (cond.y) variable.y = value.y;\n"
    "}\n"
    "\n"
    "void SMAAMovc(bvec4 cond, inout vec4 variable, vec4 value) {\n"
    "  SMAAMovc(cond.xy, variable.xy, value.xy);\n"
    "  SMAAMovc(cond.zw, variable.zw, value.zw);\n"
    "}\n"
    "\n"
    "void main() {\n"
    "  vec4 color = vec4(0.0);\n"
    "\n"
    "  // Fetch the blending weights for current pixel:\n"
    "  vec4 a;\n"
    "  a.x = texture(uBlendTexture, vOffset.xy).a; // Right\n"
    "  a.y = texture(uBlendTexture, vOffset.zw).g; // Top\n"
    "  a.wz = texture(uBlendTexture, vTexCoord0).xz; // Bottom / Left\n"
    "\n"
    "  // Is there any blending weight with a value greater than 0.0?\n"
    "  if (dot(a, vec4(1.0, 1.0, 1.0, 1.0)) < 1e-5) {\n"
    "    color = texture(uColorTexture, vTexCoord0); // LinearSampler\n"
    "\n"
    "    #if SMAA_REPROJECTION\n"
    "      vec2 velocity = SMAA_DECODE_VELOCITY(texture(uVelocityTexture, vTexCoord0));\n"
    "\n"
    "      // Pack velocity into the alpha channel:\n"
    "      color.a = sqrt(5.0 * length(velocity));\n"
    "    #endif\n"
    "  } else {\n"
    "    bool h = max(a.x, a.z) > max(a.y, a.w); // max(horizontal) > max(vertical)\n"
    "\n"
    "    // Calculate the blending offsets:\n"
    "    vec4 blendingOffset = vec4(0.0, a.y, 0.0, a.w);\n"
    "    vec2 blendingWeight = a.yw;\n"
    "    SMAAMovc(bvec4(h, h, h, h), blendingOffset, vec4(a.x, 0.0, a.z, 0.0));\n"
    "    SMAAMovc(bvec2(h, h), blendingWeight, a.xz);\n"
    "    blendingWeight /= dot(blendingWeight, vec2(1.0, 1.0));\n"
    "\n"
    "    // Calculate the texture coordinates:\n"
    "    vec4 blendingCoord = mad(blendingOffset, vec4(uTexelSize, -uTexelSize), vTexCoord0.xyxy);\n"
    "\n"
    "    // We exploit bilinear filtering to mix current pixel with the chosen\n"
    "    // neighbor:\n"
    "    color = blendingWeight.x * texture(uColorTexture, blendingCoord.xy); // LinearSampler\n"
    "    color += blendingWeight.y * texture(uColorTexture, blendingCoord.zw); // LinearSampler\n"
    "\n"
    "    #if SMAA_REPROJECTION\n"
    "      // Antialias velocity for proper reprojection in a later stage:\n"
    "      vec2 velocity = blendingWeight.x * SMAA_DECODE_VELOCITY(texture(uVelocityTexture, blendingCoord.xy));\n"
    "      velocity += blendingWeight.y * SMAA_DECODE_VELOCITY(texture(uVelocityTexture, blendingCoord.zw));\n"
    "\n"
    "      // Pack velocity into the alpha channel:\n"
    "      color.a = sqrt(5.0 * length(velocity));\n"
    "    #endif\n"
    "  }\n"
    "\n"
    "  oColor = color;\n"
    "}\n"
    "\n";


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
        pcLog("antialias: shader compile failed: %.*s", (int)length, log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint create_program(const char* vs_source, const char* fs_source, const char* name)
{
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_source);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_source);
    GLuint program;
    GLint ok = 0;
    GLchar log[2048];
    GLsizei length = 0;
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return 0;
    }
    program = glCreateProgram();
    if (!program) {
        glDeleteShader(vs);
        glDeleteShader(fs);
        return 0;
    }
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!ok) {
        glGetProgramInfoLog(program, (GLsizei)sizeof(log), &length, log);
        pcLog("antialias: %s program link failed: %.*s", name, (int)length, log);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

static void destroy_target(GLuint* texture, GLuint* fbo)
{
    if (*fbo) glDeleteFramebuffers(1, fbo);
    if (*texture) glDeleteTextures(1, texture);
    *fbo = 0;
    *texture = 0;
}

static int create_target(GLuint* texture, GLuint* fbo, int width, int height, GLint filter)
{
    GLenum status;
    glGenTextures(1, texture);
    glBindTexture(GL_TEXTURE_2D, *texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, *fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *texture, 0);
    status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        pcLog("antialias: incomplete framebuffer (0x%04x)", (unsigned int)status);
        destroy_target(texture, fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return 0;
    }
    return 1;
}

static void destroy_targets(void)
{
    int i;
    for (i = 0; i < 2; i++) destroy_target(&aa.color_texture[i], &aa.color_fbo[i]);
    destroy_target(&aa.edges_texture, &aa.edges_fbo);
    destroy_target(&aa.weights_texture, &aa.weights_fbo);
    aa.width = aa.height = 0;
}

int pcAaResize(int width, int height)
{
    int i;
    if (!aa.flags || width <= 0 || height <= 0) return 1;
    if (aa.width == width && aa.height == height && aa.color_fbo[0] && aa.color_fbo[1]) return 1;
    destroy_targets();
    for (i = 0; i < 2; i++) {
        if (!create_target(&aa.color_texture[i], &aa.color_fbo[i], width, height, GL_LINEAR)) {
            destroy_targets();
            return 0;
        }
    }
    if (aa.flags & PC_AA_SMAA) {
        if (!create_target(&aa.edges_texture, &aa.edges_fbo, width, height, GL_LINEAR) ||
            !create_target(&aa.weights_texture, &aa.weights_fbo, width, height, GL_LINEAR)) {
            destroy_targets();
            return 0;
        }
    }
    aa.width = width;
    aa.height = height;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return 1;
}

static int load_lookup_file(const char* name, unsigned char* bytes, size_t expected)
{
    char dir[1024], path[1200];
    FILE* file;
    size_t got;
    pcPlatformExeDir(dir, (int)sizeof(dir));
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    file = fopen(path, "rb");
    if (!file) {
        pcLog("antialias: cannot open SMAA lookup texture %s", path);
        return 0;
    }
    got = fread(bytes, 1, expected, file);
    if (got != expected || fgetc(file) != EOF) {
        pcLog("antialias: invalid SMAA lookup texture %s (expected %u bytes)", path, (unsigned int)expected);
        fclose(file);
        return 0;
    }
    fclose(file);
    return 1;
}

static void destroy_smaa(void)
{
    if (aa.edges_program) glDeleteProgram(aa.edges_program);
    if (aa.weights_program) glDeleteProgram(aa.weights_program);
    if (aa.blend_program) glDeleteProgram(aa.blend_program);
    if (aa.area_texture) glDeleteTextures(1, &aa.area_texture);
    if (aa.search_texture) glDeleteTextures(1, &aa.search_texture);
    aa.edges_program = aa.weights_program = aa.blend_program = 0;
    aa.area_texture = aa.search_texture = 0;
    aa.flags &= ~PC_AA_SMAA;
}

static int init_smaa(void)
{
    unsigned char* area = NULL;
    unsigned char search[64 * 16];
    aa.edges_program = create_program(shader_smaa_edges_vert, shader_smaa_edges_frag, "SMAA edges");
    aa.weights_program = create_program(shader_smaa_weights_vert, shader_smaa_weights_frag, "SMAA weights");
    aa.blend_program = create_program(shader_smaa_blend_vert, shader_smaa_blend_frag, "SMAA blend");
    if (!aa.edges_program || !aa.weights_program || !aa.blend_program) {
        destroy_smaa();
        return 0;
    }

    area = (unsigned char*)malloc(160u * 560u * 2u);
    if (!area || !load_lookup_file("smaa_area.rg8", area, 160u * 560u * 2u) ||
        !load_lookup_file("smaa_search.r8", search, sizeof(search))) {
        free(area);
        destroy_smaa();
        return 0;
    }

    glGenTextures(1, &aa.area_texture);
    glBindTexture(GL_TEXTURE_2D, aa.area_texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, 160, 560, 0, GL_RG, GL_UNSIGNED_BYTE, area);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenTextures(1, &aa.search_texture);
    glBindTexture(GL_TEXTURE_2D, aa.search_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 64, 16, 0, GL_RED, GL_UNSIGNED_BYTE, search);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    free(area);

    aa.edges_color = glGetUniformLocation(aa.edges_program, "uColorTexture");
    aa.edges_texel = glGetUniformLocation(aa.edges_program, "uTexelSize");
    aa.weights_edges = glGetUniformLocation(aa.weights_program, "uEdgesTexture");
    aa.weights_area = glGetUniformLocation(aa.weights_program, "uAreaTexture");
    aa.weights_search = glGetUniformLocation(aa.weights_program, "uSearchTexture");
    aa.weights_viewport = glGetUniformLocation(aa.weights_program, "uViewportSize");
    aa.weights_texel = glGetUniformLocation(aa.weights_program, "uTexelSize");
    aa.weights_subsample = glGetUniformLocation(aa.weights_program, "uSubsampleIndices");
    aa.blend_color = glGetUniformLocation(aa.blend_program, "uColorTexture");
    aa.blend_weights = glGetUniformLocation(aa.blend_program, "uBlendTexture");
    aa.blend_texel = glGetUniformLocation(aa.blend_program, "uTexelSize");

    if (aa.edges_color < 0 || aa.edges_texel < 0 || aa.weights_edges < 0 || aa.weights_area < 0 ||
        aa.weights_search < 0 || aa.weights_viewport < 0 || aa.weights_texel < 0 || aa.weights_subsample < 0 ||
        aa.blend_color < 0 || aa.blend_weights < 0 || aa.blend_texel < 0) {
        pcLog("antialias: SMAA shader is missing a required uniform");
        destroy_smaa();
        return 0;
    }
    return 1;
}

static int init_fxaa(void)
{
    aa.fxaa_program = create_program(shader_fxaa_vert, shader_fxaa_frag, "FXAA");
    if (!aa.fxaa_program) return 0;
    aa.fxaa_color = glGetUniformLocation(aa.fxaa_program, "uColorTexture");
    aa.fxaa_texel = glGetUniformLocation(aa.fxaa_program, "uTexelSize");
    if (aa.fxaa_color < 0 || aa.fxaa_texel < 0) {
        pcLog("antialias: FXAA shader is missing a required uniform");
        glDeleteProgram(aa.fxaa_program);
        aa.fxaa_program = 0;
        return 0;
    }
    return 1;
}

int pcAaInit(int width, int height, int fxaa, int smaa)
{
    int active = 0;
    memset(&aa, 0, sizeof(aa));
    if (fxaa && init_fxaa()) active |= PC_AA_FXAA;
    if (smaa && init_smaa()) active |= PC_AA_SMAA;
    aa.flags = active;
    if (!active) return 0;

    glGenVertexArrays(1, &aa.vao);
    if (!aa.vao || !pcAaResize(width, height)) {
        pcAaShutdown();
        pcLog("antialias: could not allocate post-process targets; AA disabled");
        return 0;
    }
    pcLog("antialias: active%s%s (%dx%d); both enabled means SMAA then FXAA",
          (active & PC_AA_FXAA) ? " FXAA" : "",
          (active & PC_AA_SMAA) ? " SMAA 1x" : "", width, height);
    return active;
}

void pcAaShutdown(void)
{
    destroy_targets();
    if (aa.fxaa_program) glDeleteProgram(aa.fxaa_program);
    if (aa.edges_program) glDeleteProgram(aa.edges_program);
    if (aa.weights_program) glDeleteProgram(aa.weights_program);
    if (aa.blend_program) glDeleteProgram(aa.blend_program);
    if (aa.area_texture) glDeleteTextures(1, &aa.area_texture);
    if (aa.search_texture) glDeleteTextures(1, &aa.search_texture);
    if (aa.vao) glDeleteVertexArrays(1, &aa.vao);
    memset(&aa, 0, sizeof(aa));
}

static void bind_texture(GLuint texture, int unit, GLint uniform)
{
    glActiveTexture((GLenum)(GL_TEXTURE0 + unit));
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(uniform, unit);
}

static void draw_fullscreen(void)
{
    glBindVertexArray(aa.vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

static void begin_pass(GLuint fbo)
{
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, aa.width, aa.height);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}

static void render_fxaa(GLuint input_texture, int output_index)
{
    begin_pass(aa.color_fbo[output_index]);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(aa.fxaa_program);
    bind_texture(input_texture, 0, aa.fxaa_color);
    glUniform2f(aa.fxaa_texel, 1.0f / (float)aa.width, 1.0f / (float)aa.height);
    draw_fullscreen();
}

static void render_smaa(GLuint input_texture, int output_index)
{
    begin_pass(aa.edges_fbo);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(aa.edges_program);
    bind_texture(input_texture, 0, aa.edges_color);
    glUniform2f(aa.edges_texel, 1.0f / (float)aa.width, 1.0f / (float)aa.height);
    draw_fullscreen();

    begin_pass(aa.weights_fbo);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(aa.weights_program);
    bind_texture(aa.edges_texture, 0, aa.weights_edges);
    bind_texture(aa.area_texture, 1, aa.weights_area);
    bind_texture(aa.search_texture, 2, aa.weights_search);
    glUniform2f(aa.weights_viewport, (float)aa.width, (float)aa.height);
    glUniform2f(aa.weights_texel, 1.0f / (float)aa.width, 1.0f / (float)aa.height);
    glUniform4f(aa.weights_subsample, 0.0f, 0.0f, 0.0f, 0.0f);
    draw_fullscreen();

    begin_pass(aa.color_fbo[output_index]);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(aa.blend_program);
    bind_texture(input_texture, 0, aa.blend_color);
    bind_texture(aa.weights_texture, 1, aa.blend_weights);
    glUniform2f(aa.blend_texel, 1.0f / (float)aa.width, 1.0f / (float)aa.height);
    draw_fullscreen();
}

int pcAaProcess(GLuint input_texture, int flags, GLuint* output_framebuffer)
{
    GLuint current_texture = input_texture;
    int next_output = 0;
    int current_output = -1;
    if (output_framebuffer) *output_framebuffer = 0;
    if (!aa.flags || !input_texture || !output_framebuffer) return 0;
    flags &= aa.flags;
    if (!flags) return 0;

    if (flags & PC_AA_SMAA) {
        render_smaa(current_texture, next_output);
        current_texture = aa.color_texture[next_output];
        current_output = next_output;
        next_output = 1 - next_output;
    }
    if (flags & PC_AA_FXAA) {
        render_fxaa(current_texture, next_output);
        current_texture = aa.color_texture[next_output];
        current_output = next_output;
    }
    (void)current_texture;
    if (current_output < 0) return 0;
    *output_framebuffer = aa.color_fbo[current_output];
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDepthMask(GL_TRUE);
    return 1;
}
