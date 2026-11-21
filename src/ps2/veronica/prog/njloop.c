#include "../../../ps2/veronica/prog/njloop.h"

#include "../../../../pc/src/pc_os.h"

// 100% matching!
int main(int argc, char *argv[])
{
    /* Install diagnostics before the PS2 initialization path: failures before SDL/window setup
       previously happened before the crash handler and could leave recvx.log empty. */
    pcOsStartupLog("main entered");
    pcOsInstallDiagnostics();
    pcOsStartupLog("diagnostics installed; entering njUserInit");
    njUserInit();

    pcOsStartupLog("njUserInit returned");

    {
        int first_frame_trace = 0;
        while (TRUE)
        {
            if (first_frame_trace == 0)
            {
                pcOsStartupLog("entering first njUserMain");
                first_frame_trace = 1;
            }
            if (njUserMain() < NJD_USER_CONTINUE)
            {
                break;
            }
            if (first_frame_trace == 1)
            {
                pcOsStartupLog("first njUserMain returned; entering njWaitVSync");
                first_frame_trace = 2;
            }
            njWaitVSync();
            if (first_frame_trace == 2)
            {
                pcOsStartupLog("first njWaitVSync returned");
                first_frame_trace = 3;
            }
        }
    }

    pcOsStartupLog("game loop exited; entering njUserExit");
    njUserExit();
    pcOsStartupLog("njUserExit returned");
    return 0;
}
