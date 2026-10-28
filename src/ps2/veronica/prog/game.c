#include "../../../ps2/veronica/prog/game.h"
#include "../../../ps2/veronica/prog/MdlPut.h"
#include "../../../ps2/veronica/prog/camera.h"
#include "../../../ps2/veronica/prog/cut.h"
#include "../../../ps2/veronica/prog/effect.h"
#include "../../../ps2/veronica/prog/eneset.h"
#include "../../../ps2/veronica/prog/event.h"
#include "../../../ps2/veronica/prog/light.h"
#include "../../../ps2/veronica/prog/message.h"
#include "../../../ps2/veronica/prog/objitm.h"
#include "../../../ps2/veronica/prog/player.h"
#include "../../../ps2/veronica/prog/pwksub.h"
#include "../../../ps2/veronica/prog/room.h"
#include "../../../ps2/veronica/prog/screen.h"
#include "../../../ps2/veronica/prog/system.h"
#include "../../../ps2/veronica/prog/ps2_dummy.h"
#include "../../../ps2/veronica/prog/main.h"

#ifdef TARGET_PC
#include "../../../../pc/src/pc_os.h"
#include "../../../../pc/include/pc_platform.h"
#include "../../../../pc/include/pc_game_profile.h"

enum
{
    GP_TOTAL, GP_PRE, GP_ANIM, GP_ENEMY_UPDATE, GP_PLAYER_UPDATE,
    GP_EFFECT_UPDATE, GP_OBJECT_UPDATE, GP_CAMERA, GP_LIGHT,
    GP_DRAW_MODEL, GP_RENDER_TEXTURE, GP_MIRROR, GP_OVERLAYS, GP_OTHER,
    GP_MODEL_ROOM, GP_MODEL_ENEOBJ, GP_MODEL_EFFECT, GP_MODEL_OBJITEM,
    GP_MODEL_ENEMY, GP_MODEL_PLAYER, GP_MODEL_WATER, GP_MODEL_MISC, GP_COUNT
};

static struct
{
    double frame[GP_COUNT], sum[GP_COUNT], max[GP_COUNT], worst[GP_COUNT];
    unsigned int count;
    double worst_total;
    int active;
} game_profile;

int pcGameProfileScope;
static struct
{
    double frame_vlist[4], sum_vlist[4], max_vlist[4];
    double frame_plist[4], sum_plist[4], max_plist[4];
    unsigned int frame_models[4], sum_models[4], max_models[4];
    unsigned int frame_chunks[4], sum_chunks[4], max_chunks[4];
} cnk_profile;

/* Indexed by scope and raw PS2 CNK chunk type (ucType is an 8-bit value). */
static struct
{
    double frame_vlist_ms[4][256], sum_vlist_ms[4][256], max_vlist_ms[4][256];
    double frame_plist_ms[4][256], sum_plist_ms[4][256], max_plist_ms[4][256];
    unsigned int frame_vlist_calls[4][256], sum_vlist_calls[4][256];
    unsigned int frame_plist_calls[4][256], sum_plist_calls[4][256];
} cnk_type_profile;

/* Focused breakdown of type-8 texture selection and GS IMAGE-data transfers. */
static struct
{
    double frame_load_ms, sum_load_ms, max_load_ms;
    double frame_send_ms, sum_send_ms, max_send_ms;
    double frame_gs_ms, sum_gs_ms, max_gs_ms;
    double frame_load_calls, sum_load_calls, max_load_calls;
    double frame_send_calls, sum_send_calls, max_send_calls;
    double frame_no_send_calls, sum_no_send_calls, max_no_send_calls;
    double frame_gs_calls, sum_gs_calls, max_gs_calls;
    double frame_gs_qwc, sum_gs_qwc, max_gs_qwc;
} texture_profile;
static int texture_upload_scope;

int pcGameProfileEnabled(void)
{
    static int enabled = -1;

    if (enabled < 0)
        enabled = pcIniGetInt("debug", 0) && pcIniGetInt("profile_game", 0);

    return enabled;
}

double pcGameProfileNowMs(void)
{
    return pcOsTime() * 1000.0;
}

void pcGameProfileSetScope(int scope)
{
    pcGameProfileScope = (scope >= PC_GAME_PROFILE_SCOPE_NONE &&
                          scope <= PC_GAME_PROFILE_SCOPE_MISC) ? scope : PC_GAME_PROFILE_SCOPE_NONE;
}

void pcGameProfileRecordCnk(int scope, double vlist_ms, double plist_ms,
                            unsigned int model_calls, unsigned int plist_chunks)
{
    if (!game_profile.active || scope < PC_GAME_PROFILE_SCOPE_ROOM ||
        scope > PC_GAME_PROFILE_SCOPE_MISC)
        return;

    cnk_profile.frame_vlist[scope] += (vlist_ms >= 0.0) ? vlist_ms : 0.0;
    cnk_profile.frame_plist[scope] += (plist_ms >= 0.0) ? plist_ms : 0.0;
    cnk_profile.frame_models[scope] += model_calls;
    cnk_profile.frame_chunks[scope] += plist_chunks;
}

