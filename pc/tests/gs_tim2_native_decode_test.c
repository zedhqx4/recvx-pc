/* Host test (not part of the game build). Build:
 *   cc -std=gnu11 -O1 -DTARGET_PC -include stddef.h -include stdlib.h -Ipc/src/gs -Ipc/include -Ipc/src pc/tests/gs_tim2_native_decode_test.c */
/* Direct A+D application vs the GIF packet path: GS state must end up identical. */
#include <stdio.h>
#include <string.h>
#include "../src/gs/gs.c"
void gsRendererDraw(const GsDrawState* s,const GsVertex* v,int n){(void)s;(void)v;(void)n;}
void gsRendererInit(void){} void gsRendererPresent(unsigned a,unsigned b){(void)a;(void)b;}
int pcIniGetInt(const char*a,int d){(void)a;return d;}
void pcLog(const char*f,...){(void)f;} double pcOsTime(void){return 0;}
/* gs_mem.c provides the VRAM; its own externs: */
double pcGameProfileNowMs(void){return 0;} int pcGameProfileTextureUploadActive(void){return 0;}
void pcGameProfileRecordTextureTransfer(double a,unsigned b){(void)a;(void)b;}
void gsXferDone(unsigned a,unsigned b,unsigned c,unsigned d,unsigned e,unsigned f,unsigned g,int h){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;}
void gsFlushLocalToLocal(unsigned a,unsigned b,unsigned c,unsigned d,unsigned e,unsigned f,unsigned g){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;}
#include "../src/gs/gs_mem.c"
#include "../src/gs/gs_native.c"


static uint64_t tex0(unsigned tbp,unsigned tbw,unsigned psm,unsigned tw,unsigned th){
  return (uint64_t)tbp|((uint64_t)tbw<<14)|((uint64_t)psm<<20)|((uint64_t)tw<<26)|((uint64_t)th<<30);}
static unsigned lg2(unsigned v){unsigned n=0;while((1u<<n)<v)n++;return n;}

