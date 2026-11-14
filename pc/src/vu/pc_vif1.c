/* PC port: VIF1 + enlace VU1 -> GIF.
 *
 * Reproduce lo que hacia el hardware con la cadena DMA que arma njCnkCsUvh (ps2_NinjaCnk.c):
 *   - tags DMA (cnt / ref / next / end) con codigos VIF en los datos,
 *   - BASE / OFFSET (doble buffer TOPS), UNPACK V4-32 (con FLG = sumar TOPS), MSCAL (arranca el microcodigo),
 *   - XGKICK: la VU1 manda un paquete GIF; se entrega a pcVu1GifSink (lo dibuja el renderer nativo).
 * El microcodigo (vu1_micro.c) esta generado desde vsm/ps2_vu1.vsm por pc/tools/vu_micro2c.py.
 */
#include <string.h>
#include "pc_vu.h"

typedef void (*PcVu1GifSinkFn)(const unsigned int* qwords, unsigned int max_qwords);

PcVu1GifSinkFn pcVu1GifSink = 0;   /* lo fija ps2_dummy.c */

static unsigned int vif_base, vif_offset, vif_tops;

void pcVif1Reset(void)
{
    vif_base = vif_offset = vif_tops = 0;
}

void pcVu1XgKick(PcVu1* v, int addr)
{
    if (pcVu1GifSink)
        pcVu1GifSink(&v->mem[addr].u[0], (unsigned int)(1024 - addr));
}

/* Interpreta un bloque de datos VIF de 'qwc' quadwords */
static void vif_run(const unsigned int* d, unsigned int qwc)
{
    unsigned int w = 0, nw = qwc * 4;

    while (w < nw)
    {
        unsigned int code = d[w++];
        unsigned int cmd = (code >> 24) & 0x7F, num = (code >> 16) & 0xFF, imm = code & 0xFFFF;

        if (cmd >= 0x60)
        {
            /* UNPACK: solo V4-32 (el unico que usa el juego), CL = WL = 1 */
            unsigned int addr = (imm & 0x3FF) + ((imm & 0x8000) ? vif_tops : 0), i;

            if (num == 0) num = 256;

            for (i = 0; i < num; i++)
            {
                memcpy(&pcVu1.mem[(addr + i) & 1023], d + w + i * 4, 16);
            }
            w += num * 4;
            w = (w + 0) ;
        }
        else if (cmd == 0x02) { vif_offset = imm & 0x3FF; vif_tops = vif_base; }   /* OFFSET: reinicia TOPS */
        else if (cmd == 0x03) { vif_base = imm & 0x3FF; }
        else if (cmd == 0x14 || cmd == 0x15 || cmd == 0x17)   /* MSCAL / MSCALF / MSCNT */
        {
            pcVu1.top = (int)vif_tops;
            pcVu1Micro((int)imm);
            if (vif_offset)
                vif_tops = vif_base + vif_offset - vif_tops;   /* alterna los dos buffers */
        }
        /* NOP, STCYCL, ITOP...: sin efecto aqui */
    }
}

/* Recorre una cadena DMA hacia VIF1 empezando en 'tag' (puntero del host) */
void pcVif1Dma(const void* tag)
{
    const unsigned int* t = (const unsigned int*)tag;
    int guard = 0;

    while (t && guard++ < 64)
    {
        unsigned int lo = t[0], qwc = lo & 0xFFFF, id = (lo >> 28) & 7;

        if (id == 1 || id == 7 || id == 2)   /* cnt / end / next: datos tras el tag */
            vif_run(t + 4, qwc);
        else
            break;                            /* ref/call/ret no los usa el juego */

        if (id == 7) break;
        t += 4 + qwc * 4;                     /* cnt: sigue tras los datos */
    }
}
