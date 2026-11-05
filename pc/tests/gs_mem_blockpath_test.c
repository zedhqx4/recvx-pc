/* Deterministic equivalence test: compare GS_BLOCK_FASTPATH=1 and =0 builds. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/gs/gs_mem.c"

static unsigned long long done_hash=1469598103934665603ULL;
static unsigned int done_count;
static unsigned long long result_hash=1469598103934665603ULL;
static unsigned int rng_state=0x13579BDFu;

void gsXferDone(unsigned int bp,unsigned int bw,unsigned int psm,unsigned int x,unsigned int y,unsigned int w,unsigned int h,int changed)
{
    unsigned long long a=bp|((unsigned long long)bw<<14)|((unsigned long long)psm<<20)|
                         ((unsigned long long)x<<26)|((unsigned long long)y<<38)|((unsigned long long)(changed!=0)<<50);
    unsigned long long b=w|((unsigned long long)h<<16);
    done_hash=(done_hash^a)*1099511628211ULL;
    done_hash=(done_hash^b)*1099511628211ULL;
    done_count++;
}
void gsFlushLocalToLocal(unsigned int bp,unsigned int bw,unsigned int psm,unsigned int x,unsigned int y,unsigned int w,unsigned int h)
{(void)bp;(void)bw;(void)psm;(void)x;(void)y;(void)w;(void)h;}

static unsigned int rnd(void){rng_state=rng_state*1664525u+1013904223u;return rng_state>>8;}
static void hash_bytes(const void* data,size_t n)
{
    const unsigned char* p=(const unsigned char*)data;size_t i;
    for(i=0;i<n;i++)result_hash=(result_hash^p[i])*1099511628211ULL;
}
static void reset_gs(void)
{
    unsigned int i;
    gsMemInit();memset(gsPageGen,0,sizeof(gsPageGen));
    memset(pageTouchEpoch,0,sizeof(pageTouchEpoch));currentPageTouchEpoch=0;coalescePageTouches=0;
    done_hash=1469598103934665603ULL;done_count=0;
    for(i=0;i<GS_VRAM_SIZE;i++)gsVram[i]=(unsigned char)rnd();
}
static void hash_state(void)
{
    hash_bytes(gsVram,sizeof(gsVram));hash_bytes(gsPageGen,sizeof(gsPageGen));
    hash_bytes(&done_hash,sizeof(done_hash));hash_bytes(&done_count,sizeof(done_count));
}
static void make_regs(GsRegs* r,unsigned int bp,unsigned int bw,unsigned int psm,unsigned int sx,unsigned int sy,unsigned int w,unsigned int h,unsigned int dir)
{
    memset(r,0,sizeof(*r));r->bitbltbuf=((u64)bp<<32)|((u64)bw<<48)|((u64)psm<<56);
    r->trxpos=((u64)sx<<32)|((u64)sy<<48);r->trxreg=(u64)w|((u64)h<<32);r->trxdir=dir;
}
static void host_case(int psm,unsigned int sx,unsigned int sy,unsigned int w,unsigned int h,unsigned int bw,int split)
{
    GsBlockInfo info;GsRegs r;unsigned int rb,total,padded,off;unsigned char* data;
    reset_gs();if(!gs_block_info(psm,&info))abort();rb=(w*info.bits+7u)/8u;total=rb*h;padded=(total+15u)&~15u;
    data=(unsigned char*)malloc(padded);if(!data)abort();for(off=0;off<padded;off++)data[off]=(unsigned char)rnd();
    make_regs(&r,37,bw,(unsigned int)psm,sx,sy,w,h,0);gsMemStartTransfer(&r);off=0;
    while(off<padded)
    {
        unsigned int q=split?(1u+rnd()%7u):(padded/16u),left=(padded-off)/16u;
        if(q>left)q=left;
        gsMemTransferData(data+off,(int)q);off+=q*16u;
    }
    hash_state();free(data);
}
static void read_case(int psm,unsigned int sx,unsigned int sy,unsigned int w,unsigned int h,unsigned int bw)
{
    GsRegs r;unsigned int bits=(unsigned int)gsPsmBits(psm);size_t n=(size_t)((w*bits+7u)/8u)*h;
    unsigned char* out=(unsigned char*)malloc(n?n:1);if(!out)abort();
    make_regs(&r,37,bw,(unsigned int)psm,sx,sy,w,h,1);
    r.bitbltbuf=37u|((u64)bw<<16)|((u64)psm<<24);r.trxpos=(u64)sx|((u64)sy<<16);
    memset(out,0xA5,n);gsMemReadImage(&r,out);hash_bytes(out,n);free(out);
}
static void local_copy_case(int psm,unsigned int sx,unsigned int sy,unsigned int w,unsigned int h,unsigned int bw,int overlap)
{
    GsRegs r;unsigned int sbp=37,dbp=overlap?37:4096,dx=overlap?sx+8u:sx;
    reset_gs();memset(&r,0,sizeof(r));
    r.bitbltbuf=(u64)sbp|((u64)bw<<16)|((u64)psm<<24)|((u64)dbp<<32)|((u64)bw<<48)|((u64)psm<<56);
    r.trxpos=(u64)sx|((u64)sy<<16)|((u64)dx<<32)|((u64)sy<<48);
    r.trxreg=(u64)w|((u64)h<<32);r.trxdir=2;gsMemStartTransfer(&r);hash_state();
}
static void render_target_case(unsigned int w,unsigned int h)
{
    unsigned int* p;unsigned int i;reset_gs();p=(unsigned int*)malloc((size_t)w*h*sizeof(*p));if(!p)abort();
    for(i=0;i<w*h;i++)p[i]=rnd();
    gsMemWriteRenderTarget(37,2,w,h,p);hash_state();free(p);
}
int main(void)
{
    static const struct{int psm;unsigned int sx,sy,w,h,bw;} cases[]={
        {GS_PSMCT32,8,8,16,16,2},{GS_PSMZ32,8,8,16,16,2},{GS_PSMCT24,8,8,16,16,2},{GS_PSMZ24,8,8,16,16,2},
        {GS_PSMCT16,16,8,32,16,2},{GS_PSMCT16S,16,8,32,16,2},{GS_PSMZ16,16,8,32,16,2},{GS_PSMZ16S,16,8,32,16,2},
        {GS_PSMT8,16,16,32,32,2},{GS_PSMT4,32,16,64,32,2},{GS_PSMT8H,8,8,16,16,2},
        {GS_PSMT4HL,8,8,16,16,2},{GS_PSMT4HH,8,8,16,16,2}
    };
    unsigned int i;
    for(i=0;i<sizeof(cases)/sizeof(cases[0]);i++)
    {
        host_case(cases[i].psm,cases[i].sx,cases[i].sy,cases[i].w,cases[i].h,cases[i].bw,0);
        host_case(cases[i].psm,cases[i].sx,cases[i].sy,cases[i].w,cases[i].h,cases[i].bw,1);
        read_case(cases[i].psm,cases[i].sx,cases[i].sy,cases[i].w,cases[i].h,cases[i].bw);
        if(cases[i].psm!=GS_PSMCT24&&cases[i].psm!=GS_PSMZ24&&cases[i].psm!=GS_PSMT8H&&cases[i].psm!=GS_PSMT4HL&&cases[i].psm!=GS_PSMT4HH)
        {
            local_copy_case(cases[i].psm,cases[i].sx,cases[i].sy,cases[i].w,cases[i].h,cases[i].bw,0);
            if(cases[i].psm==GS_PSMCT32)local_copy_case(cases[i].psm,8,8,16,16,cases[i].bw,1);
        }
    }
    render_target_case(16,16);render_target_case(13,11);
    printf("%016llx\n",result_hash);return 0;
}
