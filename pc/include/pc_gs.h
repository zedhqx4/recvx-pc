/* PC port: GS emulator interface for the platform layer (pc/src/gs). */
#pragma once

void pcGsInit(unsigned char* scratchpad);      /* needs a current OpenGL 3.3 context */
void pcGsSetDisplay(unsigned int fbp, unsigned int fbw);
void pcGsPresent(void);                         /* draws the display buffer to the window */
int pcGsToggleUiWireframe(void);                 /* toggles the 2D textured wireframe diagnostic overlay */

extern int pcGsScale;                           /* internal resolution multiplier */
extern float pcGsScaleX;                        /* effective horizontal backing-buffer scale */
extern float pcGsScaleY;                        /* effective vertical backing-buffer scale */
