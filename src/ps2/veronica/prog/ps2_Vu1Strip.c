#include "../../../ps2/veronica/prog/ps2_Vu1Strip.h"
#include "../../../ps2/veronica/prog/ps2_NaFog.h"
#include "../../../ps2/veronica/prog/ps2_NaView.h"
#include "../../../ps2/veronica/prog/ps2_dummy.h"
#include "../../../ps2/veronica/prog/main.h"
#ifdef TARGET_PC
#include <math.h>
#include <stdlib.h>
#include <string.h>
#endif


#ifdef TARGET_PC
/* Perfil opt-in (debug=1, profile_game=1): reparto del tiempo de cada tira entre color/recorte por vertice y el envio
 * (Ps2AddPrim3DEx). Cada 4000 tiras escribe una linea "PROFILE vu1Draw" en recvx.log. */
#include "pc_game_profile.h"
#include "pc_platform.h"
#include "pc_vu.h"
#include "pc_clip.h"
#include "pc_scissor.h"
#include "pc_tri_scissor.h"
static struct { double total, submit; unsigned int strips, verts; } pcsp_acc[4];
static unsigned int pcsp_count;
static void pcStripProfRecord(int kind, double t0, double t1, double t2, unsigned int verts)
{
    static const char* const names[4] = { "OpaqueSingle", "OpaqueDouble", "TransSingle", "TransDouble" };
    pcsp_acc[kind].total += t2 - t0;
    pcsp_acc[kind].submit += t2 - t1;
    pcsp_acc[kind].strips++;
    pcsp_acc[kind].verts += verts;
    if (++pcsp_count >= 4000)
    {
        int i;
        for (i = 0; i < 4; i++)
        {
            if (pcsp_acc[i].strips == 0) continue;
            pcLog("PROFILE vu1Draw %s: %u strips %u verts | %.3f us/vertex total, %.3f us/vertex submit (Ps2AddPrim3DEx), %.3f us/vertex color+clip+rest",
                  names[i], pcsp_acc[i].strips, pcsp_acc[i].verts,
                  pcsp_acc[i].total * 1000.0 / pcsp_acc[i].verts, pcsp_acc[i].submit * 1000.0 / pcsp_acc[i].verts,
                  (pcsp_acc[i].total - pcsp_acc[i].submit) * 1000.0 / pcsp_acc[i].verts);
            memset(&pcsp_acc[i], 0, sizeof(pcsp_acc[i]));
        }
        pcsp_count = 0;
    }
}
#define PCSP_BEGIN unsigned int pcsp_csr = pcVuRoundEnter(); double pcsp_t0 = pcGameProfileEnabled() ? pcGameProfileNowMs() : 0.0, pcsp_t1 = 0.0;
#define PCSP_SUBMIT(kind, call) do { pcVuRoundLeave(pcsp_csr); if (pcsp_t0 != 0.0) pcsp_t1 = pcGameProfileNowMs(); call; \
    if (pcsp_t0 != 0.0) pcStripProfRecord(kind, pcsp_t0, pcsp_t1, pcGameProfileNowMs(), usStripMax); } while (0)
#else
#define PCSP_BEGIN
#define PCSP_SUBMIT(kind, call) call
#endif

static SCISSOR_PLANE planeset;
static SCISSOR scissorflip;
static NODE node;
float fVu1Projection;
float fVu1NearClip;
float fVu1InvNearClip;
float fVu1FarClip;
float fVu1OffsetX;
float fVu1OffsetY;
float fVu1AspectW;
float fVu1AspectH;
VU1_PRIM_BUF vu1ScessorBuf[16];
#ifndef TARGET_PC
void (*pColorCalcFuncTbl[11])(VU1_STRIP_BUF* pStrip, VU1_PRIM_BUF* pPrim) = 
{
	vu1GetVertexColor,
	vu1GetVertexColorCM,
	vu1GetVertexColorIgnore,
	vu1GetVertexColorDif,
	vu1GetVertexColorDifAmb,
	vu1GetVertexColorDifSpe1,
	vu1GetVertexColorDifSpe2,
	vu1GetVertexColorDifSpe3,
	vu1GetVertexColorDifSpe1Amb,
	vu1GetVertexColorDifSpe2Amb,
	vu1GetVertexColorDifSpe3Amb
};
#endif

VU1_COLOR vu1Diffuse = { 1.0f, 1.0f, 1.0f, 1.0f };
VU1_COLOR vu1Specula = { 1.0f, 1.0f, 1.0f, 1.0f };
VU1_COLOR vu1Ambient = { 1.0f, 1.0f, 1.0f, 1.0f };
float fVu1AlphaRatio = 128.0f;

#ifdef TARGET_PC
/* Color por vertice sin la unidad vectorial: pc/include/pc_vu1_color.h reproduce las 11 vu1GetVertexColor* como C escalar
 * (verificado bit a bit contra las funciones transpiladas, ver milestone/0.9.160 y claude_notes.md). */
#include "pc_vu1_color.h"

#define PCVC_IN(s) (&(s)->fIr)
#define PCVC_OUT(p) (&(p)->fR)
static void pcvcn_0(VU1_STRIP_BUF* s, VU1_PRIM_BUF* p) { pcvc_0(PCVC_IN(s), PCVC_OUT(p)); }
static void pcvcn_1(VU1_STRIP_BUF* s, VU1_PRIM_BUF* p) { (void)s; pcvc_1(&vu1Diffuse.fR, fVu1AlphaRatio, PCVC_OUT(p)); }
static void pcvcn_2(VU1_STRIP_BUF* s, VU1_PRIM_BUF* p) { (void)s; pcvc_2(fVu1AlphaRatio, PCVC_OUT(p)); }
static void pcvcn_3(VU1_STRIP_BUF* s, VU1_PRIM_BUF* p) { pcvc_3(PCVC_IN(s), &vu1Diffuse.fR, fVu1AlphaRatio, PCVC_OUT(p)); }
static void pcvcn_4(VU1_STRIP_BUF* s, VU1_PRIM_BUF* p) { pcvc_4(PCVC_IN(s), &vu1Ambient.fR, &vu1Diffuse.fR, fVu1AlphaRatio, PCVC_OUT(p)); }
static void pcvcn_5(VU1_STRIP_BUF* s, VU1_PRIM_BUF* p) { pcvc_5(PCVC_IN(s), fVu1AlphaRatio, PCVC_OUT(p)); }
static void pcvcn_6(VU1_STRIP_BUF* s, VU1_PRIM_BUF* p) { pcvc_6(PCVC_IN(s), &vu1Ambient.fR, &vu1Diffuse.fR, &vu1Specula.fR, fVu1AlphaRatio, PCVC_OUT(p)); }
static void pcvcn_7(VU1_STRIP_BUF* s, VU1_PRIM_BUF* p) { pcvc_7(PCVC_IN(s), &vu1Diffuse.fR, &vu1Specula.fR, fVu1AlphaRatio, PCVC_OUT(p)); }
static void pcvcn_8(VU1_STRIP_BUF* s, VU1_PRIM_BUF* p) { pcvc_8(PCVC_IN(s), &vu1Ambient.fR, &vu1Diffuse.fR, &vu1Specula.fR, fVu1AlphaRatio, PCVC_OUT(p)); }
static void pcvcn_9(VU1_STRIP_BUF* s, VU1_PRIM_BUF* p) { pcvc_9(PCVC_IN(s), &vu1Ambient.fR, &vu1Diffuse.fR, &vu1Specula.fR, fVu1AlphaRatio, PCVC_OUT(p)); }
static void pcvcn_10(VU1_STRIP_BUF* s, VU1_PRIM_BUF* p) { pcvc_10(PCVC_IN(s), &vu1Ambient.fR, &vu1Diffuse.fR, &vu1Specula.fR, fVu1AlphaRatio, PCVC_OUT(p)); }

