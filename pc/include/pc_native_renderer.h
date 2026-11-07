#pragma once

/* Parallel OpenGL GPU geometry probe. No GS registers, GIF packets or GS VRAM are consumed. */
void pcNativeRendererInit(void);
void pcNativeRendererBeginFrame(void);
void pcNativeRendererCapture(unsigned int ot_index, const void* vu1_prim_vertices,
                             unsigned int vertex_count, unsigned int primitive_type);
void pcNativeRendererAccumulate(unsigned int ot_index);
void pcNativeRendererEndFrame(void);
void pcNativeRendererPresent(unsigned int display_fbp);