void pcGameProfileRecordCnkType(int scope, unsigned int type, int plist, double ms)
{
    if (!game_profile.active || scope < PC_GAME_PROFILE_SCOPE_ROOM ||
        scope > PC_GAME_PROFILE_SCOPE_MISC || type > 255)
        return;
    if (ms < 0.0)
        ms = 0.0;

    if (plist)
    {
        cnk_type_profile.frame_plist_ms[scope][type] += ms;
        cnk_type_profile.frame_plist_calls[scope][type]++;
    }
    else
    {
        cnk_type_profile.frame_vlist_ms[scope][type] += ms;
        cnk_type_profile.frame_vlist_calls[scope][type]++;
    }
}

void pcGameProfileSetTextureUploadScope(int active)
{
    texture_upload_scope = (active && game_profile.active) ? 1 : 0;
}

int pcGameProfileTextureUploadActive(void)
{
    return game_profile.active && texture_upload_scope;
}

void pcGameProfileRecordTextureLoad(double total_ms, double send_ms, unsigned int send_calls)
{
    if (!game_profile.active)
        return;

    texture_profile.frame_load_ms += (total_ms >= 0.0) ? total_ms : 0.0;
    texture_profile.frame_load_calls += 1.0;
    texture_profile.frame_send_ms += (send_ms >= 0.0) ? send_ms : 0.0;
    texture_profile.frame_send_calls += send_calls;
    if (send_calls == 0)
        texture_profile.frame_no_send_calls += 1.0;
}

void pcGameProfileRecordTextureTransfer(double ms, unsigned int qwc)
{
    if (!pcGameProfileTextureUploadActive())
        return;

    texture_profile.frame_gs_ms += (ms >= 0.0) ? ms : 0.0;
    texture_profile.frame_gs_calls += 1.0;
    texture_profile.frame_gs_qwc += qwc;
}

static void pcGameProfileBegin(void)
{
    int i;
    unsigned int type;

    texture_profile.frame_load_ms = 0.0;
    texture_profile.frame_send_ms = 0.0;
    texture_profile.frame_gs_ms = 0.0;
    texture_profile.frame_load_calls = 0.0;
    texture_profile.frame_send_calls = 0.0;
    texture_profile.frame_no_send_calls = 0.0;
    texture_profile.frame_gs_calls = 0.0;
    texture_profile.frame_gs_qwc = 0.0;
    texture_upload_scope = 0;

    for (i = 0; i < GP_COUNT; i++)
        game_profile.frame[i] = 0.0;
    for (i = 0; i < 4; i++)
    {
        cnk_profile.frame_vlist[i] = 0.0;
        cnk_profile.frame_plist[i] = 0.0;
        cnk_profile.frame_models[i] = 0;
        cnk_profile.frame_chunks[i] = 0;
        for (type = 0; type < 256; type++)
        {
            cnk_type_profile.frame_vlist_ms[i][type] = 0.0;
            cnk_type_profile.frame_plist_ms[i][type] = 0.0;
            cnk_type_profile.frame_vlist_calls[i][type] = 0;
            cnk_type_profile.frame_plist_calls[i][type] = 0;
        }
    }
    pcGameProfileScope = PC_GAME_PROFILE_SCOPE_NONE;
    game_profile.active = 1;
}

static void pcGameProfileAdd(int slot, double ms)
{
    if (game_profile.active && slot >= 0 && slot < GP_COUNT)
        game_profile.frame[slot] += (ms >= 0.0) ? ms : 0.0;
}

static void pcGameProfileLogCnkTypes(int scope, const char* label, double frames)
{
    int used[256];
    int rank, type, best_type;
    double best_ms;

    for (type = 0; type < 256; type++)
        used[type] = 0;

    for (rank = 0; rank < 6; rank++)
    {
        best_type = -1;
        best_ms = 0.0;
        for (type = 0; type < 256; type++)
        {
            double value = cnk_type_profile.sum_vlist_ms[scope][type] +
                           cnk_type_profile.sum_plist_ms[scope][type];
            if (!used[type] && value > best_ms)
            {
                best_ms = value;
                best_type = type;
            }
        }
        if (best_type < 0)
            break;

        used[best_type] = 1;
        pcLog("PROFILE CNK type %s=%d avg ms %.3f (vlist %.3f, plist %.3f) calls/frame %.2f/%.2f max-frame ms %.3f/%.3f",
              label, best_type, best_ms / frames,
              cnk_type_profile.sum_vlist_ms[scope][best_type] / frames,
              cnk_type_profile.sum_plist_ms[scope][best_type] / frames,
              (double)cnk_type_profile.sum_vlist_calls[scope][best_type] / frames,
              (double)cnk_type_profile.sum_plist_calls[scope][best_type] / frames,
              cnk_type_profile.max_vlist_ms[scope][best_type],
              cnk_type_profile.max_plist_ms[scope][best_type]);
    }
}

