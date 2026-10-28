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
void pcGameProfileRecordTextureTransfer(double ms, unsigned int qwc);
#endif