/* what LoadToVram sends: A+D tag, 4 registers, IMAGE tag, data */
static void load_to_vram(unsigned tbp,const unsigned char* data,unsigned tbw,unsigned psm,unsigned width,unsigned height,unsigned qwc){
  size_t words=(size_t)(4+4*4+4+qwc*4); unsigned int* pk=(unsigned int*)calloc(words,4);
  uint64_t tag=1ULL|(4ULL<<60), regs=0xEEEEULL; memcpy(&pk[0],&tag,8); memcpy(&pk[2],&regs,8);
  uint64_t ad[8]={ (uint64_t)tbp<<32|(uint64_t)tbw<<48|(uint64_t)psm<<56, 0x50, 0, 0x51, (uint64_t)width|((uint64_t)height<<32), 0x52, 0, 0x53 };
  for(int k=0;k<4;k++){memcpy(&pk[4+4*k],&ad[2*k],8);memcpy(&pk[4+4*k+2],&ad[2*k+1],8);}
  uint64_t itag=(uint64_t)qwc|(2ULL<<58); memcpy(&pk[20],&itag,8);
  memcpy(&pk[24],data,(size_t)qwc*16);
  gif.in_packet=0; gsGifTransfer(pk,1+4+1+qwc); free(pk);
}
int main(void){
  int fails=0,cases=0; gsMemInit();
  int dims[][2]={{256,256},{512,512},{256,512},{512,256},{1024,256},{64,64},{128,128},{128,64},{64,128},{32,32},{16,64},{128,32}};
  for(unsigned d=0;d<sizeof dims/sizeof*dims;d++) for(int t4=0;t4<2;t4++){
    int w=dims[d][0],h=dims[d][1]; int p32=(w>128&&h>128);
    unsigned psm=t4?GS_PSMT4:GS_PSMT8; unsigned tbw=(w+63)/64; if(tbw&1)tbw++;
    size_t bytes=(size_t)w*h/(t4?2:1); unsigned char* data=(unsigned char*)malloc(bytes);
    for(size_t i=0;i<bytes;i++) data[i]=(unsigned char)((i*2654435761u)>>13);
    unsigned char* a=(unsigned char*)malloc((size_t)w*h); unsigned char* b=(unsigned char*)malloc((size_t)w*h);
    memset(gsVram,0,sizeof gsVram);
    /* real path */
    if(p32){ unsigned bw_ct=(w/128)?(w/128):1; unsigned rrh=t4?h/4:h/2; unsigned qwc=t4?(w*h)/32:(w*h)/16;
             load_to_vram(190*64,data,bw_ct,0,w/2,rrh,qwc); }
    else   { unsigned qwc=(unsigned)((size_t)w*h>>(t4?5:4)); if(qwc==0)qwc=1; /* tiny images: round up to whole qwords */
             unsigned char* padded=(unsigned char*)calloc(1,(size_t)qwc*16); memcpy(padded,data,bytes);
             load_to_vram(190*64,padded,tbw,psm,w,h,qwc); free(padded); }
    gsDecodeIndices(tex0(190*64,tbw,psm,lg2(w),lg2(h)),w,h,a);
    /* native path */
    int ok=gsMemDecodeTim2Indices(data,t4,p32,w,h,(int)tbw,b);
    cases++;
    /* HD replacement hashes block bytes: the swizzled plane must equal the real VRAM blocks */
    { int bx=t4?32:16, by=16;
      if(w>=bx && h>=by){
        const unsigned char* img=gsMemSwizzleIndices((int)psm,w,h,(int)tbw,a);
        int bad=0; for(int yy=0;yy<h;yy+=by) for(int xx=0;xx<w;xx+=bx){
          int br=gsBlockNumber((int)psm,190*64,tbw,xx,yy), bn=gsBlockNumber((int)psm,0,tbw,xx,yy);
          if(!img||memcmp(&gsVram[br*256],&img[bn*256],256)) bad++; }
        if(bad){fails++;printf("SWIZZLE MISMATCH %dx%d %s: %d blocks differ\n",w,h,t4?"T4":"T8",bad);} } }
    if(!ok||memcmp(a,b,(size_t)w*h)){ fails++; size_t bad=0; if(ok) while(a[bad]==b[bad])bad++; printf("MISMATCH %dx%d %s p32=%d ok=%d first diff at %zu\n",w,h,t4?"T4":"T8",p32,ok,bad);}
    free(a);free(b);free(data);
  }
  { /* TBP bindings: valid until a real GS write touches the covered pages */
    unsigned g; uint64_t hsh; int psm,w,h,tbw; unsigned char plane[128*64]; memset(plane,7,sizeof plane);
    setenv("RECVX_GS_NATIVE","1",1);
    if(pcGsNativeEnsureTim2Plane(plane,sizeof plane,42,0x1234,0,128,64)!=1){fails++;printf("ensure plane failed\n");}
    pcGsNativeBindTbp(12160+16,42,0x1234,0,128,64);
    if(!gsNativeTbpLookup(12160+16,&g,&hsh,&psm,&w,&h,&tbw)||g!=42||w!=128||h!=64||psm!=GS_PSMT8){fails++;printf("bind lookup failed\n");}
    if(gsNativeTbpLookup(12160,&g,&hsh,&psm,&w,&h,&tbw)){fails++;printf("lookup of unbound TBP succeeded\n");}
    gsMemWrite(GS_PSMCT32,12160+16,2,5,5,0xFFFFFFFFu);                 /* a real write into the bound pages */
    if(gsNativeTbpLookup(12160+16,&g,&hsh,&psm,&w,&h,&tbw)){fails++;printf("binding survived a real GS write\n");}
    pcGsNativeBindTbp(12160+16,42,0x1234,0,128,64); pcGsNativeBindClear();
    if(gsNativeTbpLookup(12160+16,&g,&hsh,&psm,&w,&h,&tbw)){fails++;printf("binding survived clear\n");}
    if(pcGsNativeEnsureTim2Plane(plane,10,43,0x9,0,128,64)!=0){fails++;printf("short image accepted\n");}
  }
  printf("tim2 index decode: %d cases, %d mismatches\n",cases,fails); return fails!=0; }
