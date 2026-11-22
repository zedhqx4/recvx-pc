#ifndef _PS2_LOADTIM2_H_
#define _PS2_LOADTIM2_H_

#include "types.h"

typedef struct TIM2_FILEHEADER
{
    // total size: 0x10
    char FileId[4];              // offset 0x0, size 0x4
    unsigned char FormatVersion; // offset 0x4, size 0x1
    unsigned char FormatId;      // offset 0x5, size 0x1
    unsigned short Pictures;     // offset 0x6, size 0x2
    char Reserved[8];            // offset 0x8, size 0x8
} TIM2_FILEHEADER;

u_long128* MakeRenderTexHeader(void* tex_adr);
void StoreRenderTex(void* tex_adr);
void *UncAddr(void *val);
int Send_1024_Clut_data(void* clt_adr, unsigned long send_clut_adr);
void Ps2PxlconvCheck(void* timadr);

#endif
