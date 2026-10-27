#include "../../../ps2/veronica/prog/njloop.h"

#ifdef TARGET_PC
#include "../../../../pc/src/pc_os.h"
#endif

// 100% matching!
int main(int argc, char *argv[])
{
#ifdef TARGET_PC
    /* Install diagnostics before the PS2 initialization path: failures before SDL/window setup
       previously happened before the crash handler and could leave recvx.log empty. */
    pcOsStartupLog("main entered");
    pcOsInstallDiagnostics();
    pcOsStartupLog("diagnostics installed; entering njUserInit");
#endif
    njUserInit();

#ifdef TARGET_PC
    pcOsStartupLog("njUserInit returned");
#endif

    {
        int first_frame_trace = 0;
        while (TRUE)
        {
#ifdef TARGET_PC
            if (first_frame_trace == 0)
            {
                pcOsStartupLog("entering first njUserMain");
                first_frame_trace = 1;
            }
#endif
            if (njUserMain() < NJD_USER_CONTINUE)
            {
                break;
            }
#ifdef TARGET_PC
            if (first_frame_trace == 1)
            {
                pcOsStartupLog("first njUserMain returned; entering njWaitVSync");
                first_frame_trace = 2;
            }
#endif
            njWaitVSync();
#ifdef TARGET_PC
            if (first_frame_trace == 2)
            {
                pcOsStartupLog("first njWaitVSync returned");
                first_frame_trace = 3;
            }
#endif
        }
    }

#ifdef TARGET_PC
    pcOsStartupLog("game loop exited; entering njUserExit");
#endif
    njUserExit();
#ifdef TARGET_PC
    pcOsStartupLog("njUserExit returned");
#endif
    return 0;
}
