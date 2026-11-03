/* PC port: GS emulator interface for the platform layer (pc/src/gs). */
#pragma once

#include <stdint.h>

void pcGsInit(unsigned char* scratchpad);      /* needs a current OpenGL 3.3 context */
void pcGsSetDisplay(unsigned int fbp, unsigned int fbw);
void pcGsPresent(void);                         /* draws the display buffer to the window */
uint32_t pcGsFrameSerial(void);
void pcGsSetTim2Identity(uint64_t key0, uint64_t key1, int cached_image);
int pcGsHasTim2Texture(uint64_t key0, uint64_t key1);
void pcGsCaptureTim2Texture(uint64_t key0, uint64_t key1, uint64_t tex0a, uint64_t tex0b, int w, int h);

extern int pcGsScale;                           /* internal resolution multiplier */
extern float pcGsScaleX;                        /* horizontal backing-buffer scale per logical pixel */
extern float pcGsScaleY;                        /* vertical backing-buffer scale per logical pixel */