/* Tabla de las 11 funciones de color por vertice, indexada por usMode & 0xF (como en el original). */
void (*pColorCalcFuncTbl[11])(VU1_STRIP_BUF* pStrip, VU1_PRIM_BUF* pPrim) = {
    pcvcn_0, pcvcn_1, pcvcn_2, pcvcn_3, pcvcn_4, pcvcn_5, pcvcn_6, pcvcn_7, pcvcn_8, pcvcn_9, pcvcn_10
};
#define pcColorCalcFunc(k) pColorCalcFuncTbl[(k)]
#else
#define pcColorCalcFunc(k) pColorCalcFuncTbl[(k)]
#endif

extern void VU0_CLIP_VIEW_VOLUME_ALL() __attribute__((section(".vutext")));
extern void VU0_CALC_COLOR() __attribute__((section(".vutext")));
extern void VU0_INIT_CALC_COLOR() __attribute__((section(".vutext")));

// PC: los registros VU0 que escribia (vf23) ya no se leen; la proyeccion queda en pcScreen()
void vu1SetScreenProjection(float fProjection)
{
    fVu1Projection = fProjection;
    pcScreenSetProjection(fProjection);

    _Make_ClipMatrix(ClipMatrix2, fVu1Projection, _fNaViwClipNear, _fNaViwClipFar);
}

// PC: vf23.x/y (near/far) ya no los lee nadie
void vu1SetNearFarClip(float fNear, float fFar)
{
    fVu1NearClip = fNear;
    fVu1FarClip = fFar;

    fVu1InvNearClip = 1.0f / fVu1NearClip;

    _Make_ClipMatrix(ClipMatrix2, fVu1Projection, _fNaViwClipNear, _fNaViwClipFar);
}

// PC: el offset queda en pcScreen() (antes vf16)
void vu1SetScreenOffset(float fOffsetX, float fOffsetY)
{
    fVu1OffsetX = fOffsetX;
    fVu1OffsetY = fOffsetY;
    pcScreenSetOffset(fOffsetX, fOffsetY);
}

// PC: el aspecto queda en pcScreen() (antes vf17)
void vu1SetScreenAspect(float fAspectW, float fAspectH)
{
    fVu1AspectW = fAspectW;
    fVu1AspectH = fAspectH;
    pcScreenSetAspect(fAspectW, fAspectH);
}

// PC: el color lo calcula pc_clip.c con estos materiales (vf20 ya no se lee)
void vu1SetDiffuseMaterial(VU1_COLOR* pDiffuse)
{
    vu1Diffuse.fR = 128.0f * pDiffuse->fR;
    vu1Diffuse.fG = 128.0f * pDiffuse->fG;
    vu1Diffuse.fB = 128.0f * pDiffuse->fB;
    pcColorSetDiffuse(&pDiffuse->fR);
}

// PC: el color lo calcula pc_clip.c con estos materiales (vf21 ya no se lee)
void vu1SetSpeculaMaterial(VU1_COLOR* pSpecula)
{
    vu1Specula.fR = 31.0f * pSpecula->fR;
    vu1Specula.fG = 31.0f * pSpecula->fG;
    vu1Specula.fB = 31.0f * pSpecula->fB;
    pcColorSetSpecula(&pSpecula->fR);
}

// PC: vf22 ya no se lee
void vu1SetAmbient(VU1_COLOR* pAmbient)
{
    vu1Ambient.fR = pAmbient->fR;
    vu1Ambient.fG = pAmbient->fG;
    vu1Ambient.fB = pAmbient->fB;
    pcColorSetAmbient(&pAmbient->fR);
}

// PC: vf23.w ya no se lee
void vu1SetAlphaRatio(float fAlpha)
{
    fVu1AlphaRatio = 128.0f * fAlpha;
    pcColorSetAlphaRatio(fVu1AlphaRatio);
}

#include "ps2_Vu1Scissor2.c"

// PC: C plano. El triangulo de entrada tiene 3 nodos; narray[0] es la entrada y narray[1] la salida, ambos vacios.
void InitNodeArraySet(SCISSOR* scissor)
{
    scissor->rotflag = 0;
    scissor->flipflag = 0;
    scissor->in = &scissor->narray[0];
    scissor->out = &scissor->narray[1];
    scissor->narray[0].nodeNum = 0;
    scissor->narray[1].nodeNum = 0;
    scissor->triangle.nodeNum = 3;
}

// 100% matching!
static void InitScissorPlane(SCISSOR_PLANE* sp)
{
    sp->planeNum = 5;
    
    sp->plane[0].clipmask = 0x820;
    sp->plane[0].xyzflag = 0x12;
    
    sp->plane[1].clipmask = 0x208;
    sp->plane[1].xyzflag = 0x11;
    
    sp->plane[2].clipmask = 0x104;
    sp->plane[2].xyzflag = 0x1;
    
    sp->plane[3].clipmask = 0x82;
    sp->plane[3].xyzflag = 0x10;
    
    sp->plane[4].clipmask = 0x41;
    sp->plane[4].xyzflag = 0;
}

// 100% matching!
void _Init_ScissorSystem()
{
    InitScissorPlane(&planeset);
    InitNodeArraySet(&scissorflip);
}

// PC: C plano (pcClipViewVertex)
int _Clip_ViewVolume(float* clip, float local_clip[4], float* vertex)
{
    return pcClipViewVertex(clip, vertex);
}

