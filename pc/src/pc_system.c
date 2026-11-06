/* PC port: startup code of ps2_dummy.c (Ps2Init and friends). Everything that is not about
 * PS2 hardware/IOP setup is kept as in the original. */
#include "../../include/ps2/veronica/prog/ps2_NaSystem.h"
#include "../../include/ps2/veronica/prog/ps2_dummy.h"
#include "../../include/ps2/veronica/prog/ps2_loadtim2.h"
#include "../../include/ps2/veronica/prog/ps2_NaTextureFunction.h"
#include "../../include/ps2/veronica/prog/main.h"

#include "pc_platform.h"
#include "pc_os.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

extern unsigned char Ps2_tex_mem[10485760];

void _builtin_set_imask(int mask)
{
}

void Ps2Init()
{
    Ps2_tex_buff = &Ps2_tex_mem;

    pcOsStartupLog("Ps2Init: before pcVideoInit");
    pcVideoInit();
    pcOsStartupLog("Ps2Init: after pcVideoInit");

    sceGsResetPath();
    sceGsResetGraph(0, SCE_GS_INTERLACE, SCE_GS_NTSC, SCE_GS_FIELD);

    sceGsSetDefDBuffDc(&Db, SCE_GS_PSMCT32, DISP_WIDTH, DISP_HEIGHT, SCE_GS_ZGEQUAL, SCE_GS_PSMZ16S, 1);

    Db.disp[0].dispfb.FBP = 150;
    Db.disp[0].dispfb.PSM = SCE_GS_PSMCT32;

    Db.draw01.frame1.FBP = 0;
    Db.draw01.frame1.PSM = SCE_GS_PSMCT32;

    Db.draw01.zbuf1.ZBP = 300;

    Db.draw02.frame2.FBP = 0;
    Db.draw02.frame2.PSM = SCE_GS_PSMCT32;

    Db.draw02.zbuf2.ZBP = 300;

    Db.disp[1].dispfb.FBP = 150;
    Db.disp[1].dispfb.PSM = SCE_GS_PSMCT32;

    Db.draw11.frame1.FBP = 0;
    Db.draw11.frame1.PSM = SCE_GS_PSMCT32;

    Db.draw11.zbuf1.ZBP = 300;

    Db.draw12.frame2.FBP = 0;
    Db.draw12.frame2.PSM = SCE_GS_PSMCT32;

    Db.draw12.zbuf2.ZBP = 300;

    Db.disp[0].display.DY = 636;
    Db.disp[1].display.DY = 32;

    {
        extern void pcGsSelfTest(void);

        pcOsStartupLog("Ps2Init: before pcGsSelfTest");
        pcGsSelfTest();
        pcOsStartupLog("Ps2Init: after pcGsSelfTest");
    }

    pcOsStartupLog("Ps2Init: before ClearVram");
    ClearVram();
    pcOsStartupLog("Ps2Init: after ClearVram");

    pcOsStartupLog("Ps2Init: before pcCdOpen");
    pcCdOpen();
    pcOsStartupLog("Ps2Init: after pcCdOpen");

    Ps2SetVSyncCounter();

    pcOsStartupLog("Ps2Init: before Ps2InitFunc");
    Ps2InitFunc();
    pcOsStartupLog("Ps2Init: after Ps2InitFunc");

    Ps2DispScreenClear();
    Ps2ScreenClear();

    Db.disp[0].pmode.EN2 = 0;
    Db.disp[1].pmode.EN2 = 0;

    Ps2SwapDBuff();

    Db.disp[0].pmode.EN2 = 1;
    Db.disp[1].pmode.EN2 = 1;

    Ps2InitTexCache();
    Ps2InitPS2_GS_SAVE();

    njColorBlendingMode(0, 8);
    njColorBlendingMode(1, 6);

    njUserClipping(0, NULL);

    _Make_SinTable();

    /* Ps2Vu0ProgSend / Ps2Vu1ProgSend: the VU microprograms are C code on PC */
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

/* GameCube START is translated to PS2 SELECT in either of two observed states:
 * 1) title Options: task 16 is runnable, while the game task (task 7) is absent;
 * 2) all four player-slot status words are exactly 0x4.
 *
 * The task scheduler in main.c uses tk_flg/ts_flg to decide whether task 16 runs.
 * DEMO PLAY also enables task 16 as part of its game-task set (tk_flg=0x73DFC0),
 * so the game-task bit must be absent to identify the title Options screen.
 * Do not use gm_mode: Battle Mode is a legitimate playable mode. */
int pcGameOptionsMenuActive(void)
{
    extern SYS_WORK* sys;

    return (sys != NULL) &&
           ((sys->tk_flg & (1u << 16)) != 0) &&
           ((sys->ts_flg & (1u << 16)) == 0) &&
           ((sys->tk_flg & (1u << 7)) == 0);
}

int pcGameAllPlayerSlotsAreFour(void)
{
    extern SYS_WORK* sys;
    int i;

    if (sys == NULL)
        return 0;

    for (i = 0; i < 4; i++)
    {
        if (sys->ply_stflg[i] != 0x4)
            return 0;
    }

    return 1;
}

int pcGameCubeStartUsesSelect(void)
{
    return pcGameOptionsMenuActive() || pcGameAllPlayerSlotsAreFour();
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


/* Live state overlay. Values are read directly from the game's SYS_WORK structure. */
static void pcDebugAppend(char* out, size_t cap, size_t* used, const char* fmt, ...)
{
    va_list ap;
    int n;

    if (*used >= cap)
        return;

    va_start(ap, fmt);
    n = vsnprintf(out + *used, cap - *used, fmt, ap);
    va_end(ap);

    if (n > 0)
    {
        size_t left = cap - *used;
        *used += ((size_t)n < left) ? (size_t)n : left - 1;
    }
}

static void pcDebugFlagLine(char* out, size_t cap, size_t* used, const char* name,
                            unsigned int value, int bit_count)
{
    char bits[33];
    int i;

    if (bit_count < 1) bit_count = 1;
    if (bit_count > 32) bit_count = 32;

    for (i = 0; i < bit_count; i++)
    {
        bits[i] = (value & (1u << (bit_count - i - 1))) ? '1' : '0';
    }
    bits[bit_count] = 0;

    pcDebugAppend(out, cap, used, "%-12s 0x%08X  %s\n", name, value, bits);
}

#define PC_DEBUG_FLAG(field) pcDebugFlagLine(out, cap, &used, #field, sys->field, 32)
#define PC_DEBUG_ARRAY(field, first, count) \
    do { \
        int pc_debug_i; \
        for (pc_debug_i = (first); pc_debug_i < (first) + (count); pc_debug_i++) { \
            char pc_debug_name[24]; \
            snprintf(pc_debug_name, sizeof(pc_debug_name), #field "[%02d]", pc_debug_i); \
            pcDebugFlagLine(out, cap, &used, pc_debug_name, sys->field[pc_debug_i], 32); \
        } \
    } while (0)

/* page layout:
 * 0 = runtime state + every scalar *_flg register
 * 1-2 = ev_flg, 3 = it_flg, 4 = ic_flg, 5-6 = ed_flg, 7 = ky_flg,
 * 8 = mp_flg + en_flg + ply_stflg
 */
void pcGameDebugText(int page, char* out, size_t cap)
{
    size_t used = 0;
    char input[768];

    if (out == NULL || cap == 0)
        return;

    out[0] = 0;

    if (sys == NULL)
    {
        pcDebugAppend(out, cap, &used, "RECVX LIVE DEBUG\nSYS_WORK is not initialized yet.\n");
        return;
    }

    if (page < 0 || page > 8)
        page = 0;

    pcDebugAppend(out, cap, &used,
                  "RECVX LIVE DEBUG  |  Ctrl+F10 hide  |  Ctrl+PgUp/PgDn page\n"
                  "PAGE %d/9  |  flags shown as HEX and binary (MSB -> LSB)\n",
                  page + 1);

    if (page == 0)
    {
        pcDebugAppend(out, cap, &used,
                      "GAME gm_mode=%d  stg=%d rom=%d rcase=%d pos=%d cut=%d flr=%d ply=%d\n"
                      "PAD pad_on=%08X pad_ps=%08X pad_rs=%08X pad_old=%08X\n"
                      "GC START rule: title_options=%d all_ply_stflg_4=%d -> PS2 %s\n",
                      sys->gm_mode, (int)sys->stg_no, (int)sys->rom_no, (int)sys->rcase,
                      (int)sys->pos_no, (int)sys->cut_no, (int)sys->flr_no, (int)sys->ply_id,
                      sys->pad_on, sys->pad_ps, sys->pad_rs, sys->pad_old,
                      pcGameOptionsMenuActive(), pcGameAllPlayerSlotsAreFour(),
                      pcGameCubeStartUsesSelect() ? "SELECT" : "START");

        input[0] = 0;
        {
            extern void pcPlatformDebugInputText(char* text, size_t size);
            pcPlatformDebugInputText(input, sizeof(input));
        }
        pcDebugAppend(out, cap, &used, "%s", input);

        PC_DEBUG_FLAG(ssd_flg);
        PC_DEBUG_FLAG(ss_flg);
        PC_DEBUG_FLAG(typ_flg);
        PC_DEBUG_FLAG(tk_flg);
        PC_DEBUG_FLAG(ts_flg);
        PC_DEBUG_FLAG(gm_flg);
        PC_DEBUG_FLAG(st_flg);
        PC_DEBUG_FLAG(cb_flg);
        PC_DEBUG_FLAG(rm_flg);
        PC_DEBUG_FLAG(pt_flg);
        PC_DEBUG_FLAG(sp_flg);
        PC_DEBUG_FLAG(ef_flg);
        PC_DEBUG_FLAG(sdm_flg);
        PC_DEBUG_FLAG(vsyc_flg);
        PC_DEBUG_FLAG(sco_flg);
        PC_DEBUG_FLAG(com_flg);
        PC_DEBUG_FLAG(opt_flg);
        pcDebugFlagLine(out, cap, &used, "mvi_flg", sys->mvi_flg, 16);
        pcDebugFlagLine(out, cap, &used, "ufo_flg", sys->ufo_flg, 8);
        pcDebugFlagLine(out, cap, &used, "brkfg", sys->brkfg, 32);
        pcDebugFlagLine(out, cap, &used, "brkfg_tkb", sys->brkfg_tkb, 32);
        pcDebugFlagLine(out, cap, &used, "brkfg_tsb", sys->brkfg_tsb, 32);
    }
    else
    {
        switch (page)
        {
        case 1: PC_DEBUG_ARRAY(ev_flg, 0, 16); break;
        case 2: PC_DEBUG_ARRAY(ev_flg, 16, 16); break;
        case 3: PC_DEBUG_ARRAY(it_flg, 0, 16); break;
        case 4: PC_DEBUG_ARRAY(ic_flg, 0, 16); break;
        case 5: PC_DEBUG_ARRAY(ed_flg, 0, 16); break;
        case 6: PC_DEBUG_ARRAY(ed_flg, 16, 16); break;
        case 7: PC_DEBUG_ARRAY(ky_flg, 0, 16); break;
        case 8:
            PC_DEBUG_ARRAY(mp_flg, 0, 8);
            PC_DEBUG_ARRAY(en_flg, 0, 4);
            PC_DEBUG_ARRAY(ply_stflg, 0, 4);
            break;
        }
    }

    if (used == 0)
        snprintf(out, cap, "RECVX LIVE DEBUG: no data");
}
