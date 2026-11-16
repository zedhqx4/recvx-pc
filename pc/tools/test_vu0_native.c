/* Prueba de equivalencia nativo vs interprete de los microprogramas VU0 237/269/394 (11 modos de color).
 * Estado aleatorio con denormales, inf y ceros. Ignora solo el signo de cero (el interprete usa fmaxf/fminf).
 * gcc -std=gnu99 -O1 -ffp-contract=off -msse2 -Ipc/include -include stdint.h pc/tools/test_vu0_native.c \
 *     pc/src/vu/vu0_micro.c pc/src/vu/vu0_native.c <stub con pcIniGetInt/pcLog y pc_platform.h> -lm
 * Para probar la truncacion, descomentar la linea de _mm_setcsr en main. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "pc_vu.h"
void pcLog(const char* f, ...) { va_list a; va_start(a, f); vprintf(f, a); printf("\n"); va_end(a); }
void pcVu0MicroInterp(int addr);
int pcVu0NativeMicro(int addr);
void pcVu0Micro(int addr);
static unsigned rs = 12345;
static unsigned rnd(void) { rs = rs * 1664525u + 1013904223u; return rs >> 8; }
static float rf(void)
{
    switch (rnd() % 12) {
    case 0: return 0.0f; case 1: return 1.0f; case 2: return -1.0f;
    case 3: return 1e-40f;           /* denormal */
    case 4: return 3.0e38f * 10.0f;  /* inf */
    case 5: return (float)(rnd() % 1000) / 7.0f;
    case 6: return -(float)(rnd() % 1000) / 13.0f;
    default: return ((float)(rnd() % 20001) - 10000.0f) / 5000.0f;
    }
}
int main(void)
{
    int bad = 0, n = 0; /* _mm_setcsr(_mm_getcsr()|0x6000|0x8040); */
    int addrs[3] = {237, 269, 394};
    int iter, a, m;
    for (iter = 0; iter < 200000; iter++) {
        static PcVu0 s0, nat, itp;
        int mode = rnd() % 11;
        a = addrs[rnd() % 3];
        /* estado aleatorio */
        for (int r = 1; r < 32; r++) for (int c = 0; c < 4; c++) pcVu0.vf[r].f[c] = rf();
        pcVu0.vf[0].f[0]=0;pcVu0.vf[0].f[1]=0;pcVu0.vf[0].f[2]=0;pcVu0.vf[0].f[3]=1;
        pcVu0.vi[0]=0; pcVu0.clip = rnd() & 0xFFFFFF; pcVu0.cycle = rnd() % 1000;
        /* inicializa tabla via interprete (INIT 365 = entrada con vi01 = modo) */
        pcVu0.vi[1] = mode;
        pcVu0MicroInterp(365);
        /* re-randomiza lo que INIT pudo tocar de vf15/18/19 y regs de datos */
        for (int r = 3; r < 32; r++) if (r!=24&&r!=25&&r!=26&&r!=27&&r!=14&&r!=4&&r!=5&&r!=20&&r!=21&&r!=22&&r!=23&&r!=18&&r!=15&&r!=19&&r!=1&&r!=3) {}
        { static const int rr[16]={1,3,4,5,14,15,18,19,20,21,22,23,24,25,26,27}; for (int q=0;q<16;q++) for (int c = 0; c < 4; c++) pcVu0.vf[rr[q]].f[c] = rf(); }
        pcVu0.vf[0].f[3]=1; pcVu0.vf[0].f[0]=0;pcVu0.vf[0].f[1]=0;pcVu0.vf[0].f[2]=0;
        memcpy(&s0, &pcVu0, sizeof s0);
        if (!pcVu0NativeMicro(a)) { printf("native refused %d\n", a); return 2; }
        memcpy(&nat, &pcVu0, sizeof nat);
        memcpy(&pcVu0, &s0, sizeof s0);
        pcVu0MicroInterp(a);
        memcpy(&itp, &pcVu0, sizeof itp);
        n++;
        int diff = 0;
        for (int r = 0; r < 32; r++) for (int c = 0; c < 4; c++) if (nat.vf[r].u[c] != itp.vf[r].u[c] && !(((nat.vf[r].u[c]|itp.vf[r].u[c])&0x7fffffffu)==0)) {
            /* NaN: comparar como floats */
            if (nat.vf[r].f[c] == nat.vf[r].f[c] || itp.vf[r].f[c] == itp.vf[r].f[c]) { if (bad<15) printf("it %d micro %d mode %d: vf%d.%d nat %08x itp %08x\n", iter,a,mode,r,c,nat.vf[r].u[c],itp.vf[r].u[c]); diff=1; } }
        for (int c = 0; c < 4; c++) if (nat.acc.u[c] != itp.acc.u[c]) { if(bad<15) printf("it %d micro %d mode %d: acc.%d nat %08x itp %08x\n", iter,a,mode,c,nat.acc.u[c],itp.acc.u[c]); diff=1; }
        for (int r = 1; r < 16; r++) if (nat.vi[r] != itp.vi[r]) { if(bad<15) printf("it %d micro %d mode %d: vi%d nat %d itp %d\n", iter,a,mode,r,nat.vi[r],itp.vi[r]); diff=1; }
        if (nat.clip != itp.clip) { if(bad<15) printf("it %d micro %d mode %d: clip nat %06x itp %06x\n", iter,a,mode,nat.clip,itp.clip); diff=1; }
        if (nat.i != itp.i) { if(bad<15) printf("it %d micro %d mode %d: I nat %f itp %f\n", iter,a,mode,nat.i,itp.i); diff=1; }
        bad += diff;
    }
    printf("%d casos, %d con diferencias\n", n, bad);
    return bad != 0;
}
