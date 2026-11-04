/* PC port: GS emulator interface for the platform layer (pc/src/gs). */
#pragma once

void pcGsInit(unsigned char* scratchpad);      /* needs a current OpenGL 3.3 context */
void pcGsSetDisplay(unsigned int fbp, unsigned int fbw);
void pcGsPresent(void);                         /* draws the display buffer to the window */

extern int pcGsScale;                           /* internal resolution multiplier */
extern float pcGsScaleX;                        /* horizontal backing-buffer scale per logical pixel */
extern float pcGsScaleY;                        /* vertical backing-buffer scale per logical pixel */

/* Low-overhead per-room texture-call counters for periodic profiling. */
void pcGsProfileTexLoad(int room);
void pcGsProfileCnkTid(int room);
