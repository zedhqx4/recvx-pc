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

void sceDmaReset(int mode) { (void)mode; }
void sceVpu0Reset(void) {}

int sceGsResetGraph(short mode, short inter, short omode, short ffmode)
{
    (void)mode; (void)inter; (void)omode; (void)ffmode;
    return 0;
}

void sceGsResetPath(void) {}

int sceGsSyncPath(int mode, unsigned short timeout)
{
    (void)mode; (void)timeout;
    return 0;
}

int sceGsSetDefDBuffDc(sceGsDBuffDc* db, short psm, short w, short h,
                       short ztest, short zpsm, short clear)
{
    (void)psm; (void)w; (void)h; (void)ztest; (void)zpsm; (void)clear;
    if (db != NULL) memset(db, 0, sizeof(*db));
    return 0;
}

int sceGsSwapDBuffDc(sceGsDBuffDc* db, int id)
{
    (void)db; (void)id;
    pcRendererFlushTransparent();
    pcVideoPresent();
    return id;
}

void sceGsSetDefLoadImage(sceGsLoadImage* lp, short dbp, short dbw, short dpsm,
                          short x, short y, short w, short h)
{
    (void)dbp; (void)dbw; (void)dpsm; (void)x; (void)y; (void)w; (void)h;
    if (lp != NULL) memset(lp, 0, sizeof(*lp));
}

int sceGsExecLoadImage(sceGsLoadImage* lp, u_long128* srcaddr)
{
    (void)lp; (void)srcaddr;
    return 0;
}

void sceGsSetDefStoreImage(sceGsStoreImage* sp, short sbp, short sbw, short spsm,
                           short x, short y, short w, short h)
{
    (void)sbp; (void)sbw; (void)spsm; (void)x; (void)y; (void)w; (void)h;
    if (sp != NULL) memset(sp, 0, sizeof(*sp));
}

/* CLAUDE-NOTE[N7]: no-op. Es el camino por el que el juego leia el framebuffer a una textura (render texture, espejos,
 * efectos de agua/gas). Sin implementarlo, esas escenas salen vacias. Ver StoreRenderTex() en ps2_loadtim2.c. */
int sceGsExecStoreImage(sceGsStoreImage* sp, u_long128* dstaddr)
{
    (void)sp; (void)dstaddr;
    return 0;
}

/* DMA to VIF/VU/GIF is not present in the native PC rendering pipeline. */
int sceDmaSend(sceDmaChan* d, void* tag)
{
    (void)d; (void)tag;
    return 0;
}

int sceDmaSync(sceDmaChan* d, int mode, int timeout)
{
    (void)d; (void)mode; (void)timeout;
    return 0;
}

/* The PC build never uploads the PS2 VU microprograms. */
int ps2_vu0sub0;
int ps2_vu1sub0;
int ps2_vu1sub1;