static void pcGameProfileEnd(int loop_ct, double total_ms)
{
    unsigned int i, type;
    double covered;

    game_profile.active = 0;
    texture_upload_scope = 0;
    game_profile.frame[GP_TOTAL] = total_ms;

    /* GP_OTHER is the uninstrumented remainder, including state/event bookkeeping. */
    covered = game_profile.frame[GP_PRE] + game_profile.frame[GP_ANIM] +
              game_profile.frame[GP_ENEMY_UPDATE] + game_profile.frame[GP_PLAYER_UPDATE] +
              game_profile.frame[GP_EFFECT_UPDATE] + game_profile.frame[GP_OBJECT_UPDATE] +
              game_profile.frame[GP_CAMERA] + game_profile.frame[GP_LIGHT] +
              game_profile.frame[GP_DRAW_MODEL] + game_profile.frame[GP_RENDER_TEXTURE] +
              game_profile.frame[GP_MIRROR] + game_profile.frame[GP_OVERLAYS];
    game_profile.frame[GP_OTHER] = (total_ms > covered) ? total_ms - covered : 0.0;

    for (i = 0; i < GP_COUNT; i++)
    {
        double value = game_profile.frame[i];
        game_profile.sum[i] += value;
        if (value > game_profile.max[i])
            game_profile.max[i] = value;
    }

    texture_profile.sum_load_ms += texture_profile.frame_load_ms;
    texture_profile.sum_send_ms += texture_profile.frame_send_ms;
    texture_profile.sum_gs_ms += texture_profile.frame_gs_ms;
    texture_profile.sum_load_calls += texture_profile.frame_load_calls;
    texture_profile.sum_send_calls += texture_profile.frame_send_calls;
    texture_profile.sum_no_send_calls += texture_profile.frame_no_send_calls;
    texture_profile.sum_gs_calls += texture_profile.frame_gs_calls;
    texture_profile.sum_gs_qwc += texture_profile.frame_gs_qwc;
    if (texture_profile.frame_load_ms > texture_profile.max_load_ms)
        texture_profile.max_load_ms = texture_profile.frame_load_ms;
    if (texture_profile.frame_send_ms > texture_profile.max_send_ms)
        texture_profile.max_send_ms = texture_profile.frame_send_ms;
    if (texture_profile.frame_gs_ms > texture_profile.max_gs_ms)
        texture_profile.max_gs_ms = texture_profile.frame_gs_ms;
    if (texture_profile.frame_load_calls > texture_profile.max_load_calls)
        texture_profile.max_load_calls = texture_profile.frame_load_calls;
    if (texture_profile.frame_send_calls > texture_profile.max_send_calls)
        texture_profile.max_send_calls = texture_profile.frame_send_calls;
    if (texture_profile.frame_no_send_calls > texture_profile.max_no_send_calls)
        texture_profile.max_no_send_calls = texture_profile.frame_no_send_calls;
    if (texture_profile.frame_gs_calls > texture_profile.max_gs_calls)
        texture_profile.max_gs_calls = texture_profile.frame_gs_calls;
    if (texture_profile.frame_gs_qwc > texture_profile.max_gs_qwc)
        texture_profile.max_gs_qwc = texture_profile.frame_gs_qwc;

    for (i = PC_GAME_PROFILE_SCOPE_ROOM; i <= PC_GAME_PROFILE_SCOPE_MISC; i++)
    {
        cnk_profile.sum_vlist[i] += cnk_profile.frame_vlist[i];
        cnk_profile.sum_plist[i] += cnk_profile.frame_plist[i];
        cnk_profile.sum_models[i] += cnk_profile.frame_models[i];
        cnk_profile.sum_chunks[i] += cnk_profile.frame_chunks[i];
        for (type = 0; type < 256; type++)
        {
            cnk_type_profile.sum_vlist_ms[i][type] += cnk_type_profile.frame_vlist_ms[i][type];
            cnk_type_profile.sum_plist_ms[i][type] += cnk_type_profile.frame_plist_ms[i][type];
            cnk_type_profile.sum_vlist_calls[i][type] += cnk_type_profile.frame_vlist_calls[i][type];
            cnk_type_profile.sum_plist_calls[i][type] += cnk_type_profile.frame_plist_calls[i][type];
            if (cnk_type_profile.frame_vlist_ms[i][type] > cnk_type_profile.max_vlist_ms[i][type])
                cnk_type_profile.max_vlist_ms[i][type] = cnk_type_profile.frame_vlist_ms[i][type];
            if (cnk_type_profile.frame_plist_ms[i][type] > cnk_type_profile.max_plist_ms[i][type])
                cnk_type_profile.max_plist_ms[i][type] = cnk_type_profile.frame_plist_ms[i][type];
        }
        if (cnk_profile.frame_vlist[i] > cnk_profile.max_vlist[i])
            cnk_profile.max_vlist[i] = cnk_profile.frame_vlist[i];
        if (cnk_profile.frame_plist[i] > cnk_profile.max_plist[i])
            cnk_profile.max_plist[i] = cnk_profile.frame_plist[i];
        if (cnk_profile.frame_models[i] > cnk_profile.max_models[i])
            cnk_profile.max_models[i] = cnk_profile.frame_models[i];
        if (cnk_profile.frame_chunks[i] > cnk_profile.max_chunks[i])
            cnk_profile.max_chunks[i] = cnk_profile.frame_chunks[i];
    }

    if (total_ms > game_profile.worst_total)
    {
        game_profile.worst_total = total_ms;
        for (i = 0; i < GP_COUNT; i++)
            game_profile.worst[i] = game_profile.frame[i];
    }

    game_profile.count++;
    if (game_profile.count >= 60)
    {
        double n = (double)game_profile.count;

        pcLog("PROFILE bhMainSequence n=%u loop_ct=%d avg/max ms: total %.2f/%.2f pre %.2f/%.2f anim %.2f/%.2f enemy-up %.2f/%.2f player-up %.2f/%.2f",
              game_profile.count, loop_ct,
              game_profile.sum[GP_TOTAL] / n, game_profile.max[GP_TOTAL],
              game_profile.sum[GP_PRE] / n, game_profile.max[GP_PRE],
              game_profile.sum[GP_ANIM] / n, game_profile.max[GP_ANIM],
              game_profile.sum[GP_ENEMY_UPDATE] / n, game_profile.max[GP_ENEMY_UPDATE],
              game_profile.sum[GP_PLAYER_UPDATE] / n, game_profile.max[GP_PLAYER_UPDATE]);

        pcLog("PROFILE bhMainSequence avg/max ms: effects-up %.2f/%.2f objects-up %.2f/%.2f camera %.2f/%.2f light %.2f/%.2f draw-model %.2f/%.2f render-texture %.2f/%.2f mirror %.2f/%.2f overlays %.2f/%.2f other %.2f/%.2f",
              game_profile.sum[GP_EFFECT_UPDATE] / n, game_profile.max[GP_EFFECT_UPDATE],
              game_profile.sum[GP_OBJECT_UPDATE] / n, game_profile.max[GP_OBJECT_UPDATE],
              game_profile.sum[GP_CAMERA] / n, game_profile.max[GP_CAMERA],
              game_profile.sum[GP_LIGHT] / n, game_profile.max[GP_LIGHT],
              game_profile.sum[GP_DRAW_MODEL] / n, game_profile.max[GP_DRAW_MODEL],
              game_profile.sum[GP_RENDER_TEXTURE] / n, game_profile.max[GP_RENDER_TEXTURE],
              game_profile.sum[GP_MIRROR] / n, game_profile.max[GP_MIRROR],
              game_profile.sum[GP_OVERLAYS] / n, game_profile.max[GP_OVERLAYS],
              game_profile.sum[GP_OTHER] / n, game_profile.max[GP_OTHER]);

        pcLog("PROFILE bhAllDrawModel avg/max ms: room geometry %.2f/%.2f enemy objects %.2f/%.2f effects %.2f/%.2f objects/items %.2f/%.2f enemies %.2f/%.2f player %.2f/%.2f water %.2f/%.2f other/setup %.2f/%.2f",
              game_profile.sum[GP_MODEL_ROOM] / n, game_profile.max[GP_MODEL_ROOM],
              game_profile.sum[GP_MODEL_ENEOBJ] / n, game_profile.max[GP_MODEL_ENEOBJ],
              game_profile.sum[GP_MODEL_EFFECT] / n, game_profile.max[GP_MODEL_EFFECT],
              game_profile.sum[GP_MODEL_OBJITEM] / n, game_profile.max[GP_MODEL_OBJITEM],
              game_profile.sum[GP_MODEL_ENEMY] / n, game_profile.max[GP_MODEL_ENEMY],
              game_profile.sum[GP_MODEL_PLAYER] / n, game_profile.max[GP_MODEL_PLAYER],
              game_profile.sum[GP_MODEL_WATER] / n, game_profile.max[GP_MODEL_WATER],
              game_profile.sum[GP_MODEL_MISC] / n, game_profile.max[GP_MODEL_MISC]);

        pcLog("PROFILE worst frame %.2f ms: pre %.2f anim %.2f enemy-up %.2f player-up %.2f effects-up %.2f objects-up %.2f camera %.2f light %.2f draw-model %.2f mirror %.2f overlays %.2f other %.2f",
              game_profile.worst_total,
              game_profile.worst[GP_PRE], game_profile.worst[GP_ANIM],
              game_profile.worst[GP_ENEMY_UPDATE], game_profile.worst[GP_PLAYER_UPDATE],
              game_profile.worst[GP_EFFECT_UPDATE], game_profile.worst[GP_OBJECT_UPDATE],
              game_profile.worst[GP_CAMERA], game_profile.worst[GP_LIGHT],
              game_profile.worst[GP_DRAW_MODEL], game_profile.worst[GP_MIRROR],
              game_profile.worst[GP_OVERLAYS], game_profile.worst[GP_OTHER]);

        pcLog("PROFILE CNK room avg/max ms: vlist %.2f/%.2f plist %.2f/%.2f | models/frame %u/%u plist-chunks/frame %u/%u",
              cnk_profile.sum_vlist[PC_GAME_PROFILE_SCOPE_ROOM] / n,
              cnk_profile.max_vlist[PC_GAME_PROFILE_SCOPE_ROOM],
              cnk_profile.sum_plist[PC_GAME_PROFILE_SCOPE_ROOM] / n,
              cnk_profile.max_plist[PC_GAME_PROFILE_SCOPE_ROOM],
              cnk_profile.sum_models[PC_GAME_PROFILE_SCOPE_ROOM] / game_profile.count,
              cnk_profile.max_models[PC_GAME_PROFILE_SCOPE_ROOM],
              cnk_profile.sum_chunks[PC_GAME_PROFILE_SCOPE_ROOM] / game_profile.count,
              cnk_profile.max_chunks[PC_GAME_PROFILE_SCOPE_ROOM]);

        pcLog("PROFILE CNK enemy avg/max ms: vlist %.2f/%.2f plist %.2f/%.2f | models/frame %u/%u plist-chunks/frame %u/%u",
              cnk_profile.sum_vlist[PC_GAME_PROFILE_SCOPE_ENEMY] / n,
              cnk_profile.max_vlist[PC_GAME_PROFILE_SCOPE_ENEMY],
              cnk_profile.sum_plist[PC_GAME_PROFILE_SCOPE_ENEMY] / n,
              cnk_profile.max_plist[PC_GAME_PROFILE_SCOPE_ENEMY],
              cnk_profile.sum_models[PC_GAME_PROFILE_SCOPE_ENEMY] / game_profile.count,
              cnk_profile.max_models[PC_GAME_PROFILE_SCOPE_ENEMY],
              cnk_profile.sum_chunks[PC_GAME_PROFILE_SCOPE_ENEMY] / game_profile.count,
              cnk_profile.max_chunks[PC_GAME_PROFILE_SCOPE_ENEMY]);

        pcLog("PROFILE TEX Ps2TexLoad avg/max ms %.2f/%.2f calls/frame %.2f/%.2f | Send_Tim2_dataEx ms %.2f/%.2f calls/frame %.2f/%.2f no-send/frame %.2f",
              texture_profile.sum_load_ms / n, texture_profile.max_load_ms,
              texture_profile.sum_load_calls / n, texture_profile.max_load_calls,
              texture_profile.sum_send_ms / n, texture_profile.max_send_ms,
              texture_profile.sum_send_calls / n, texture_profile.max_send_calls,
              texture_profile.sum_no_send_calls / n);
        pcLog("PROFILE TEX GS IMAGE upload avg/max ms %.2f/%.2f calls/frame %.2f/%.2f QWC/frame %.0f/%.0f",
              texture_profile.sum_gs_ms / n, texture_profile.max_gs_ms,
              texture_profile.sum_gs_calls / n, texture_profile.max_gs_calls,
              texture_profile.sum_gs_qwc / n, texture_profile.max_gs_qwc);

        pcGameProfileLogCnkTypes(PC_GAME_PROFILE_SCOPE_ROOM, "room", n);
        pcGameProfileLogCnkTypes(PC_GAME_PROFILE_SCOPE_ENEMY, "enemy", n);

        for (i = 0; i < GP_COUNT; i++)
        {
            game_profile.sum[i] = 0.0;
            game_profile.max[i] = 0.0;
        }
        for (i = 0; i < 4; i++)
        {
            cnk_profile.sum_vlist[i] = cnk_profile.max_vlist[i] = 0.0;
            cnk_profile.sum_plist[i] = cnk_profile.max_plist[i] = 0.0;
            cnk_profile.sum_models[i] = cnk_profile.max_models[i] = 0;
            cnk_profile.sum_chunks[i] = cnk_profile.max_chunks[i] = 0;
            for (type = 0; type < 256; type++)
            {
                cnk_type_profile.sum_vlist_ms[i][type] = 0.0;
                cnk_type_profile.max_vlist_ms[i][type] = 0.0;
                cnk_type_profile.sum_plist_ms[i][type] = 0.0;
                cnk_type_profile.max_plist_ms[i][type] = 0.0;
                cnk_type_profile.sum_vlist_calls[i][type] = 0;
                cnk_type_profile.sum_plist_calls[i][type] = 0;
            }
        }
        texture_profile.sum_load_ms = texture_profile.max_load_ms = 0.0;
        texture_profile.sum_send_ms = texture_profile.max_send_ms = 0.0;
        texture_profile.sum_gs_ms = texture_profile.max_gs_ms = 0.0;
        texture_profile.sum_load_calls = texture_profile.max_load_calls = 0.0;
        texture_profile.sum_send_calls = texture_profile.max_send_calls = 0.0;
        texture_profile.sum_no_send_calls = texture_profile.max_no_send_calls = 0.0;
        texture_profile.sum_gs_calls = texture_profile.max_gs_calls = 0.0;
        texture_profile.sum_gs_qwc = texture_profile.max_gs_qwc = 0.0;
        game_profile.count = 0;
        game_profile.worst_total = 0.0;
    }
}

