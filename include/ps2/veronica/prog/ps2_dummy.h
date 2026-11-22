#ifndef _PS2_DUMMY_H_
#define _PS2_DUMMY_H_

#include "types.h"
#include "pc_prim.h"

typedef struct PS2_TP_CACHE
{
    // total size: 0x10
	void* tp;              // offset 0x0, size 0x4
    unsigned int bank;     // offset 0x4, size 0x4
    unsigned int tex_addr; // offset 0x8, size 0x4
    unsigned int clt_addr; // offset 0xC, size 0x4
} PS2_TP_CACHE;

/* Memoria de los modos de mezcla pedidos por njSetBlendingMode/njColorBlendingModeSys (origen y destino, cual se fijo al ultimo).
 * En la PS2 compartia estructura con los registros GS que se volcaban por DMA; en PC solo quedan estos campos. */
typedef struct PS2_GS_SAVE
{
    unsigned char mode_bk[2];
    unsigned char set_last;
    unsigned char dc_alpha;
} PS2_GS_SAVE;

void _builtin_set_imask(int mask);
void Ps2Init();
void Ps2LoadModule(char* p);
void Snd_init();
void Cd_init();
void Card_init();
void PS2_jikken();
void Ps2AddPrim2D(unsigned long prim, void* dp, unsigned int num);
void Ps2AddPrim3D(unsigned long prim, void* dp, unsigned int num);
void Ps2AddPrim3DEx(unsigned long prim, void* dp, unsigned int num);
void Ps2AddPrim3DEx1P(unsigned long prim, void* dp, unsigned int num);
void Ps2AddPrim3DMod(unsigned long prim, void* dp, unsigned int num);
void Ps2DrawOTag();
unsigned int Ps2BitCount(unsigned int value);
void Ps2InitTexCache();
int Ps2GlobalIndexTexLoad(unsigned int index);
int Ps2TexLoad(NJS_TEXMEMLIST* addr);
void Ps2SetFogColor();
void Ps2SetFogColorSys(unsigned int r, unsigned int g, unsigned int b);
void Ps2AlphaIs000(unsigned int* cp, unsigned int num);
unsigned int Ps2AlphaIsHalf(unsigned int* cp, unsigned int num);
unsigned int Ps2Alpha4to8(unsigned int* cp, unsigned int num);
int Ps2CheckTextureAlpha(void* pp);
void Ps2ShadowStart();
void Ps2ShadowDraw();
void Ps2ShadowMain0();
void Ps2ShadowMain1();
void Ps2ShadowEnd();
void Ps2AddPrim3DExI(unsigned long prim, void* dp, unsigned int num);
void PS2_Render_Tex_Sub();

extern unsigned int PS2_Render_tex_sub_flag;
extern float Ps2AddPrimPrio;
extern int ViewType;
extern unsigned char Ps2_PBUFF[1835008];
extern unsigned int Ps2_use_pt_flag;
extern PS2_GS_SAVE Ps2_gs_save;
extern unsigned int Ps2_albinoid_flag;
extern unsigned int Ps2_sys_cnt;
extern unsigned char* Ps2_MOVIE;
extern unsigned int Ps2_ice_flag;
extern unsigned int palbuf[4096];
extern float mbuf[128][16];
extern float cmmat[2][16];
extern NJS_MATRIX crmat;
extern float Ps2_shadow_fog;
extern float Ps2_shadow_vec[4];
extern int Ps2_shadow_z;
extern NJS_MATRIX lcmat[12];
extern Uint8* _BSG_END;
extern NJS_VERTEX_BUF* _nj_vertex_buf_;
extern unsigned int _nj_control_3d_flag_;
extern NJS_SCREEN _nj_screen_;
extern unsigned char Ps2_DRAW_TMP[16384];
extern unsigned int Ps2_njControl3D_flag;
extern void (* VsyncFunc)();
extern void (* EorFunc)();
extern unsigned int Ps2_vcount;
extern unsigned int Ps2_dbuff;
extern unsigned int Ps2_tex_load_tp_cancel;
extern NJS_TEXMEMLIST* Ps2_now_tex;

void Ps2PcTextureSourceCacheReset(void);

#endif
