#include <stdlib.h>
#include "pc_vu.h"
#include "pc_platform.h"

/* Registros VU0 que aun usa el codigo COP2 traducido (ps2_Vu1Strip.c, ps2_NaMatrix.c). Se retira cuando ese codigo
 * pase a variables propias. */
PcVu0 pcVu0 = { { { { 0, 0, 0, 1 } } } };

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
