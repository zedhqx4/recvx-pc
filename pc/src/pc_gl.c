/* Platform OpenGL entry-point loader, shared by the native renderer. */
#include <stdio.h>
#include "pc_gl.h"
#include "pc_platform.h"

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
