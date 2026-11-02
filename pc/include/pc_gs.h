/* PC port: GS emulator interface for the platform layer (pc/src/gs). */
#pragma once

void pcGsInit(unsigned char* scratchpad);      /* needs a current OpenGL 3.3 context */
void pcGsSetDisplay(unsigned int fbp, unsigned int fbw);
void pcGsPresent(void);                         /* draws the display buffer to the window */

/* Experimental material-state inspector (decals-wip branch only). */
void pcGsMaterialDebugToggle(void);
void pcGsMaterialDebugCycle(int direction); /* +1/-1 selects the next/previous in-frustum state */
int pcGsMaterialDebugMark(void);             /* records the selected GS material state in recvx.log */
void pcGsMaterialDebugStatus(int* enabled, int* selected, int* count, int* marked);

extern int pcGsScale;                           /* internal resolution multiplier */
extern float pcGsScaleX;                        /* horizontal backing-buffer scale per logical pixel */
extern float pcGsScaleY;                        /* vertical backing-buffer scale per logical pixel */