// PC: C plano. Copia el nodo a triangle.node[rotflag] y avanza rotflag en 0,1,2 (el asm usaba el NODE estatico `node`,
// que es lo unico que se le pasa).
void PushTriangleNodeArray(SCISSOR* scissor, NODE* nod)
{
    unsigned int next = (scissor->rotflag + 1) & 0x3;

    scissor->triangle.node[scissor->rotflag] = *nod;
    scissor->rotflag = (next >= 3) ? 0 : next;
}

// PC: C plano
void ResetNodeArraySet(SCISSOR* scissor)
{
    scissor->flipflag = 0;
    scissor->in = &scissor->narray[0];
    scissor->out = &scissor->narray[1];
    scissor->narray[0].nodeNum = 0;
    scissor->narray[1].nodeNum = 0;
}

// PC: C plano sobre pcTriScissorPlane (el original eran ~780 lineas de VU0 transpilado)
void ScissorTriangle(SCISSOR* scissor, SCISSOR_PLANE* plane_set)
{
    /* Recorta el triangulo contra cada plano con pcTriScissorPlane; la salida de un plano es la entrada del siguiente
     * (los dos arreglos de narray se alternan con flipflag). El historial de flags de clip (vi18) lo actualiza igual. */
    SCISSOR_NODE* inarray = &scissor->triangle;
    SCISSOR_NODE* outarray = scissor->out;
    uint32_t hist = pcClipFlagsGet();
    unsigned int i;

    for (i = 0; i < plane_set->planeNum; i++)
    {
        const unsigned int xyzflag = plane_set->plane[i].xyzflag;
        const unsigned int flip = scissor->flipflag;

        outarray->nodeNum += (unsigned int)pcTriScissorPlane((const PcTriNode*)inarray->node, (int)inarray->nodeNum,
            (PcTriNode*)&outarray->node[outarray->nodeNum], (xyzflag & 16) ? -1.0f : 1.0f,
            (int)plane_set->plane[i].clipmask, (int)(xyzflag & 0xF), &hist);

        scissor->flipflag = (flip + 1) & 1;
        scissor->out = &scissor->narray[flip];
        scissor->in = &scissor->narray[scissor->flipflag];
        scissor->out->nodeNum = 0;

        inarray = scissor->in;
        outarray = scissor->out;
    }

    pcClipFlagsSet(hist);
}


// 100% matching!
int _Check_DisplayAreaPoint(NJS_VECTOR* vec)
{
    if (((vec->x >= 1.0f) && (vec->x < 4095.0f)) && ((vec->y >= 1.0f) && (vec->y < 4095.0f)) && ((vec->z >= 1.0f) && (vec->z < 65535.0f))) 
    {
        return 0;
    }
    
    return 1;
}

// PC: C plano. Proyecta los nodos del poligono recortado (pcScissorProject) y los manda como un abanico.
void DrawScissorPolygonOpaque(SCISSOR* scissor, unsigned long ulType)
{
    SCISSOR_NODE* in = scissor->in;

    pcScissorProject(vu1ScessorBuf, sizeof(VU1_PRIM_BUF), (int)in->nodeNum, in->node, sizeof(NODE),
                     offsetof(NODE, vertex), offsetof(NODE, texUV), offsetof(NODE, color), 1);
    ulType = (ulType & 0xFFFDFFFFFFFFFFFF) | 0x6800000000000;
    Ps2AddPrim3DEx(ulType, vu1ScessorBuf, in->nodeNum);
}

// 100% matching! 
void vu1DrawTriangleStripOpaqueSingle(unsigned long ulType, VU1_STRIP_BUF* pStripTop, unsigned short usStripMax, unsigned short usMode)
{
    PCSP_BEGIN
    VU1_PRIM_BUF* pP;                            
    VU1_STRIP_BUF* pS0, *pS1, *pS2;                         
    float fIz;                                   
    unsigned short usStripCnt;                  
    void (*pFunc)(VU1_STRIP_BUF*, VU1_PRIM_BUF*); 
    unsigned int clipflag;                     
    int flg;                                    
    int count;                                

    pS0 = pStripTop; 
    pS1 = &pS0[1];
    pS2 = &pS1[1];
    
    InitNodeArraySet2();
    
    pP = (VU1_PRIM_BUF*)Ps2_DRAW_TMP;
    
    pFunc = pcColorCalcFunc((usMode & 0xFFFF) & 0xF);
    
    pFunc(pS0, pP);
    
    _Clip_ViewVolume2((NJS_POINT4*)&pS0->fVx);
    
    fIz = pS0->fIz;
   
    pP->fS = pS0->fU * fIz;
    pP->fT = pS0->fV * fIz;
    pP->fQ = fIz;
    
    pP->ulKick = 0;
    
    pP->fX = fVu1OffsetX + pS0->fSx;
    pP->fY = fVu1OffsetY + pS0->fSy;
    pP->fZ = pS0->fVz;
    
    pP->fF = pS0->fFog;
    
    flg = _Check_DisplayAreaPoint((NJS_VECTOR*)&pP->fX);
    
    flg <<= 1;
    
    pP++;
    
    pFunc(pS1, pP);
    
    _Clip_ViewVolume2((NJS_POINT4*)&pS1->fVx);
    
    fIz = pS1->fIz;
    
    pP->fS = pS1->fU * fIz;
    pP->fT = pS1->fV * fIz;
    pP->fQ = fIz;
    
    pP->ulKick = 0;
    
    pP->fX = fVu1OffsetX + pS1->fSx;
    pP->fY = fVu1OffsetY + pS1->fSy;
    pP->fZ = pS1->fVz;
    
    pP->fF = pS1->fFog;
    
    flg |= _Check_DisplayAreaPoint((NJS_VECTOR*)&pP->fX);
    
    flg <<= 1;
    
    pP++; 
    
    for (usStripCnt = 2; usStripCnt < usStripMax; usStripCnt++, pS0++, pS1++, pS2++, pP++) 
    {
        pFunc(pS2, pP);
        
        _Check_ClipViewAll((NJS_POINT4*)&pS2->fVx);
        
        fIz = pS2->fIz;
        
        pP->fS = pS2->fU * fIz;
        pP->fT = pS2->fV * fIz;
        pP->fQ = fIz;
        
        pP->ulKick = 0;
        
        pP->fX = fVu1OffsetX + pS2->fSx;
        pP->fY = fVu1OffsetY + pS2->fSy;
        pP->fZ = pS2->fVz;
        
        pP->fF = pS2->fFog;
        
        flg |= _Check_DisplayAreaPoint((NJS_VECTOR*)&pP->fX);
        
        clipflag = _Get_ClipViewVolume2();
        
        if ((0 < pS0->fNz) && (0 < pS1->fNz) && (0 < pS2->fNz))  
        {
            pP->ulKick = 32768;
        }
        else if (((flg & 0x7)) && ((clipflag & 0x3FFFF)))
        {
            pP->ulKick = 32768;
            
            if (_Get_ClipVolumePlane() != 0) 
            {
                _Set_NodeArray(pS2, pP);
                
                count = _Check_ScissorPlane();
                
                if (count != 0)
                {
                    DrawScissorPolygonOpaque2(count, ulType);
                }
            }
        }
        
        flg <<= 1;
    }
    
    PCSP_SUBMIT(0, Ps2AddPrim3DEx(ulType, Ps2_DRAW_TMP, usStripMax));
}