#define PC_PROFILE_CALL(enabled, slot, call) \
    do { if (enabled) { double _profile_t0 = pcGameProfileNowMs(); call; \
         pcGameProfileAdd((slot), pcGameProfileNowMs() - _profile_t0); } else { call; } } while (0)
#define PC_PROFILE_SCOPE_CALL(enabled, scope, slot, call) \
    do { if (enabled) { int _old_scope = pcGameProfileScope; \
         double _profile_t0 = pcGameProfileNowMs(); \
         pcGameProfileSetScope(scope); call; pcGameProfileSetScope(_old_scope); \
         pcGameProfileAdd((slot), pcGameProfileNowMs() - _profile_t0); } else { call; } } while (0)
#else
#define PC_PROFILE_CALL(enabled, slot, call) do { call; } while (0)
#define PC_PROFILE_SCOPE_CALL(enabled, scope, slot, call) do { call; } while (0)
#endif

// 100% matching! 
void bhMainSequence()
{
    int i;
#ifdef TARGET_PC
    int profile = pcGameProfileEnabled();
    double profile_frame_start = 0.0;

    if (profile)
    {
        pcGameProfileBegin();
        profile_frame_start = pcGameProfileNowMs();
    }
#endif

    if ((sys->cb_flg & 0x1)) 
    {
        PC_PROFILE_CALL(profile, GP_PRE, bhStartDoorDemo());
    }
    
    if (sys->evt_tmd != 0) 
    {
        PC_PROFILE_CALL(profile, GP_PRE, bhCheckEvtTimer());
    }
    
    for (i = 0; i < sys->loop_ct; i++)
    {
        sys->loop_now = i;
        
        if ((sys->cb_flg & 0x2)) 
        {
            PC_PROFILE_CALL(profile, GP_ANIM, bhControlScreenFade());
        }
            
        if (((sys->cb_flg & 0x4)) || (sys->cine_an)) 
        {
            PC_PROFILE_CALL(profile, GP_ANIM, bhControlCinesco());
        }
        
        PC_PROFILE_CALL(profile, GP_ENEMY_UPDATE, bhControlEnemy());
        PC_PROFILE_CALL(profile, GP_PLAYER_UPDATE, bhControlPlayer());
        PC_PROFILE_CALL(profile, GP_EFFECT_UPDATE, bhControlEffect());
        PC_PROFILE_CALL(profile, GP_OBJECT_UPDATE, bhControlObjItm());
            
        if (((sys->gm_flg & 0x40)) && ((sys->sp_flg & 0x40)) && (!(sys->st_flg & 0x1)))
        {
            PC_PROFILE_CALL(profile, GP_CAMERA, bhSetPlEyeCamera());
        }
        
        PC_PROFILE_CALL(profile, GP_CAMERA, bhControlCamera());
        PC_PROFILE_CALL(profile, GP_LIGHT, bhControlLight());
            
        if (i == (sys->loop_ct - 1)) 
        {
            if ((sys->gm_flg & 0x200))
            {
                PC_PROFILE_CALL(profile, GP_RENDER_TEXTURE, bhDrawSmallScreenRenderTexture());
            }
            
            if ((sys->gm_flg & 0x100)) 
            {
                PC_PROFILE_CALL(profile, GP_RENDER_TEXTURE, bhDrawFullScreenRenderTexture());
            } 
            else 
            {
                PC_PROFILE_CALL(profile, GP_DRAW_MODEL, bhAllDrawModel());
            }
            
            if ((sys->st_flg & 0x100)) 
            {
                sys->gm_flg |= 0x4000;
                
                njSetMatrix(cam.mtxb, cam.mtx);
                
                njMirror(cam.mtx, &sys->mr_pl);
                
                bhSetLight();
                
                PC_PROFILE_CALL(profile, GP_MIRROR, bhEtcMirrorDrawModel());
                
                njSetMatrix(cam.mtx, cam.mtxb);
                
                sys->gm_flg &= ~0x4000;
            }
            
            if (sys->cine_an > 0) 
            {
                PC_PROFILE_CALL(profile, GP_OVERLAYS, bhDrawCinesco());
            }
            
            if ((sys->st_flg & 0x800000)) 
            {
                PC_PROFILE_CALL(profile, GP_OVERLAYS, bhDrawScope());
            }
            
            if ((sys->st_flg & 0x40000000))
            {
                PC_PROFILE_CALL(profile, GP_OVERLAYS, bhDrawThermometer());
            }
            
            if (sys->fade_an > 0)
            {
                PC_PROFILE_CALL(profile, GP_OVERLAYS, bhDrawScreenFade());
            }
        }
        
        if (((sys->st_flg & 0x200000)) && (!(sys->cb_flg & 0x1)))
        {
            sys->st_flg &= ~0x200080;
            
            if ((sys->cb_flg & 0x20000000))
            {
                sys->cb_flg |= 0x1000;
                
                sys->cb_flg &= ~0x20000000;
            }
            
            if (!(sys->cb_flg & 0x4))
            {
                sys->st_flg &= ~0x4;
            }
            
            sys->sp_flg = ~0x0;
            
            sys->pt_flg |= 0x3;
            
            if (!(sys->gm_flg & 0x20000)) 
            {
                sys->st_flg &= ~0x1;
                
                sys->gm_flg |= 0x800;
            }
            
            sys->pad_on = sys->pad_onb;
            sys->pad_ps = sys->pad_psb;
            sys->pad_old = sys->pad_oldb;
        }
        
        if (((sys->st_flg & 0x400000)) && (!(sys->cb_flg & 0x1))) 
        {
            if ((sys->cb_flg & 0x20000000)) 
            {
                sys->cb_flg |= 0x1000;
                
                sys->cb_flg &= ~0x20000000;
            }
            
            if ((sys->cb_flg & 0x200000)) 
            {
                sys->st_flg &= ~0x200;
            } 
            else 
            {
                sys->st_flg &= ~0x400200;
                
                if ((sys->st_flg & 0x2000)) 
                {
                    sys->sp_flg = ~0x0;
                    
                    sys->st_flg &= ~0x2200;
                    
                    if (!(sys->cb_flg & 0x4)) 
                    {
                        sys->st_flg &= ~0x4;
                    }
                    
                    sys->pad_on = sys->pad_onb;
                    sys->pad_ps = sys->pad_psb;
                    sys->pad_old = sys->pad_oldb;
                }
                
                if ((sys->st_flg & 0x80)) 
                {
                    sys->st_flg &= ~0x80;
                    
                    sys->pt_flg |= 0x3;
                    
                    if (!(sys->gm_flg & 0x20000))
                    {
                        sys->st_flg &= ~0x1;
                        
                        sys->gm_flg |= 0x800;
                    }
                }
            } 
        }
        
        if ((sys->cb_flg & 0x8)) 
        {
            sys->cb_flg &= ~0x8;
            
            sys->sp_flg = 0;
            
            sys->sp_flg |= 0x38;
            
            if (!(sys->exm_attr & 0x20)) 
            {
                sys->sp_flg |= 0x80;
            }
            
            if ((sys->exm_attr & 0x2)) 
            {
                sys->pt_flg &= ~0x1;
                
                rom->lgtp[1].flg &= ~0x2;
            }
            
            if ((sys->exm_attr & 0x4))
            {
                sys->pt_flg &= ~0x2;
            }
            
            if ((sys->exm_attr & 0x10)) 
            {
                sys->sp_flg |= 0x2;
            }
        }
        
        if ((sys->st_flg & 0x80))
        {
            if (((sys->gm_flg & 0x10000)) && ((sys->gm_flg & 0x20000))) 
            {
                sys->sp_flg |= 0x40;
            }
            
            if (((sys->pad_ps & 0x200)) && (!(sys->st_flg & 0x200))) 
            {
                sys->st_flg |= 0x200000;
            }
        }
        
        if ((sys->cb_flg & 0x20)) 
        {
            sys->cb_flg &= ~0x20;
            
            sys->sp_flg = 0;
            
            sys->sp_flg |= 0x30;
            
            if ((sys->st_flg & 0x80)) 
            {
                sys->sp_flg |= 0x8;
                
                if (!(sys->exm_attr & 0x20)) 
                {
                    sys->sp_flg |= 0x80;
                }
            }
            
            if ((sys->exm_attr & 0x10))
            {
                sys->sp_flg |= 0x2;
            }
        }
        
        if ((((sys->st_flg & 0x200)) && ((sys->st_flg & 0x4000)) && ((sys->st_flg & 0x1000))) && ((((sys->pad_ps & 0x800)) || ((sys->pad_ps & 0x1000))) || (((sys->cb_flg & 0x1000)) && (!(sys->cb_flg & 0x1)))))
        {
            sys->st_flg |= 0x400000;
            
            sys->cb_flg |= 0x2000;
            
            sys->cb_flg &= ~0x1000;
        }
        
        if ((sys->gm_flg & 0x1000))
        {
            if (!(sys->gm_flg & 0x10000)) 
            {
                bhSetFixedCut(sys->fixcno);
            } 
            else 
            {
                bhSetEventFixedCut(sys->fixcno, sys->fixkno);
            }
        }
        else if ((sys->gm_flg & 0x2000)) 
        {
            bhInitPlEyeCamera();
        }
        
        if (!(sys->gm_flg & 0x800))
        {
            bhCheckCut(0);
        }
        else 
        {
            bhCheckCut(1);
        }
        
        if (sys->loop_ct > 1) 
        {
            if ((sys->st_flg & 0x200)) 
            {
                if ((sys->ts_flg & 0x200)) 
                {
                    bhControlMessage(0);
                } 
                else
                {
                    bhControlMessage(1);
                }
            }
            
            bhControlEvent();
        }
    }

#ifdef TARGET_PC
    if (profile)
        pcGameProfileEnd(sys->loop_ct, pcGameProfileNowMs() - profile_frame_start);
#endif
}

