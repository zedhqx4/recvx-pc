/* Host test (not part of the game build). Build:
 *   cc -std=gnu11 -O1 -DTARGET_PC -include stddef.h -Ipc/src/gs -Ipc/include -Ipc/src pc/tests/gs_direct_ad_test.c */
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

static uint64_t tex0(unsigned tbp,unsigned tbw,unsigned psm,unsigned tw,unsigned th,unsigned tcc,unsigned tfx,unsigned cbp,unsigned cpsm,unsigned csm,unsigned csa,unsigned cld){
  return (uint64_t)tbp|((uint64_t)tbw<<14)|((uint64_t)psm<<20)|((uint64_t)tw<<26)|((uint64_t)th<<30)|((uint64_t)tcc<<34)|((uint64_t)tfx<<35)|((uint64_t)cbp<<37)|((uint64_t)cpsm<<51)|((uint64_t)csm<<55)|((uint64_t)csa<<56)|((uint64_t)cld<<61);}
static void snapshot(unsigned char*buf,size_t*n,unsigned*clut){ memcpy(buf,&gsRegs,sizeof gsRegs); *n=sizeof gsRegs;
  gsReadClut32(gsRegs.tex0[0],gsRegs.texa,256,clut); }
int main(void){
  int fails=0, cases=0, i;
  gsMemInit();
  for(i=0;i<1024;i++) gsMemWrite(GS_PSMCT32,254*64,1,i%16,(i/16)%16,0x80000000u|(i*2654435761u));
  unsigned psms[]={0,0x13,0x14}; unsigned tws[]={5,7,8}; unsigned banks[]={0,0x13,0x2a,0x35};
  for(int pm=0;pm<3;pm++)for(int tw=0;tw<3;tw++)for(int b=0;b<4;b++)for(int variant=0;variant<3;variant++){
    unsigned psm=psms[pm],bank=banks[b],tbw=(1<<tws[tw])/64?(1<<tws[tw])/64:1; if(psm&&(tbw&1))tbw++;
    uint64_t t0a,t0b; unsigned cbp,csa=0,cpsm=0,csm=0;
    (void)csm;
    if(variant==0){ cbp=((bank&0x30)/4)+16368; t0a=tex0(190*64,tbw,0x13,tws[tw],tws[tw],1,0,cbp,0,0,0,2); t0b=tex0(190*64,tbw,psm,tws[tw],tws[tw],1,0,cbp,0,0,bank&0xF,0);}
    else if(variant==1){ t0a=t0b=tex0(190*64,tbw,psm,tws[tw],tws[tw],1,0,254*64,0,0,0,2);}
    else { t0a=t0b=tex0(190*64,tbw,0,tws[tw],tws[tw],0,0,254*64,0,0,0,2);}
    (void)csa;(void)cpsm;
    uint64_t clampv=(uint64_t)(bank&3)|((uint64_t)255<<4)|((uint64_t)255<<16);
    uint64_t ad[12]={0,0x3f, t0a,0x06, t0b,0x06, 0x0,0x14, clampv,0x08, 0x30001ULL|(7ULL<<17),0x47};
    /* GIF packet: tag (NLOOP=6, EOP=1, FLG=0 PACKED, NREG=1, REGS=0xE) then 6 A+D qwords */
    unsigned int pkt[4*7]; memset(pkt,0,sizeof pkt);
    uint64_t tag=6|(1ULL<<15)|(1ULL<<60); memcpy(&pkt[0],&tag,8); uint64_t regs=0xE; memcpy(&pkt[2],&regs,8);
    for(int k=0;k<6;k++){ memcpy(&pkt[4*(k+1)],&ad[2*k],8); memcpy(&pkt[4*(k+1)+2],&ad[2*k+1],8);}    
    /* path A: packet */
    memset(&gsRegs,0,sizeof gsRegs); gsTextureAssetId[0]=77;gsTextureAssetValid[0]=1; gsTextureBindingGeneration=0; gif.in_packet=0;
    gsGifTransfer(pkt,7);
    unsigned char sa[sizeof gsRegs]; size_t n; unsigned ca[256],cb[256]; snapshot(sa,&n,ca);
    uint64_t ga=gsTextureBindingGeneration; unsigned ida=gsTextureAssetId[0]; int va=gsTextureAssetValid[0];
    /* path B: direct */
    memset(&gsRegs,0,sizeof gsRegs); gsTextureAssetId[0]=77;gsTextureAssetValid[0]=1; gsTextureBindingGeneration=0; gif.in_packet=0;
    int r=pcGsApplyAdList(ad,6);
    unsigned char sb[sizeof gsRegs]; snapshot(sb,&n,cb);
    cases++;
    if(!r||memcmp(sa,sb,n)||memcmp(ca,cb,sizeof ca)||ga!=gsTextureBindingGeneration||ida!=gsTextureAssetId[0]||va!=gsTextureAssetValid[0]){fails++;printf("MISMATCH psm=%x tw=%u bank=%x variant=%d\n",psm,tws[tw],bank,variant);}
  }
  /* busy GIF -> must refuse and write nothing */
  gif.in_packet=1; uint64_t ad1[2]={123,0x06}; uint64_t before=gsRegs.tex0[0];
  if(pcGsApplyAdList(ad1,1)!=0||gsRegs.tex0[0]!=before){fails++;printf("busy GIF not refused\n");}
  gif.in_packet=0;
  printf("equiv: %d cases, %d failures\n",cases,fails); return fails!=0; }