// 100% matching! 
void vu1DrawTriangleStripOpaqueDouble(unsigned long ulType, VU1_STRIP_BUF* pS, unsigned short usStripMax, unsigned short usMode)
{
    PCSP_BEGIN
    VU1_PRIM_BUF* pP;                            
    float fIz;                                   
    unsigned short usStripCnt;                  
    void (*pFunc)(VU1_STRIP_BUF*, VU1_PRIM_BUF*); 
    unsigned int clipflag;                     
    unsigned int flg;                                    
    int count;   

    InitNodeArraySet2();
    
    pP = (VU1_PRIM_BUF*)Ps2_DRAW_TMP;
    
    pFunc = pcColorCalcFunc((usMode & 0xFFFF) & 0xF);
    
    pFunc(pS, pP);
    
    fIz = pS->fIz;
   
    pP->fS = pS->fU * fIz;
    pP->fT = pS->fV * fIz;
    pP->fQ = fIz;
    
    pP->ulKick = 0;

    _Clip_ViewVolume2((NJS_POINT4*)&pS->fVx);
    
    pP->fX = fVu1OffsetX + pS->fSx;
    pP->fY = fVu1OffsetY + pS->fSy;
    pP->fZ = pS->fVz;
    
    pP->fF = pS->fFog;
    
    flg = _Check_DisplayAreaPoint((NJS_VECTOR*)&pP->fX);
    
    flg <<= 1;
    
    pP++;
    pS++;
    
    pFunc(pS, pP);
    
    fIz = pS->fIz;
    
    pP->fS = pS->fU * fIz;
    pP->fT = pS->fV * fIz;
    pP->fQ = fIz;
    
    pP->ulKick = 0;

    _Clip_ViewVolume2((NJS_POINT4*)&pS->fVx);
    
    pP->fX = fVu1OffsetX + pS->fSx;
    pP->fY = fVu1OffsetY + pS->fSy;
    pP->fZ = pS->fVz;
    
    pP->fF = pS->fFog;
    
    flg |= _Check_DisplayAreaPoint((NJS_VECTOR*)&pP->fX);
    
    flg <<= 1;
    
    pP++; 
    pS++;
    
    for (usStripCnt = 2; usStripCnt < usStripMax; usStripCnt++, pS++, pP++) 
    {
        pFunc(pS, pP);
        
        fIz = pS->fIz;
        
        pP->fS = pS->fU * fIz;
        pP->fT = pS->fV * fIz;
        pP->fQ = fIz;
        
        pP->ulKick = 0;

        _Check_ClipViewAll((NJS_POINT4*)&pS->fVx);
        
        pP->fX = fVu1OffsetX + pS->fSx;
        pP->fY = fVu1OffsetY + pS->fSy;
        pP->fZ = pS->fVz;
        
        pP->fF = pS->fFog;
        
        flg |= _Check_DisplayAreaPoint((NJS_VECTOR*)&pP->fX);
        
        clipflag = _Get_ClipViewVolume2();
        
        if (((flg & 0x7)) && ((clipflag & 0x3FFFF)))
        {
            pP->ulKick = 32768;
            
            if (_Get_ClipVolumePlane() != 0) 
            {
                _Set_NodeArray(pS, pP);
                
                count = _Check_ScissorPlane();
                
                if (count != 0)
                {
                    DrawScissorPolygonOpaque2(count, ulType);
                }
            }
        }
        
        flg <<= 1;
    }
    
    PCSP_SUBMIT(1, Ps2AddPrim3DEx(ulType, Ps2_DRAW_TMP, usStripMax));
}

// 100% matching! 
void vu1DrawTriangleStripTransSingle(unsigned long ulType, VU1_STRIP_BUF* pStripTop, unsigned short usStripMax, unsigned short usMode)
{
    PCSP_BEGIN
    VU1_PRIM_BUF* pP;                            
    VU1_STRIP_BUF* pS0, *pS1, *pS2;                         
    float fIz;                                   
    unsigned short usStripCnt;                  
    void (*pFunc)(VU1_STRIP_BUF*, VU1_PRIM_BUF*); 
    unsigned int clipflag;                     
    int flg;                                    
    int count;                                

    pS0 = pStripTop; 
    pS1 = &pS0[1];
    pS2 = &pS1[1];
    
    InitNodeArraySet2();
    
    pP = (VU1_PRIM_BUF*)Ps2_DRAW_TMP;
    
    pFunc = pcColorCalcFunc((usMode & 0xFFFF) & 0xF);
    
    pFunc(pS0, pP);
    
    _Clip_ViewVolume2((NJS_POINT4*)&pS0->fVx);
    
    fIz = pS0->fIz;
   
    pP->fS = pS0->fU * fIz;
    pP->fT = pS0->fV * fIz;
    pP->fQ = fIz;
    
    pP->ulKick = 0;
    
    pP->fX = fVu1OffsetX + pS0->fSx;
    pP->fY = fVu1OffsetY + pS0->fSy;
    pP->fZ = pS0->fVz;
    
    pP->fF = pS0->fFog;
    
    flg = _Check_DisplayAreaPoint((NJS_VECTOR*)&pP->fX);
    
    flg <<= 1;
    
    pP++;
    
    pFunc(pS1, pP);
    
    _Clip_ViewVolume2((NJS_POINT4*)&pS1->fVx);
    
    fIz = pS1->fIz;
    
    pP->fS = pS1->fU * fIz;
    pP->fT = pS1->fV * fIz;
    pP->fQ = fIz;
    
    pP->ulKick = 0;
    
    pP->fX = fVu1OffsetX + pS1->fSx;
    pP->fY = fVu1OffsetY + pS1->fSy;
    pP->fZ = pS1->fVz;
    
    pP->fF = pS1->fFog;
    
    flg |= _Check_DisplayAreaPoint((NJS_VECTOR*)&pP->fX);
    
    flg <<= 1;
    
    pP++; 
    
    for (usStripCnt = 2; usStripCnt < usStripMax; usStripCnt++, pS0++, pS1++, pS2++, pP++) 
    {
        pFunc(pS2, pP);
        
        _Check_ClipViewAll((NJS_POINT4*)&pS2->fVx);
        
        fIz = pS2->fIz;
        
        pP->fS = pS2->fU * fIz;
        pP->fT = pS2->fV * fIz;
        pP->fQ = fIz;
        
        pP->ulKick = 0;
        
        pP->fX = fVu1OffsetX + pS2->fSx;
        pP->fY = fVu1OffsetY + pS2->fSy;
        pP->fZ = pS2->fVz;
        
        pP->fF = pS2->fFog;
        
        flg |= _Check_DisplayAreaPoint((NJS_VECTOR*)&pP->fX);
        
        clipflag = _Get_ClipViewVolume2();
        
        if ((0 < pS0->fNz) && (0 < pS1->fNz) && (0 < pS2->fNz))  
        {
            pP->ulKick = 32768;
        }
        else if (((flg & 0x7)) && ((clipflag & 0x3FFFF)))
        {
            pP->ulKick = 32768;
            
            if (_Get_ClipVolumePlane() != 0) 
            {
                _Set_NodeArray(pS2, pP);
                
                count = _Check_ScissorPlane();
                
                if (count != 0)
                {
                    DrawScissorPolygonOpaque2(count, ulType);
                }
            }
        }
        
        flg <<= 1;
    }
    
    PCSP_SUBMIT(2, Ps2AddPrim3DEx(ulType, Ps2_DRAW_TMP, usStripMax));
}