// 100% matching
void bhAllDrawModel()
{
#ifdef TARGET_PC
    int profile = pcGameProfileEnabled();
    double model_profile_start = 0.0;
    double model_profile_children_start = 0.0;

    if (profile)
    {
        model_profile_start = pcGameProfileNowMs();
        model_profile_children_start =
            game_profile.frame[GP_MODEL_ROOM] + game_profile.frame[GP_MODEL_ENEOBJ] +
            game_profile.frame[GP_MODEL_EFFECT] + game_profile.frame[GP_MODEL_OBJITEM] +
            game_profile.frame[GP_MODEL_ENEMY] + game_profile.frame[GP_MODEL_PLAYER] +
            game_profile.frame[GP_MODEL_WATER];
    }
#endif
    njControl3D(0x2500);
    njSetCheapShadowMode(0x80);
    njCnkSetEasyMultiAmbient(rom->amb_r[rom->amb_rom], rom->amb_g[rom->amb_rom], rom->amb_b[rom->amb_rom]);
    njCnkSetSimpleMultiAmbient(rom->amb_r[rom->amb_rom], rom->amb_g[rom->amb_rom], rom->amb_b[rom->amb_rom]);
    if ((rom->mdl.texP != 0) && !(sys->error & 2)) {
        njSetTexture(rom->mdl.texP);
    }
    njSetMatrix(0, cam.mtx);
    if ((rom->mdl.objP != 0) && (sys->pt_flg & 0x20)) {
        if (sys->gm_flg & 0x80) {
            PC_PROFILE_SCOPE_CALL(profile, PC_GAME_PROFILE_SCOPE_ROOM, GP_MODEL_ROOM, njCnkEasyDrawObject(rom->mdl.objP));
        } else {
            PC_PROFILE_SCOPE_CALL(profile, PC_GAME_PROFILE_SCOPE_ROOM, GP_MODEL_ROOM, njCnkEasyMultiDrawObject(rom->mdl.objP));
        }
    }
    njControl3D(0x2400);
    if ((sys->en_objn != 0) && (sys->pt_flg & 2)) {
        PC_PROFILE_CALL(profile, GP_MODEL_ENEOBJ, bhDrawEneObject());
    }
    if ((Ps2_albinoid_flag != 0) && (sys->pt_flg & 0x10)) {
        PC_PROFILE_CALL(profile, GP_MODEL_EFFECT, bhDrawEffect());
    }
    PC_PROFILE_CALL(profile, GP_MODEL_OBJITEM, bhDrawObjItm());
    njControl3D(0x100);
    njCnkSetEasyMultiAmbient(rom->amb_r[rom->amb_chr], rom->amb_g[rom->amb_chr], rom->amb_b[rom->amb_chr]);
    njCnkSetSimpleMultiAmbient(rom->amb_r[rom->amb_chr], rom->amb_g[rom->amb_chr], rom->amb_b[rom->amb_chr]);
    if (Ps2_albinoid_flag == 0) {
        if (sys->pt_flg & 0x10) {
            PC_PROFILE_CALL(profile, GP_MODEL_EFFECT, bhDrawEffect());
        }
    }
    njControl3D(0);
    bhSetHalfLight();
    if (sys->pt_flg & 2) {
        PC_PROFILE_SCOPE_CALL(profile, PC_GAME_PROFILE_SCOPE_ENEMY, GP_MODEL_ENEMY, bhDrawEnemy());
    }
    njControl3D(0);
    if (((sys->pt_flg & 1) && !(plp->stflg & 0x01000000)) || ((plp->stflg & 0x01000000) && (pl_sleep_cnt != 0))) {
        if (pl_sleep_cnt != 0) {
            pl_sleep_cnt -= 1;
        }
        if (!(plp->mdflg & 1)) {
            if (!(plp->mdflg & 0x20)) {
                if (bhCheckClipModel(plp) == 0) {
                    PC_PROFILE_CALL(profile, GP_MODEL_PLAYER, bhPutModel(plp));
                }
            } else {
                PC_PROFILE_CALL(profile, GP_MODEL_PLAYER, bhPutModel(plp));
            }
        }
    }
    if ((rom->mdl.texP != 0) && !(sys->error & 2)) {
        njSetTexture(rom->mdl.texP);
    }
    njSetMatrix(0, cam.mtx);
    njControl3D(0x2400);
    PC_PROFILE_CALL(profile, GP_MODEL_WATER, bhDrawGeneralPurposeWater());
    njControl3D(0);
#ifdef TARGET_PC
    if (profile)
    {
        double children_ms =
            game_profile.frame[GP_MODEL_ROOM] + game_profile.frame[GP_MODEL_ENEOBJ] +
            game_profile.frame[GP_MODEL_EFFECT] + game_profile.frame[GP_MODEL_OBJITEM] +
            game_profile.frame[GP_MODEL_ENEMY] + game_profile.frame[GP_MODEL_PLAYER] +
            game_profile.frame[GP_MODEL_WATER] - model_profile_children_start;
        double model_ms = pcGameProfileNowMs() - model_profile_start;
        pcGameProfileAdd(GP_MODEL_MISC, model_ms - children_ms);
    }
#endif
}

