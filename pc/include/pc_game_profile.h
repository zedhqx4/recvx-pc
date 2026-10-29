/* Internal opt-in profiling hooks shared by PS2 game-code translation units. */
#pragma once

#ifdef TARGET_PC
enum
{
    PC_GAME_PROFILE_SCOPE_NONE = 0,
    PC_GAME_PROFILE_SCOPE_ROOM = 1,
    PC_GAME_PROFILE_SCOPE_ENEMY = 2,
    PC_GAME_PROFILE_SCOPE_MISC = 3
};

int pcGameProfileEnabled(void);
double pcGameProfileNowMs(void);
void pcGameProfileSetScope(int scope);
extern int pcGameProfileScope;
void pcGameProfileRecordCnk(int scope, double vlist_ms, double plist_ms,
                            unsigned int model_calls, unsigned int plist_chunks);
/* Diagnostic-only: time each V-list and P-list chunk handler by its PS2 type. */
void pcGameProfileRecordCnkType(int scope, unsigned int type, int plist, double ms);
/* Sub-profile the texture-upload path used by CNK type 8. */
void pcGameProfileSetTextureUploadScope(int active);
int pcGameProfileTextureUploadActive(void);
void pcGameProfileRecordTextureLoad(double total_ms, double send_ms, unsigned int send_calls);
/* Diagnostic-only phase timings; all are subsets of their containing functions. */
void pcGameProfileRecordTextureLoadDetail(double post_send_setup_ms, double final_loadimage_ms);
void pcGameProfileRecordTextureSendDetail(double clut_ms, double image_ms, double set_tex_ms, double other_ms);
/* Diagnostic-only identity and reuse counters for every Ps2TexLoad request. */
void pcGameProfileRecordTextureRequest(
    const void* texmemlist, const void* surface,
    unsigned int memlist_gindex, unsigned int tim2_gindex,
    unsigned int bank, unsigned int width, unsigned int height,
    unsigned int image_type, unsigned int pict_format,
    unsigned int clut_type, unsigned int clut_colors, unsigned int tp_flag,
    int last_surface_hit, int tp_cache_skip, int cache_break,
    unsigned int cache_flag, unsigned int send_calls, int scope,
    double load_ms, double send_ms);
void pcGameProfileRecordTextureTransfer(double ms, unsigned int qwc);
#endif