// PC: C plano (pcClipScreen)
int _Clip_Screen(float* clip)
{
    return pcClipScreen(clip);
}

// Un vertice de la tira: color y clip (pcClipColorVertex) y los datos que la GS espera en VU1_PRIM_BUF.
// kind 2 = solo el flag de clip del vertice nuevo (los dos primeros), 1 = ademas el historial de tres vertices.
static void StripPrimVertex(VU1_STRIP_BUF* pS, VU1_PRIM_BUF* pP, int kind)
{
    float color[4];

    memcpy(color, &pS->fIr, sizeof(color));
    pcClipColorVertex(&pS->fVx, kind, color);

    memcpy(&pP->fX, &pS->fVx, 16);              /* x, y, z, fog */
    pP->fS = pS->fU * pS->fIz;
    pP->fT = pS->fV * pS->fIz;
    pP->fQ = pS->fIz;
    pP->ulKick = 0;
    pP->fX = pS->fSx + fVu1OffsetX;
    pP->fY = pS->fSy + fVu1OffsetY;
    memcpy(&pP->fR, color, sizeof(color));
}

// PC: C plano. Los dos primeros vertices solo calculan color; desde el tercero se hace el clip contra el volumen de
// vista y, si el triangulo toca algun plano de pantalla, se recorta (scissor nativo) y se dibuja aparte.
// (La rama clipswitch != 0 del original era codigo muerto: clipswitch es una constante 0.)
void vu1DrawTriangleStripTransDouble(unsigned long ulType, VU1_STRIP_BUF* pS, unsigned short usStripMax, unsigned short usMode)
{
    PCSP_BEGIN
    VU1_PRIM_BUF* pP;
    unsigned short usStripCnt;
    unsigned int clipflag;
    int flg;
    int count;

    pcClipSetColorMode(usMode & 0xF);
    pcClipResetHistory();

    pP = (VU1_PRIM_BUF*)Ps2_DRAW_TMP;

    StripPrimVertex(pS, pP, 2);
    flg = _Check_DisplayAreaPoint((NJS_VECTOR*)&pP->fX) << 2;
    pP++;
    pS++;

    StripPrimVertex(pS, pP, 2);
    flg |= _Check_DisplayAreaPoint((NJS_VECTOR*)&pP->fX) << 1;
    pP++;
    pS++;

    for (usStripCnt = 2; usStripCnt < usStripMax; usStripCnt++, pS++, pP++)
    {
        StripPrimVertex(pS, pP, 1);
        flg |= _Check_DisplayAreaPoint((NJS_VECTOR*)&pP->fX);
        clipflag = pcClipFlagsGet();

        if ((flg != 0) && ((clipflag & 0x3FFFF)))
        {
            pP->ulKick = 32768;

            if (_Get_ClipVolumePlane() != 0)
            {
                _Set_NodeArray(pS, pP);
                count = _Check_ScissorPlane();

                if (count != 0)
                {
                    DrawScissorPolygonOpaque2(count, ulType);
                }
            }
        }

        flg = (flg << 1) & 0x7;
    }

    PCSP_SUBMIT(3, Ps2AddPrim3DEx(ulType, Ps2_DRAW_TMP, usStripMax));
}

// 100% matching!
#ifndef TARGET_PC   /* PC: el color por vertice es pc/include/pc_vu1_color.h (sin VU0); esta version es la del PS2 original */
void vu1GetVertexColor(VU1_STRIP_BUF* pStrip, VU1_PRIM_BUF* pPrim)
{
    asm volatile 
    ("
        lqc2      vf4, VU1_STRIP_BUF.fIr(%0)
        
        mfc1      t0, %2
        mfc1      t1, %2
        mfc1      t2, %2
        mfc1      t3, %3
        
        pextlw    t0, t1, t0
        pextlw    t2, t3, t2
        pcpyld    t0, t2, t0
    
        qmtc2     t0, vf5
    
        vmul.xyzw vf4, vf4, vf5 
        
        sqc2      vf4, VU1_PRIM_BUF.fR(%1)
    " : : "r"(pStrip), "r"(pPrim), "f"(128.0f), "f"(256.0f) : "$t0", "$t1", "$t2", "$t3"
    );
}
#endif

// 100% matching!
#ifndef TARGET_PC   /* PC: el color por vertice es pc/include/pc_vu1_color.h (sin VU0); esta version es la del PS2 original */
void vu1GetVertexColorCM(VU1_STRIP_BUF* pStrip, VU1_PRIM_BUF* pPrim) // first parameter is not present on the debugging symbols
{
    asm volatile 
    ("
        lqc2    vf4, VU1_COLOR.fR(%1)

        lw      t0, fVu1AlphaRatio

        qmtc2   t0, vf5 
    
        vmulx.w vf4, vf0, vf5x 

        sqc2    vf4, VU1_PRIM_BUF.fR(%0)
    " : : "r"(pPrim), "r"(&vu1Diffuse), "f"(fVu1AlphaRatio) : "$v1", "$t0"
    );
}
#endif

