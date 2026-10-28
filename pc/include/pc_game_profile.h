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
#endif