// 100% matching
void bhAllEasyDrawModel()
{
    njControl3D(0x18100);
    njSetCnkBlendMode(0);
    njCnkSetEasyMultiAmbient(rom->amb_r[rom->amb_rom], rom->amb_g[rom->amb_rom], rom->amb_b[rom->amb_rom]);
    njCnkSetSimpleMultiAmbient(rom->amb_r[rom->amb_rom], rom->amb_g[rom->amb_rom], rom->amb_b[rom->amb_rom]);
    if ((rom->mdl.texP != 0) && !(sys->error & 2)) {
        njSetTexture(rom->mdl.texP);
    }
    njSetMatrix(0, cam.mtx);
    if ((rom->mdl.objP != 0) && (sys->pt_flg & 0x20)) {
        if (sys->gm_flg & 0x80) {
            njCnkEasyDrawObject(rom->mdl.objP);
        } else {
            njCnkEasyMultiDrawObject(rom->mdl.objP); 
        }
    }
    bhDrawObjItm();
    njControl3D(0x10000);
    if (sys->pt_flg & 0x2) {
        bhDrawEnemy();
    }
}

// 100% matching
void bhEtcMirrorDrawModel()
{
    bhSetHideObject(4);
    njControl3D(0x12500);
    njSetCheapShadowMode(0x80);
    njCnkSetEasyMultiAmbient(rom->amb_r[rom->amb_rom], rom->amb_g[rom->amb_rom], rom->amb_b[rom->amb_rom]);
    njCnkSetSimpleMultiAmbient(rom->amb_r[rom->amb_rom], rom->amb_g[rom->amb_rom], rom->amb_b[rom->amb_rom]);
    if ((rom->mdl.texP != 0) && !(sys->error & 2)) {
        njSetTexture(rom->mdl.texP);
    }
    njSetMatrix(0, cam.mtx);
    if ((rom->mdl.objP != 0) && (sys->pt_flg & 0x20)) {
        if (sys->gm_flg & 0x80) {
            njCnkEasyDrawObject(rom->mdl.objP);
        } else {
            njCnkEasyMultiDrawObject(rom->mdl.objP); 
        }
    }
    bhSetHideObject(0);
    njControl3D(0x100);
    if (sys->pt_flg & 0x10) {
        bhDrawEffect();
    }
}
