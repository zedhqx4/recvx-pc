#include <stdio.h>
#include <stdint.h>
#include <string.h>
typedef unsigned int GLenum;
enum { GL_ZERO=0, GL_ONE=1, GL_SRC_ALPHA=770, GL_ONE_MINUS_SRC_ALPHA=771, GL_DST_ALPHA=772, GL_ONE_MINUS_DST_ALPHA=773,
 GL_CONSTANT_ALPHA=32771, GL_ONE_MINUS_CONSTANT_ALPHA=32772, GL_FUNC_ADD=32774, GL_FUNC_SUBTRACT=32778, GL_FUNC_REVERSE_SUBTRACT=32779 };
typedef struct { GLenum equation, src, dst; float fix; } GsBlend;
enum { PC_BLEND_CS = 0, PC_BLEND_CD = 1, PC_BLEND_ZERO = 2 };
enum { PC_BLEND_AS = 0, PC_BLEND_AD = 1, PC_BLEND_FIX = 2 };
typedef struct PcBlendEquation { unsigned char a, b, c, d, fix; } PcBlendEquation;
typedef struct { int ka, kb; } GsCoef;

/* Traduce un coeficiente a un factor de glBlendFunc. c: 0=As, 1=Ad, 2=FIX. Devuelve 1 si el coeficiente es negativo
 * (se resuelve con SUBTRACT/REVERSE_SUBTRACT). */
static int gs_coef_to_factor(GsCoef k, int c, GLenum* factor)
{
    GLenum plain = (c == 1) ? GL_DST_ALPHA : (c == 2) ? GL_CONSTANT_ALPHA : GL_SRC_ALPHA;
    GLenum inverse = (c == 1) ? GL_ONE_MINUS_DST_ALPHA : (c == 2) ? GL_ONE_MINUS_CONSTANT_ALPHA : GL_ONE_MINUS_SRC_ALPHA;
    if (k.ka == 0) { *factor = k.kb ? GL_ONE : GL_ZERO; return 0; }
    if (k.ka > 0) { *factor = k.kb ? GL_ONE : plain; return 0; }          /* C  (1+C se satura a 1) */
    if (k.kb) { *factor = inverse; return 0; }                           /* 1-C */
    *factor = plain; return 1;                                           /* -C  */
}
static void gs_alpha_decode(uint64_t reg, GsBlend* out)
{
    int a = (int)(reg & 3u), b = (int)((reg >> 2) & 3u), c = (int)((reg >> 4) & 3u), d = (int)((reg >> 6) & 3u);
    unsigned int fix = (unsigned int)((reg >> 32) & 0xFFu);
    GsCoef cs, cd;
    int neg_s, neg_d;
    if (c > 2) c = 0;
    cs.ka = (a == 0) - (b == 0); cs.kb = (d == 0);
    cd.ka = (a == 1) - (b == 1); cd.kb = (d == 1);
    neg_s = gs_coef_to_factor(cs, c, &out->src);
    neg_d = gs_coef_to_factor(cd, c, &out->dst);
    out->equation = GL_FUNC_ADD;
    if (neg_d && !neg_s) out->equation = GL_FUNC_SUBTRACT;          /* Cs*f - Cd*g */
    else if (neg_s && !neg_d) out->equation = GL_FUNC_REVERSE_SUBTRACT; /* Cd*g - Cs*f */
    out->fix = (float)fix / 128.0f;
}

static void blend_decode(const PcBlendEquation* eq, GsBlend* out)
{
    int a = eq->a, b = eq->b, c = eq->c, d = eq->d;
    GsCoef cs, cd;
    int neg_s, neg_d;
    if (c > 2) c = 0;
    cs.ka = (a == 0) - (b == 0); cs.kb = (d == 0);
    cd.ka = (a == 1) - (b == 1); cd.kb = (d == 1);
    neg_s = gs_coef_to_factor(cs, c, &out->src);
    neg_d = gs_coef_to_factor(cd, c, &out->dst);
    out->equation = GL_FUNC_ADD;
    if (neg_d && !neg_s) out->equation = GL_FUNC_SUBTRACT;          /* Cs*f - Cd*g */
    else if (neg_s && !neg_d) out->equation = GL_FUNC_REVERSE_SUBTRACT; /* Cd*g - Cs*f */
    out->fix = (float)eq->fix / 128.0f;
}

int main(void){
  unsigned long n=0,bad=0; int a,b,c,d,f;
  for(a=0;a<4;a++)for(b=0;b<4;b++)for(c=0;c<4;c++)for(d=0;d<4;d++)for(f=0;f<256;f++){
    uint64_t reg=(uint64_t)a|((uint64_t)b<<2)|((uint64_t)c<<4)|((uint64_t)d<<6)|((uint64_t)f<<32);
    PcBlendEquation e; GsBlend o,w; e.a=a;e.b=b;e.c=c;e.d=d;e.fix=f;
    memset(&o,0,sizeof o); memset(&w,0,sizeof w);
    gs_alpha_decode(reg,&o); blend_decode(&e,&w); n++;
    if(memcmp(&o,&w,sizeof o)) bad++;
  }
  printf("%lu casos, %lu diferencias\n",n,bad); return bad!=0;
}
