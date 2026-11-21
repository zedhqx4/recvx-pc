#include "../../../ps2/veronica/prog/ps2_Vu1Scissor2.h"
#include "../../../ps2/veronica/prog/ps2_Vu1Strip.h"
#include "pc_clip.h"
#include "pc_scissor.h"
#include "pc_platform.h"

extern void VU0_CLIP_VIEW_VOLUME() __attribute__((section(".vutext")));
extern void VU0_CLIP_VIEW_VOLUME_ALL() __attribute__((section(".vutext")));
extern void VU0_LOAD_SCISSOR_WORK() __attribute__((section(".vutext")));
extern void VU0_LOAD_SCISSOR_WORKi() __attribute__((section(".vutext")));
extern void VU0_LOAD_SCISSOR_WORKb() __attribute__((section(".vutext")));
extern void VU0_SET_NODE_ARRAY() __attribute__((section(".vutext")));
extern void VU0_STORE_SCISSOR_WORK() __attribute__((section(".vutext")));

// 98.80% matching
void DrawScissorPolygonOpaque2(int count, unsigned long ulType)
{
    VU1_PRIM_BUF* pPrim; 

    pcScissorEmit(vu1ScessorBuf, sizeof(VU1_PRIM_BUF), count);

    ulType = (ulType & ~0x2000000000000) | 0x6800000000000;
    
    pPrim = vu1ScessorBuf;
    
    Ps2AddPrim3DEx(ulType, pPrim, count);
}

// PC: C plano. Pone a cero el historial de clip (vf15/vf18/vf19 = vf0 en la VU)
void InitNodeArraySet2()
{
    pcClipResetHistory();
}

// PC: C plano. Manda el vertice a clip (historial de tres vertices y 3 flags); devuelve 0 como el original.
unsigned int _Clip_ViewVolume2(NJS_POINT4* vec)
{
    pcClipViewVolume(&vec->x, 0);
    return 0;
}

// PC: C plano. Flags de clip (vi18) del ultimo vertice enviado
unsigned int _Get_ClipViewVolume2()
{
    return pcClipFlagsGet();
}

// PC: C plano. vi2 del micro 237: 0 si algun plano descarta el triangulo, 1 si no
int _Get_ClipVolumePlane()
{
    return pcClipRegs.vi2;
}

// PC: C plano. Igual que _Clip_ViewVolume2 pero con vi1/vi2 (micro 237)
void _Check_ClipViewAll(NJS_POINT4* vec)
{
    pcClipViewVolume(&vec->x, 1);
}

// 96% matching
void _Set_NodeArray(VU1_STRIP_BUF* pS, VU1_PRIM_BUF* pP)
{
    pcScissorSetNode(pS, pP, pcClipRegs.h19, pcClipRegs.h18, pcClipRegs.h15);
}

// TODO: Verify that this function hasn't got more parts written in C, for example addiu and paddub tend to be compiler-emitted instructions
// 96.57% matching 
int _ClipInter(int mask1, int mask2, int xyzflg, float sin, int work0, int work1, int count)
{
    int ret;
    int pad[4]; // not from the debugging symbols

    ret = 0;

    (void)pad;
    ret = pcScissorClipPlane(mask1, mask2, xyzflg, sin, work0, work1, count);

    return ret;
}

// 100% matching!
int _Check_ScissorPlane()
{
    int count;

    count = _ClipInter(2048, 32, 2, -1.0f, 80, 128, 3);
    
    if (count == 0) 
    {
        return count;
    }

    count = _ClipInter(1024, 16, 2, 1.0f, 128, 80, count);

    if (count == 0) 
    {
        return count;
    }

    count = _ClipInter(128, 2, 0, -1.0f, 80, 128, count);
    
    if (count == 0) 
    {
        return count;
    }

    count = _ClipInter(64, 1, 0, 1.0f, 128, 80, count);
    
    if (count == 0) 
    {
        return count;
    }

    count = _ClipInter(512, 8, 1, -1.0f, 80, 128, count);
    
    if (count == 0) 
    {
        return count;
    }
    
    count = _ClipInter(256, 4, 1, 1.0f, 128, 80, count);

    return count;
}