// 100% matching!
#ifndef TARGET_PC   /* PC: el color por vertice es pc/include/pc_vu1_color.h (sin VU0); esta version es la del PS2 original */
void vu1GetVertexColorIgnore(VU1_STRIP_BUF* pStrip, VU1_PRIM_BUF* pPrim) // first parameter is not present on the debugging symbols
{
    asm volatile 
    ("
        addi        t0, zero, 128
        
        qmtc2       t0, vf5
        
        vitof0.xyzw vf5, vf5
        
        lw          t1, fVu1AlphaRatio
    
        qmtc2       t1, vf6
    
        vaddx.xyz   vf4, vf0, vf5x
        vmulx.w     vf4, vf0, vf6x 

        sqc2        vf4, VU1_PRIM_BUF.fR(%0)
    " : : "r"(pPrim), "f"(fVu1AlphaRatio) : "$t1"
    );
}
#endif

// 100% matching!
#ifndef TARGET_PC   /* PC: el color por vertice es pc/include/pc_vu1_color.h (sin VU0); esta version es la del PS2 original */
void vu1GetVertexColorDif(VU1_STRIP_BUF* pStrip, VU1_PRIM_BUF* pPrim)
{
    asm volatile 
    ("
        lqc2       vf4, VU1_STRIP_BUF.fIr(%0)
        lqc2       vf5, VU1_COLOR.fR(%2)

        lw         t0, fVu1AlphaRatio

        vminiw.xyz vf4, vf4, vf0w
        vmaxx.xyz  vf4, vf4, vf0x 
        
        qmtc2      t0, vf6 

        vmul.xyz   vf4, vf4, vf5
        vmulx.w    vf4, vf0, vf6x 

        sqc2       vf4, VU1_PRIM_BUF.fR(%1)
    " : : "r"(pStrip), "r"(pPrim), "r"(&vu1Diffuse), "f"(fVu1AlphaRatio) : "$v1", "$t0"
    );
}
#endif

// 100% matching!
#ifndef TARGET_PC   /* PC: el color por vertice es pc/include/pc_vu1_color.h (sin VU0); esta version es la del PS2 original */
void vu1GetVertexColorDifAmb(VU1_STRIP_BUF* pStrip, VU1_PRIM_BUF* pPrim) 
{
    asm volatile 
    ("
        lqc2       vf4, VU1_STRIP_BUF.fIr(%0)

        lw         t0, fVu1AlphaRatio
        
        qmtc2      t0, vf8

        vadd.xyz   vf7, vf4, vf22
        
        vmaxx.xyz  vf7, vf7, vf0x
        vminiw.xyz vf7, vf7, vf0w
        
        vmul.xyz   vf4, vf7, vf20
        vmulx.w    vf4, vf0, vf8x 

        sqc2       vf4, VU1_PRIM_BUF.fR(%1)
    " : : "r"(pStrip), "r"(pPrim), "f"(fVu1AlphaRatio) : "$t0"
    );
}
#endif

// 96.47% matching
#ifndef TARGET_PC   /* PC: el color por vertice es pc/include/pc_vu1_color.h (sin VU0); esta version es la del PS2 original */
void vu1GetVertexColorDifSpe1(VU1_STRIP_BUF* pStrip, VU1_PRIM_BUF* pPrim)
{
    asm volatile 
    ("
        lqc2       vf4, VU1_STRIP_BUF.fIr(%0)

        lw         t0, fVu1AlphaRatio
    
        vmove.xyzw vf5, vf4
        vmove.xyzw vf6, vf4
        
        qmtc2      t0, vf8
    
        vsubw.xyz  vf7, vf4, vf0w

        vmaxx.xyz  vf4, vf4, vf0x
        vminiw.xyz vf4, vf4, vf0w
        
        vmaxx.xyz  vf7, vf7, vf0x
        vminiw.xyz vf7, vf7, vf0w
        
        vmul.xyz   vf4, vf4, vf5
        vmul.xyz   vf7, vf7, vf6
        
        vadd.xyz   vf4, vf4, vf7
        
        vmulx.w    vf4, vf0, vf8x 

        sqc2       vf4, VU1_PRIM_BUF.fR(%1)
    " : : "r"(pStrip), "r"(pPrim), "f"(fVu1AlphaRatio) : "$t0", "memory"
    );
}
#endif

// 100% matching!
#ifndef TARGET_PC   /* PC: el color por vertice es pc/include/pc_vu1_color.h (sin VU0); esta version es la del PS2 original */
void vu1GetVertexColorDifSpe2(VU1_STRIP_BUF* pStrip, VU1_PRIM_BUF* pPrim)
{
    asm volatile 
    ("
        lqc2       vf4, VU1_STRIP_BUF.fIr(%0)
        lqc2       vf5, VU1_COLOR.fR(%2)
        lqc2       vf6, VU1_COLOR.fR(%3)
        lqc2       vf7, VU1_COLOR.fR(%4)

        lw         t0, fVu1AlphaRatio
        
        qmtc2      t0, vf8
    
        vadd.xyz   vf5, vf4, vf5
        vsubw.xyz  vf5, vf5, vf0w

        vmaxx.xyz  vf4, vf4, vf0x
        vminiw.xyz vf4, vf4, vf0w
        
        vmaxx.xyz  vf5, vf5, vf0x
        vminiw.xyz vf5, vf5, vf0w
        
        vmul.xyz   vf4, vf4, vf6
        vmul.xyz   vf5, vf5, vf7
        
        vadd.xyz   vf4, vf4, vf5
        
        vmulx.w    vf4, vf0, vf8x 

        sqc2       vf4, VU1_PRIM_BUF.fR(%1)
    " : : "r"(pStrip), "r"(pPrim), "r"(&vu1Ambient), "r"(&vu1Diffuse), "r"(&vu1Specula), "f"(fVu1AlphaRatio) : "$v1", "$a2", "$a3", "$t0"
    );
}
#endif

