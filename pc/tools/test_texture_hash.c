#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define HASH_ROTL32(x, r) (((x) << (r)) | ((x) >> (32 - (r))))
static uint64_t hash_bytes(const unsigned char* p, size_t n)
{
    /* 4 acumuladores de 32 bits independientes (estilo xxHash32): el build es x86 de 32 bits, donde el multiplicar de 64 bits del FNV
     * anterior se expande en varias imul encadenadas y la latencia manda (~0.1 ms por enlace de textura en un FX-8350). Aqui las
     * cuatro cadenas avanzan en paralelo con imul de 32 bits. Es solo deteccion de cambios de contenido dentro del proceso
     * (el hash no se guarda en disco); el resultado de 64 bits son dos plegados distintos de los 4 acumuladores. */
    const uint32_t P1 = 0x9E3779B1u, P2 = 0x85EBCA77u, P3 = 0xC2B2AE3Du, P4 = 0x27D4EB2Fu, P5 = 0x165667B1u;
    uint32_t v0 = P1 + P2, v1 = P2, v2 = 0u, v3 = 0u - P1, a, b, w;
    size_t i = 0;
    for (; i + 16 <= n; i += 16) {
        memcpy(&w, p + i, 4);      v0 = HASH_ROTL32(v0 + w * P2, 13) * P1;
        memcpy(&w, p + i + 4, 4);  v1 = HASH_ROTL32(v1 + w * P2, 13) * P1;
        memcpy(&w, p + i + 8, 4);  v2 = HASH_ROTL32(v2 + w * P2, 13) * P1;
        memcpy(&w, p + i + 12, 4); v3 = HASH_ROTL32(v3 + w * P2, 13) * P1;
    }
    for (; i + 4 <= n; i += 4) { memcpy(&w, p + i, 4); v0 = HASH_ROTL32(v0 + w * P3, 17) * P4; }
    for (; i < n; i++) v1 = HASH_ROTL32(v1 + p[i] * P5, 11) * P1;
    a = HASH_ROTL32(v0, 1) + HASH_ROTL32(v1, 7) + HASH_ROTL32(v2, 12) + HASH_ROTL32(v3, 18) + (uint32_t)n;
    b = (v0 ^ HASH_ROTL32(v1, 9)) * P3 + (v2 ^ HASH_ROTL32(v3, 11)) * P4 + (uint32_t)(n >> 7);
    a ^= a >> 15; a *= P2; a ^= a >> 13; a *= P3; a ^= a >> 16;
    b ^= b >> 15; b *= P3; b ^= b >> 13; b *= P2; b ^= b >> 16;
    return ((uint64_t)b << 32) | a;
}
int main(void){
  enum{N=70000}; static unsigned char b[N], c[N]; long bad=0,tot=0;
  for(int i=0;i<N;i++) b[i]=(unsigned char)(rand()>>4);
  /* 1: cada byte cambiado por separado (tamaños con cola) */
  size_t sz[]={65536,65537,65539,64,1,15,16,17,4096};
  for(int s=0;s<9;s++){ size_t n=sz[s]; uint64_t h0=hash_bytes(b,n); for(size_t i=0;i<n;i+= (n>5000?13:1)){ unsigned char o=b[i]; b[i]=(unsigned char)(o+1); if(hash_bytes(b,n)==h0) bad++; tot++; b[i]=o; } }
  printf("cambio de 1 byte no detectado: %ld/%ld\n",bad,tot); bad=0;tot=0;
  /* 2: intercambio de dos bloques de 16 B distintos y de dos palabras */
  memcpy(c,b,N); for(int k=0;k<20000;k++){ size_t i=(rand()%4000)*16, j=(rand()%4000)*16; if(i==j||!memcmp(c+i,c+j,16)) continue; unsigned char t[16]; memcpy(t,c+i,16); memcpy(c+i,c+j,16); memcpy(c+j,t,16); if(hash_bytes(c,65536)==hash_bytes(b,65536)) bad++; tot++; memcpy(c,b,N);} 
  printf("intercambio de bloques no detectado: %ld/%ld\n",bad,tot); bad=0;tot=0;
  /* 3: mover 1 byte de sitio / desplazar contenido */
  for(int k=0;k<5000;k++){ memcpy(c,b+1,4096); memcpy(b+70000-4096,c,0); if(hash_bytes(b,4096)==hash_bytes(b+1,4096)) bad++; tot++; b[rand()%60000]^=0; }
  printf("desplazado 1 byte no detectado: %ld/%ld\n",bad,tot); bad=0;tot=0;
  /* 4: dos cambios que se podrian cancelar (+x en un sitio, -x en otro) */
  for(int k=0;k<200000;k++){ memcpy(c,b,65536); size_t i=rand()%65536, j=rand()%65536; if(i==j) continue; unsigned char x=(unsigned char)(1+rand()%255); c[i]=(unsigned char)(c[i]+x); c[j]=(unsigned char)(c[j]-x); if(hash_bytes(c,65536)==hash_bytes(b,65536)) bad++; tot++; }
  printf("+x/-x no detectado: %ld/%ld\n",bad,tot); bad=0;tot=0;
  /* 5: texturas casi vacias (ceros) con un pixel distinto */
  memset(c,0,N); uint64_t z=hash_bytes(c,65536); for(int i=0;i<65536;i+=3){ c[i]=1; if(hash_bytes(c,65536)==z) bad++; tot++; c[i]=0; }
  printf("1 pixel en textura vacia no detectado: %ld/%ld\n",bad,tot); return 0; }
