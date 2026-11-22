/* PC compatibility shims for the original PS2 libgraph/libdma APIs.
 *
 * The native PC renderer does not interpret GIF packets, emulate GS registers, maintain
 * GS VRAM, or translate DMA chains. Graphics entry points have been moved to pc_renderer.
 * These no-op shims exist only for PS2 hardware setup paths which remain compiled for the
 * shared game sources but are bypassed by the native PC renderer.
 */
#include <string.h>

#include "../../include/ps2/veronica/prog/ps2_loadtim2.h"
#include "../../include/ps2/veronica/prog/main.h"
#include "../include/pc_renderer.h"
#include "../include/pc_platform.h"

/* The GS PATH3 entry point is intentionally inert on PC. No GIF commands are consumed. */
void loadImage(void* tags) { (void)tags; }
void SyncPath(void) {}
void D2_SyncTag(void) {}

int sceGsSyncPath(int mode, unsigned short timeout)
{
    (void)mode; (void)timeout;
    return 0;
}