// 100% matching!
#ifndef TARGET_PC   /* PC: el color por vertice es pc/include/pc_vu1_color.h (sin VU0); esta version es la del PS2 original */
void vu1GetVertexColorDifSpe3(VU1_STRIP_BUF* pStrip, VU1_PRIM_BUF* pPrim)
{
    asm volatile 
    ("
        lqc2       vf4, VU1_STRIP_BUF.fIr(%0)
        lqc2       vf5, VU1_COLOR.fR(%2)
        lqc2       vf6, VU1_COLOR.fR(%3)

        lw         t0, fVu1AlphaRatio
        
        qmtc2      t0, vf8

        addi       t1, zero, 17

        vaddw.xyz  vf7, vf0, vf0w

        l_002D5770:
        vmul.xyz   vf7, vf7, vf4

        addi       t1, t1, -1
        nop
        nop
        nop

        bnez       t1, l_002D5770
        nop

        vmaxx.xyz  vf4, vf4, vf0x
        vminiw.xyz vf4, vf4, vf0w
        
        vmaxx.xyz  vf7, vf7, vf0x
        vminiw.xyz vf7, vf7, vf0w
        
        vmul.xyz   vf4, vf4, vf5
        vmul.xyz   vf7, vf7, vf6
        
        vadd.xyz   vf4, vf4, vf7
        
        vmulx.w    vf4, vf0, vf8x 

        sqc2       vf4, VU1_PRIM_BUF.fR(%1)
    " : : "r"(pStrip), "r"(pPrim), "r"(&vu1Diffuse), "r"(&vu1Specula), "f"(fVu1AlphaRatio) : "$v1", "$a2", "$t0"
    );
}
#endif

// 100% matching!
#ifndef TARGET_PC   /* PC: el color por vertice es pc/include/pc_vu1_color.h (sin VU0); esta version es la del PS2 original */
void vu1GetVertexColorDifSpe1Amb(VU1_STRIP_BUF* pStrip, VU1_PRIM_BUF* pPrim)
{
    asm volatile 
    ("
        lqc2       vf4, VU1_STRIP_BUF.fIr(%0)
        lqc2       vf5, VU1_COLOR.fR(%2)
        lqc2       vf6, VU1_COLOR.fR(%3)
        lqc2       vf7, VU1_COLOR.fR(%4)

        lw         t0, fVu1AlphaRatio
        
        qmtc2      t0, vf8
    
        vsubw.xyz  vf9, vf4, vf0w

        vmaxx.xyz  vf9, vf9, vf0x
        vminiw.xyz vf9, vf9, vf0w

        vadd.xyz   vf4, vf4, vf5
        
        vmaxx.xyz  vf4, vf4, vf0x
        vminiw.xyz vf4, vf4, vf0w
        
        vmul.xyz   vf9, vf9, vf7
        vmul.xyz   vf4, vf4, vf6
        
        vadd.xyz   vf4, vf4, vf9
        
        vmulx.w    vf4, vf0, vf8x 

        sqc2       vf4, VU1_PRIM_BUF.fR(%1)
    " : : "r"(pStrip), "r"(pPrim), "r"(&vu1Ambient), "r"(&vu1Diffuse), "r"(&vu1Specula), "f"(fVu1AlphaRatio) : "$v1", "$a2", "$a3", "$t0"
    );
}
#endif

// 100% matching!
#ifndef TARGET_PC   /* PC: el color por vertice es pc/include/pc_vu1_color.h (sin VU0); esta version es la del PS2 original */
void vu1GetVertexColorDifSpe2Amb(VU1_STRIP_BUF* pStrip, VU1_PRIM_BUF* pPrim)
{
    asm volatile 
    ("
        lqc2       vf4, VU1_STRIP_BUF.fIr(%0)
        lqc2       vf5, VU1_COLOR.fR(%2)
        lqc2       vf6, VU1_COLOR.fR(%3)
        lqc2       vf7, VU1_COLOR.fR(%4)

        lw         t0, fVu1AlphaRatio
        
        qmtc2      t0, vf8

        vadd.xyz   vf4, vf4, vf5
        vsubw.xyz  vf9, vf4, vf0w

        vmaxx.xyz  vf9, vf9, vf0x
        vminiw.xyz vf9, vf9, vf0w
        
        vmaxx.xyz  vf4, vf4, vf0x
        vminiw.xyz vf4, vf4, vf0w
        
        vmul.xyz   vf9, vf9, vf7
        vmul.xyz   vf4, vf4, vf6
        
        vadd.xyz   vf4, vf4, vf9
        
        vmulx.w    vf4, vf0, vf8x 

        sqc2       vf4, VU1_PRIM_BUF.fR(%1)
    " : : "r"(pStrip), "r"(pPrim), "r"(&vu1Ambient), "r"(&vu1Diffuse), "r"(&vu1Specula), "f"(fVu1AlphaRatio) : "$v1", "$a2", "$a3", "$t0"
    );
}
#endif

// 100% matching!
#ifndef TARGET_PC   /* PC: el color por vertice es pc/include/pc_vu1_color.h (sin VU0); esta version es la del PS2 original */
void vu1GetVertexColorDifSpe3Amb(VU1_STRIP_BUF* pStrip, VU1_PRIM_BUF* pPrim)
{
    asm volatile 
    ("
        lqc2       vf4, VU1_STRIP_BUF.fIr(%0)
        lqc2       vf5, VU1_COLOR.fR(%2)
        lqc2       vf6, VU1_COLOR.fR(%3)
        lqc2       vf7, VU1_COLOR.fR(%4)

        lw         t0, fVu1AlphaRatio
        
        qmtc2      t0, vf8

        vadd.xyz   vf4, vf4, vf5

        addi       t1, zero, 17

        vaddw.xyz  vf9, vf0, vf0w

        l_002D58E0:
        vmul.xyz   vf9, vf9, vf4

        addi       t1, t1, -1
        nop
        nop
        nop

        bnez       t1, l_002D58E0
        nop

        vmaxx.xyz  vf4, vf4, vf0x
        vminiw.xyz vf4, vf4, vf0w
        
        vmaxx.xyz  vf9, vf9, vf0x
        vminiw.xyz vf9, vf9, vf0w
        
        vmul.xyz   vf4, vf4, vf6
        vmul.xyz   vf9, vf9, vf7
        
        vadd.xyz   vf4, vf4, vf9
        
        vmulx.w    vf4, vf0, vf8x 

        sqc2       vf4, VU1_PRIM_BUF.fR(%1)
    " : : "r"(pStrip), "r"(pPrim), "r"(&vu1Ambient), "r"(&vu1Diffuse), "r"(&vu1Specula), "f"(fVu1AlphaRatio) : "$v1", "$a2", "$a3", "$t0"
    );
}
#endif

