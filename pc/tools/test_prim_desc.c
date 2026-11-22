/* Comprueba pc_prim.h contra las macros GS originales: los 2048 valores del campo PRIM (topologia 0..7 x 8 bits de opcion)
 * y las expresiones que usaban los productores. Compilar: gcc -I pc/include -I include/ps2/veronica/prog pc/tools/test_prim_desc.c */
#include <stdio.h>
#include <stdint.h>
typedef uint64_t u_long64_t;
#define GSL(x) ((u_long64_t)(x))
#define SCE_GS_SET_PRIM(prim, iip, tme, fge, abe, aa1, fst, ctxt, fix) \
    (GSL(prim) | (GSL(iip) << 3) | (GSL(tme) << 4) | (GSL(fge) << 5) | (GSL(abe) << 6) | \
     (GSL(aa1) << 7) | (GSL(fst) << 8) | (GSL(ctxt) << 9) | (GSL(fix) << 10))
#define SCE_GIF_SET_TAG(nloop, eop, pre, prim, flg, nreg) \
    (GSL(nloop) | (GSL(eop) << 15) | (GSL(pre) << 46) | (GSL(prim) << 47) | (GSL(flg) << 58) | (GSL(nreg) << 60))
#define SCE_GIF_PRIM(prim, iip, tme, fge, abe, aa1, fst, ctxt, fix) SCE_GIF_SET_TAG(0, 0, 0, SCE_GS_SET_PRIM(prim, iip, tme, fge, abe, aa1, fst, ctxt, fix), 0, 0)
#include "pc_prim.h"

static int bad;
#define CHECK(c) do { if (!(c)) { bad++; printf("fallo: %s\n", #c); } } while (0)

int main(void)
{
    unsigned int v, n = 0;
    for (v = 0; v < 2048; v++) {
        unsigned int t = v & 7, iip = (v >> 3) & 1, tme = (v >> 4) & 1, fge = (v >> 5) & 1, abe = (v >> 6) & 1, aa1 = (v >> 7) & 1,
                     fst = (v >> 8) & 1, ctxt = (v >> 9) & 1, fix = (v >> 10) & 1;
        u_long64_t prim = SCE_GIF_PRIM(t, iip, tme, fge, abe, aa1, fst, ctxt, fix);
        u_long64_t rebuilt = PC_PRIM_SET_TOPOLOGY(t) | (iip ? PC_PRIM_IIP : 0) | (tme ? PC_PRIM_TME : 0) | (fge ? PC_PRIM_FGE : 0) |
                             (abe ? PC_PRIM_ABE : 0) | (aa1 ? PC_PRIM_AA1 : 0) | (fst ? PC_PRIM_FST : 0) | (ctxt ? PC_PRIM_CTXT : 0) | (fix ? PC_PRIM_FIX : 0);
        n++;
        CHECK(PC_PRIM_TOPOLOGY(prim) == t);
        CHECK(((prim & PC_PRIM_IIP) != 0) == (iip != 0));
        CHECK(((prim & PC_PRIM_TME) != 0) == (tme != 0));
        CHECK(((prim & PC_PRIM_FGE) != 0) == (fge != 0));
        CHECK(((prim & PC_PRIM_ABE) != 0) == (abe != 0));
        CHECK(((prim & PC_PRIM_AA1) != 0) == (aa1 != 0));
        CHECK(((prim & PC_PRIM_FST) != 0) == (fst != 0));
        CHECK(((prim & PC_PRIM_CTXT) != 0) == (ctxt != 0));
        CHECK(((prim & PC_PRIM_FIX) != 0) == (fix != 0));
        CHECK(rebuilt == prim);
    }
    /* literales que usaba el codigo de consumo */
    CHECK(PC_PRIM_TME == 0x8000000000000ULL);
    CHECK(PC_PRIM_ABE == 0x20000000000000ULL);
    CHECK(PC_PRIM_FST == 0x80000000000000ULL);
    CHECK(PC_PRIM_FGE == 0x10000000000000ULL);
    /* productores */
    CHECK((PC_PRIM_SET_TOPOLOGY(PC_PRIM_TRIANGLE_STRIP) | PC_PRIM_IIP | PC_PRIM_TME | PC_PRIM_FST) ==
          SCE_GIF_SET_TAG(0, 0, 0, SCE_GS_SET_PRIM(4, 1, 1, 0, 0, 0, 1, 0, 0), 0, 0));
    CHECK(PC_PRIM_ABE == SCE_GIF_SET_TAG(0, 0, 0, SCE_GS_SET_PRIM(0, 0, 0, 0, 1, 0, 0, 0, 0), 0, 0));
    CHECK((PC_PRIM_SET_TOPOLOGY(PC_PRIM_TRIANGLE) | PC_PRIM_ABE | PC_PRIM_CTXT) ==
          SCE_GIF_SET_TAG(0, 0, 0, SCE_GS_SET_PRIM(3, 0, 0, 0, 1, 0, 0, 1, 0), 0, 0));
    CHECK((PC_PRIM_SET_TOPOLOGY(PC_PRIM_TRIANGLE_STRIP) | PC_PRIM_TME | PC_PRIM_FGE) == SCE_GIF_PRIM(4, 0, 1, 1, 0, 0, 0, 0, 0));
    CHECK((PC_PRIM_SET_TOPOLOGY(PC_PRIM_TRIANGLE_FAN) | PC_PRIM_TME | PC_PRIM_FGE) == SCE_GIF_PRIM(5, 0, 1, 1, 0, 0, 0, 0, 0));
    CHECK(PC_PRIM_IIP == SCE_GIF_PRIM(0, 1, 0, 0, 0, 0, 0, 0, 0));
    printf("%u valores PRIM, %d fallos\n", n, bad);
    return bad != 0;
}
