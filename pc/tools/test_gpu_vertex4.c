#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdint.h>
static float sat(float x){return floorf(fminf(fmaxf(x,0.0f),255.0f));}
static float rnd(float a,float b){return a+(b-a)*(float)rand()/RAND_MAX;}
int main(void){
  long bad=0,n=2000000; float za=-1234.5f, zb=-56789.0f;
  for(long k=0;k<n;k++){
    float x=rnd(1.0f,4094.0f),y=rnd(1.0f,4094.0f),q=rnd(0.0f,0.05f),a=rnd(-20,300),f=rnd(-5,400),S=rnd(0,1);
    /* CPU: pcPs2Prim3DCore + pcSubmitPrepared3D */
    float sz=-za+q*-zb; if(sz<0)sz=0; if(sz>65534.0f)sz=65534.0f;
    unsigned d10=(unsigned)(int)(sz*16.0f), d8=(unsigned)(int)(x*16.0f);
    float gz=(float)(int32_t)d10/16.0f, cx=(float)(int32_t)d8/16.0f-1728.0f;
    float cz=fminf(fmaxf(1.0f-gz/65535.0f,0.0f),1.0f);
    float ca=fminf(fmaxf(sat(a)/128.0f,0.0f),1.0f), cf=sat(f)/255.0f, cu=S*512.0f;
    /* GLSL transcription */
    float sz2=fminf(fmaxf(-za+q*-zb,0.0f),65534.0f);
    float g2=truncf(sz2*16.0f)/16.0f, d2=fminf(fmaxf(1.0f-g2/65535.0f,0.0f),1.0f);
    float p=truncf(x*16.0f)/16.0f-1728.0f;
    float a2=fminf(fmaxf(floorf(fminf(fmaxf(a,0.0f),255.0f))/128.0f,0.0f),1.0f);
    float f2=floorf(fminf(fmaxf(f,0.0f),255.0f))/255.0f, u2=S*512.0f;
    if(cx!=p||cz!=d2||ca!=a2||cf!=f2||cu!=u2) bad++;
  }
  printf("diffs=%ld of %ld\n",bad,n); return 0; }