// 100% matching! 
void vu1RotTransStripBuf(NJS_MATRIX* pMatrix, NJS_VECTOR* pVector, VU1_STRIP_BUF* pBuf)
{
    float fZ;
    float fIz;
    
    njCalcPoint(pMatrix, pVector, (NJS_POINT3*)&pBuf->fVx);
    
    fIz = 1.0f / pBuf->fVz;
    
    fZ = fVu1Projection * fIz;
    
    pBuf->fSx = fVu1AspectW * (pBuf->fVx * fZ);
    pBuf->fSy = fVu1AspectH * (pBuf->fVy * fZ);
    
    pBuf->fIz = fIz;
    
    pBuf->fFog = njCalcFogPowerEx(fIz);
}

// PC: C plano. Por vertice: color (pFunc), campos de VU1_PRIM_BUF, flags de clip contra el volumen de vista y el
// vertice se guarda en triangle.node[rotflag] por si hay que recortar el triangulo (ScissorTriangle).
void vu1DrawTriangleStripTransDoubleI(unsigned long ulType, VU1_STRIP_BUF* pS, unsigned short usStripMax, unsigned short usMode)
{
    VU1_PRIM_BUF* pP;
    unsigned short usStripCnt;
    void (*pFunc)(VU1_STRIP_BUF*, VU1_PRIM_BUF*);
    unsigned int clipflag;
    int flg;

    clipflag = 0;
    flg = 0;

    InitNodeArraySet(&scissorflip);

    pP = (VU1_PRIM_BUF*)Ps2_DRAW_TMP;
    pFunc = pcColorCalcFunc(usMode & 0xF);

    for (usStripCnt = 0; usStripCnt < usStripMax; usStripCnt++, pS++, pP++)
    {
        NODE* nd = &scissorflip.triangle.node[scissorflip.rotflag];

        pFunc(pS, pP);

        pP->fS = vu_f(pS->fU * pS->fIz);
        pP->fT = vu_f(pS->fV * pS->fIz);
        pP->fQ = vu_f(0.0f + pS->fIz);
        pP->ulKick = 0;

        pP->fX = vu_f(pS->fSx + pcScreen()->offset[0]);
        pP->fY = vu_f(pS->fSy + pcScreen()->offset[1]);
        pP->fZ = pS->fVz;
        pP->fF = pS->fFog;

        memcpy(nd->vertex, &pS->fVx, 16);
        memcpy(nd->color, &pP->fR, 16);
        memcpy(nd->texUV, &pS->fU, 16);
        clipflag |= (unsigned int)pcClipViewVertex(nd->clipV, &pS->fVx) & 0x3F;

        scissorflip.rotflag = (scissorflip.rotflag + 1) & 0x3;
        if (scissorflip.rotflag >= 3) scissorflip.rotflag = 0;

        flg |= _Check_DisplayAreaPoint((NJS_VECTOR*)&pP->fX);

        if ((((flg & 0x7)) && ((clipflag & 0x3FFFF))) && (usStripCnt >= 2))
        {
            pP->ulKick = 32768;
            ScissorTriangle(&scissorflip, &planeset);

            if (scissorflip.in->nodeNum != 0)
            {
                DrawScissorPolygonOpaque(&scissorflip, ulType);
                ResetNodeArraySet(&scissorflip);
            }
        }

        clipflag <<= 6;
        flg <<= 1;
    }

    Ps2AddPrim3DExI(ulType, Ps2_DRAW_TMP, usStripMax);
}

// PC: igual que DrawScissorPolygonOpaque, hacia la cola 1P
void DrawScissorPolygonTrans1P(SCISSOR* scissor, unsigned long ulType)
{
    SCISSOR_NODE* in = scissor->in;

    pcScissorProject(vu1ScessorBuf, sizeof(VU1_PRIM_BUF), (int)in->nodeNum, in->node, sizeof(NODE),
                     offsetof(NODE, vertex), offsetof(NODE, texUV), offsetof(NODE, color), 1);
    ulType = (ulType & 0xFFFDFFFFFFFFFFFF) | 0x6800000000000;
    Ps2AddPrim3DEx1P(ulType, vu1ScessorBuf, in->nodeNum);
}

// 100% matching!
void vu1DrawTriangleStripTransDouble1P(unsigned long ulType, VU1_STRIP_BUF* pS, unsigned short usStripMax, unsigned short usMode)
{
    VU1_PRIM_BUF* pP;                                      
    float fIz;                                    
    float sam;                                    
    unsigned short usStripCnt;                                   
    void (*pFunc)(VU1_STRIP_BUF*, VU1_PRIM_BUF*); 
    unsigned int clipflag;                                                           
    int flg;                                                             

    clipflag = 0; 
    flg = 0;

    InitNodeArraySet(&scissorflip); 
    
    pP = (VU1_PRIM_BUF*)Ps2_DRAW_TMP; 

    sam = 0;
    
    pFunc = pcColorCalcFunc(usMode & 0xF); 
    
    for (usStripCnt = 0; usStripCnt < usStripMax; usStripCnt++)
    {
        sam += pS[usStripCnt].fVz;
    }
    
    Ps2AddPrimPrio = sam / usStripMax;
    
    for (usStripCnt = 0; usStripCnt < usStripMax; usStripCnt++, pS++, pP++) 
    {
        pFunc(pS, pP);

        fIz = pS->fIz;
       
        pP->fS = pS->fU * fIz;
        pP->fT = pS->fV * fIz;
        pP->fQ = fIz;
        
        pP->fX = fVu1OffsetX + pS->fSx;
        pP->fY = fVu1OffsetY + pS->fSy;
        pP->fZ = pS->fVz;
        
        pP->fF = pS->fFog;

        pP->ulKick = 0;

        clipflag |= _Clip_ViewVolume(node.clipV, ClipMatrix2[0], &pS->fVx) & 0x3F;
    
        flg |= _Check_DisplayAreaPoint((NJS_VECTOR*)&pP->fX); 

        *(u_long128*)&node.texUV = *(u_long128*)&pS->fU;
        *(u_long128*)&node.vertex = *(u_long128*)&pS->fVx;
        *(u_long128*)&node.color = *(u_long128*)&pP->fR;
        
        PushTriangleNodeArray(&scissorflip, &node);

        if ((((flg & 0x7)) && ((clipflag & 0x3FFFF))) && (usStripCnt >= 2)) 
        { 
            pP->ulKick = 32768;
            
            ScissorTriangle(&scissorflip, &planeset); 
            
            if (scissorflip.in->nodeNum != 0) 
            { 
                DrawScissorPolygonTrans1P(&scissorflip, ulType); 
       
                ResetNodeArraySet(&scissorflip); 
            } 
        }
        
        clipflag <<= 6; 
        flg <<= 1; 
    } 
    
    Ps2AddPrim3DEx1P(ulType, Ps2_DRAW_TMP, usStripMax); 
} 
