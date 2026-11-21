#include <stdlib.h>
#include "pc_vu.h"
#include "pc_platform.h"

int pcVuTruncEnabled(void)
{
    static int on = -1;
    if (on < 0)
    {
        const char* e = getenv("RECVX_VU_TRUNC");
        on = (e != NULL) ? (atoi(e) != 0) : (pcIniGetInt("vu_trunc", 1) != 0);
        pcLog("VU rounding: %s", on ? "truncate toward zero + flush denormals (EE/VU)" : "round to nearest (host default)");
    }
    return on;
}
