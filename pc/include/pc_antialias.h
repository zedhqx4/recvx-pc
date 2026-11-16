/* Optional native-renderer post-process anti-aliasing. */
#pragma once
#include "pc_gl.h"

int pcAaInit(int width, int height, int fxaa, int smaa);
void pcAaShutdown(void);
int pcAaResize(int width, int height);
int pcAaProcess(GLuint input_texture, int flags, GLuint* output_framebuffer);
