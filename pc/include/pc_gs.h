/* PC port: GS emulator interface for the platform layer (pc/src/gs). */
#pragma once

void pcGsInit(unsigned char* scratchpad);      /* needs a current OpenGL 3.3 context */
void pcGsSetDisplay(unsigned int fbp, unsigned int fbw);
void pcGsPresent(void);                         /* draws the display buffer to the window */

/* Experimental decal-priority controls (decals-wip branch only). */
void pcGsGetDecalSettings(int* enabled, float* bias);
void pcGsDecalPriorityToggle(void);
void pcGsDecalBiasAdjust(int direction);        /* +1/-1 changes bias by 0.25 */
int pcGsSaveDecalSettings(void);

extern int pcGsScale;                           /* internal resolution multiplier */
extern float pcGsScaleX;                        /* horizontal backing-buffer scale per logical pixel */
extern float pcGsScaleY;                        /* vertical backing-buffer scale per logical pixel */
