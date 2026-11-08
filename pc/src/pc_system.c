/* PC port: startup code of ps2_dummy.c (Ps2Init and friends). Everything that is not about
 * PS2 hardware/IOP setup is kept as in the original. */
#include "../../include/ps2/veronica/prog/ps2_NaSystem.h"
#include "../../include/ps2/veronica/prog/ps2_dummy.h"
#include "../../include/ps2/veronica/prog/ps2_loadtim2.h"
#include "../../include/ps2/veronica/prog/ps2_NaTextureFunction.h"
#include "../../include/ps2/veronica/prog/main.h"

#include "pc_platform.h"
#include "pc_os.h"
#include "pc_renderer.h"

extern unsigned char Ps2_tex_mem[10485760];

void _builtin_set_imask(int mask)
{
}

void Ps2Init()
{
    Ps2_tex_buff = &Ps2_tex_mem;
    pcOsStartupLog("Ps2Init: before pcVideoInit");
    pcVideoInit();
    pcOsStartupLog("Ps2Init: native renderer ready");

    pcOsStartupLog("Ps2Init: before pcCdOpen");
    pcCdOpen();
    pcOsStartupLog("Ps2Init: after pcCdOpen");

    Ps2SetVSyncCounter();
    Ps2InitFunc();
    Ps2ClearOT();
    pcRendererSetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    pcRendererSetScissor(0, 0, 0, 0, 0);
    _Make_SinTable();
}

void Ps2LoadModule(char* p)
{
}

void Snd_init()
{
}

void Cd_init()
{
}

void Card_init()
{
}

void PS2_jikken()
{
    pdGetPeripheral(PDD_PORT_A0);

    Ps2SwapDBuff();
}

/* debug: one line of game state for the periodic log */
void pcGameStateLog(void)
{
    extern SYS_WORK* sys;

    pcLog("state tk %06x ts %06x gm %08x st %08x stg %d rom %d gm_mode %d", sys->tk_flg, sys->ts_flg, sys->gm_flg,
          sys->st_flg, sys->stg_no, sys->rom_no, sys->gm_mode);
}

/* debug: nonzero while the title screen task is running */
int pcGameInTitle(void)
{
    extern SYS_WORK* sys;

    return ((sys->tk_flg & ~sys->ts_flg) & (1 << 3)) != 0;
}

/* debug: nonzero while the status screen (item select task) is running */
int pcGameInStatusScreen(void)
{
    extern SYS_WORK* sys;

    return ((sys->tk_flg & ~sys->ts_flg) & (1 << 9)) != 0;
}
