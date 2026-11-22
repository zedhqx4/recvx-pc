#include "../../../ps2/veronica/prog/ps2_loadtim2.h"
#include "../../../ps2/veronica/prog/ps2_dummy.h"
#include "../../../ps2/veronica/prog/ps2_pxlconv.h"

//#include <string.h>

// 100% matching! 
u_long128* MakeRenderTexHeader(void* tex_adr)
{
    TIM2_FILEHEADER* fHead; 
    TIM2_PICTUREHEADER* pHead; 
    unsigned char* work;     
    unsigned int fsize;    
    unsigned int psize;      
    unsigned int loop;        
    TIM2_PICTUREHEADER_EX *temp; // not from the debugging symbols

    work = tex_adr;
    
    temp = tex_adr;
    
    temp->ClampFlag = temp->TpFlag = 0;
    
    fHead = (TIM2_FILEHEADER*)tex_adr;
    
    strcpy(fHead->FileId, "TIM2");
    
    fHead->FormatVersion = 4;
    fHead->FormatId = 1;
    
    fHead->Pictures = 1;
    
    for (loop = 0; loop < 8; loop++)
    {
        fHead->Reserved[loop] = 0;
    }
    
    if (fHead->FormatId != 0)
    {
        fsize = 128;
        psize = 128;
        
        work += fsize;
    } 
    else 
    {
        fsize = 16;
        psize = 48;
        
        work += fsize;
    }

    pHead = (TIM2_PICTUREHEADER*)work;
    
    pHead->TotalSize = (fsize + 1048576) + psize;
    pHead->ClutSize = 0;
    pHead->ImageSize = 1048576;
    pHead->HeaderSize = psize;
    
    pHead->ClutColors = 0;
    
    pHead->PictFormat = 0;
    
    pHead->MipMapTextures = 1;
    
    pHead->ClutType = 0;
    pHead->ImageType = 3;
    
    pHead->ImageWidth = 512;
    pHead->ImageHeight = 512;
    
    pHead->GsTex0.TFX = 0;
    pHead->GsTex0.TCC = 1;
    pHead->GsTex0.TW = 8;
    pHead->GsTex0.TH = 8;
    pHead->GsTex0.PSM = 0;
    pHead->GsTex0.TBW = 8;
    
    work += psize;
    
    return (u_long128*)work;
}

// 100% matching! 
/* En PC, StoreRenderTex copia su framebuffer directamente a una textura GL persistente ligada al payload TIM2.
 * La captura usa GPU->GPU y solo cae a readback completo por CPU si falla la ruta GL. */
void StoreRenderTex(void* tex_adr)
{
    sceGsStoreImage gs_simage; 
    int loop; 
    int l;
    u_long128* p; 
    u_long128* imagetop; 
    
    imagetop = MakeRenderTexHeader(tex_adr);
    
    l = 255;
    
    for (loop = 0; loop < 512; loop += l) 
    {
        p = &imagetop[loop * 128]; 
        
        if ((loop + l) > 512) 
        {
            l = 512 - loop;
        }
        
        sceGsSetDefStoreImage(&gs_simage, 0, 10, SCE_GS_PSMCT32, 0, loop, 512, l);
        
        FlushCache(0);
        
        sceGsExecStoreImage(&gs_simage, p);
    } 
    
    /* La GS limpia aqui el framebuffer (Ps2ScreenClear, sprite RGBA 0): lo que se dibuje despues parte de una pantalla vacia. Ps2ScreenClear
     * es un no-op en PC (limpiaba trabajo ya dibujado), asi que se limpia el frame explicitamente. */
    {
        extern void pcRendererClear(float r, float g, float b, float a, int clear_depth);

        pcRendererClear(0.0f, 0.0f, 0.0f, 0.0f, 1);
    }
}

// 100% matching!
void *UncAddr(void *val)
{
    return val;
}

// 99.83% matching
int Send_1024_Clut_data(void* clt_adr, unsigned long send_clut_adr)
{
    /* PC: la unica subida de 1024 entradas (njSetPaletteData -> CBP 0x3FF0) alimenta el CLUT residente que leen las texturas
     * PictFormat 5..8 (pc_texture.c). Ya no hay paquete DMA/GIF hacia la GS. */
    extern void pcTextureUploadVramClut(const unsigned int* entries);

    (void)send_clut_adr;
    pcTextureUploadVramClut((const unsigned int*)clt_adr);
    return 0;
}





#pragma divbyzerocheck off

// 100% matching!
void Ps2PxlconvCheck(void* timadr)
{
    TIM2_PICTUREHEADER* ph; 

    if (((unsigned char*)timadr)[5] == 0) 
    {
        ph = (TIM2_PICTUREHEADER*)((int)timadr + 16);
    }
    else 
    {
        ph = (TIM2_PICTUREHEADER*)((int)timadr + 128);
    }
    
    if (((ph->ImageWidth < 1024) && (ph->ImageHeight < 1024)) && ((ph->ImageWidth > 128) && (ph->ImageHeight > 128)) && ((ph->ImageType == 5) || (ph->ImageType == 4)))
    {
        Tim2_Format_Check(timadr); 
    }
}
