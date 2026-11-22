#include "../../../ps2/veronica/prog/ps2_NaSystem.h"
#include "../../../ps2/veronica/prog/ps2_dummy.h"
#include "../../../ps2/veronica/prog/ps2_loadtim2.h"
#include "../../../ps2/veronica/prog/main.h"
#include "pc_renderer.h"


PS2_NJ_SAVE Ps2_nj_save_current;

// 100% matching!
void	njSetTextureMemorySize(Uint32 size )
{

}

// 100% matching!
void	njSetVertexBuffer(Uint32 *buffer,Sint32 size)
{

}

// 100% matching!
void	njInitSystem( Int mode, Int frame, Int count )
{
    Ps2Init();
}

// 100% matching!
void	njInitVertexBuffer( Sint32 op, Sint32 om, Sint32 tp, Sint32 tm ,Sint32 pt)
{

}

// 100% matching!
void njWaitVSync(void) 
{

}

// 100% matching! 
void null_func()
{

}

// 100% matching! 
void Ps2InitFunc()
{ 
    EorFunc = null_func; 
    VsyncFunc = null_func; 
} 

// 100% matching! 
void 	njSetEORFunction( void (*func)(void) )
{
    if (func == NULL) 
    { 
        EorFunc = null_func; 
    } 
    else 
    {
        EorFunc = func; 
    }
} 

#pragma optimization_level 0

// 100% matching!
void Ps2SwapDBuff()
{
    Ps2DrawOTag();
    Ps2ClearOT();
    EorFunc();
    /* The emulated EE kernel (VBLANK interrupt, ADX file-server threads, IOP) only runs when the kernel is entered.
     * Without this wait nothing else advances and every file read stays pending forever. */
    while (Ps2_vcount < 2)
    {
        pcKernelBusyWait();
    }
    pcVideoPresent();
    Ps2_vcount = 0;
    VsyncFunc();
}

// 100% matching! 
int vsync_func(int cause)
{
    Ps2_vcount++; 
    
    if ((((sys->tk_flg & 0x80)) && ((sys->gm_flg & 0x80000000))) && (sys->time < 216000000)) 
    {
        sys->time++;
    }
    
    ExitHandler(); 
    return 0;
} 

#pragma optimization_level reset

// 100% matching! 
void Ps2SetVSyncCounter()
{
    DisableIntc(2);
    
    AddIntcHandler(2, vsync_func, 0);
    
    EnableIntc(2);
}

// 100% matching! 
void	njSetVSyncFunction( void (*func)(void) )
{
    Ps2_vcount = 0; 
    
    if (func == NULL)
    { 
        VsyncFunc = null_func; 
    }
    else 
    {
        VsyncFunc = func; 
    }
} 

// 100% matching!
void	njExitSystem( void )
{

}

// 100% matching!
void	njSetBackColor( Uint32 col1, Uint32 col2, Uint32 col3 ) 
{
    /* En la PS2 el color de fondo se guardaba en los sprites de limpieza del frame buffer (Db.clear0/1).
     * El renderer limpia el FBO al empezar cada frame con ese mismo color (el logo ADX y otras pantallas usan fondo blanco). */
    {
        float a = (float)((col1 >> 24) & 0xFF) / 128.0f;
        pcRendererSetClearColor((float)((col1 >> 16) & 0xFF) / 255.0f, (float)((col1 >> 8) & 0xFF) / 255.0f,
                                (float)(col1 & 0xFF) / 255.0f, a > 1.0f ? 1.0f : a);
    }
} 

// 100% matching! 
void	njTextureFilterMode( Int mode )
{
    Ps2_nj_save_current.mode = mode;
}

// 100% matching!
void    njPolygonCullingMode( Int mode )
{

}

// 99.91% matching 
void njColorBlendingModeSys(int s_mode, int d_mode) // this function is not on this KATANA release
{ 
    #define PC_BLEND_NONE { 0xFF, 0, 0, 0, 0 }   /* combinacion de modos no soportada */
    static const PcBlendEquation alpha_tbl[13][12] = 
    {
    	{
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    	},
    	{
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    	},
    	{
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		{ PC_BLEND_CS, PC_BLEND_ZERO, PC_BLEND_FIX, PC_BLEND_CD, 128 },
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    	},
    	{
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    	},
    	{
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    	},
    	{
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		{ PC_BLEND_ZERO, PC_BLEND_CD, PC_BLEND_FIX, PC_BLEND_CS, 16 },
    	},
    	{
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    	},
    	{
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    	},
    	{
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		{ PC_BLEND_CS, PC_BLEND_CD, PC_BLEND_AS, PC_BLEND_CD, 0 },
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		{ PC_BLEND_CS, PC_BLEND_CD, PC_BLEND_AS, PC_BLEND_CD, 0 },
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		{ PC_BLEND_CS, PC_BLEND_ZERO, PC_BLEND_AS, PC_BLEND_CD, 0 },
    		PC_BLEND_NONE,
    	},
    	{
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		{ PC_BLEND_CS, PC_BLEND_CD, PC_BLEND_AS, PC_BLEND_CD, 0 },
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		{ PC_BLEND_CS, PC_BLEND_ZERO, PC_BLEND_AS, PC_BLEND_CD, 0 },
    		PC_BLEND_NONE,
    	},
    	{
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    	},
    	{
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		{ PC_BLEND_ZERO, PC_BLEND_CD, PC_BLEND_AS, PC_BLEND_CD, 0 },
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		{ PC_BLEND_ZERO, PC_BLEND_CD, PC_BLEND_AS, PC_BLEND_CD, 0 },
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		{ PC_BLEND_ZERO, PC_BLEND_CD, PC_BLEND_AS, PC_BLEND_CD, 0 },
    		{ PC_BLEND_CS, PC_BLEND_CS, PC_BLEND_AS, PC_BLEND_CS, 0 },
    	},
    	{
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    		PC_BLEND_NONE,
    	},
    };
    PcBlendEquation alpha_value;
    
    Ps2_gs_save.mode_bk[0] = s_mode; 
    Ps2_gs_save.mode_bk[1] = d_mode; 
    
    Ps2_gs_save.set_last = Ps2_nj_save_current.set_last;
    
    Ps2_gs_save.dc_alpha = 0; 
    
    if (Ps2_gs_save.mode_bk[Ps2_gs_save.set_last] < 2) 
    { 
        if (Ps2_gs_save.mode_bk[Ps2_gs_save.set_last] == 0)
        { 
            alpha_value.a = PC_BLEND_CD; alpha_value.b = PC_BLEND_CS; alpha_value.c = PC_BLEND_AS; alpha_value.d = PC_BLEND_CS; alpha_value.fix = 0;   /* (Cd - Cs) * As + Cs */
        }
        else
        { 
            alpha_value.a = PC_BLEND_CS; alpha_value.b = PC_BLEND_CD; alpha_value.c = PC_BLEND_AS; alpha_value.d = PC_BLEND_CD; alpha_value.fix = 0;   /* (Cs - Cd) * As + Cd */
        }
    } 
    else if ((Ps2_gs_save.mode_bk[0] < 2) || (Ps2_gs_save.mode_bk[1] < 2)) 
    { 
        return;
    }
    else 
    {
        alpha_value = alpha_tbl[Ps2_gs_save.mode_bk[0]][Ps2_gs_save.mode_bk[1]]; 
    }
    


    if (alpha_value.a == 0xFF) 
    { 
        printf("CALL ISHIKAWA ! ALPHA ERROR !!!!!!! MODE %d %d", Ps2_gs_save.mode_bk[0], Ps2_gs_save.mode_bk[1]); 
        
        while (TRUE); 
    }
    
    pcRendererSetBlend(&alpha_value);
    return;
} 

// 100% matching! 
void    njColorBlendingMode( Int target, Int mode ) 
{
    Ps2_nj_save_current.mode_bk[target] = mode; 
    
    Ps2_nj_save_current.set_last = target; 
    
    Ps2_nj_save_current.dc_alpha = 0; 
    
    njColorBlendingModeSys(Ps2_nj_save_current.mode_bk[0], Ps2_nj_save_current.mode_bk[1]); 
}

// 100% matching!
void    njTextureShadingMode( Int mode )
{

}

// 100% matching! 
void    njSetCheapShadowMode( Int mode )
{

}

// 99.95% matching 
void	njUserClipping(Int mode ,NJS_POINT2 *p)
{
    if (mode == 0) {
        pcRendererSetScissor(0, 0, 0, 640, 480);
        return;
    }
    if (mode == 2 && p != NULL) {
        /* El original: SCISSOR(x0 = p0.x*32, x1 = p1.x*32 + 32, y0 = p0.y*32, y1 = p1.y*32 + 32), limites inclusivos en
         * pixeles. Los llamadores (visor de objetos, sp_evt) pasan las coordenadas divididas entre 32. */
        int x0 = (int)(p[0].x * 32.0f);
        int y0 = (int)(p[0].y * 32.0f);
        int x1 = (int)(p[1].x * 32.0f) + 32;
        int y1 = (int)(p[1].y * 32.0f) + 32;
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > 639) x1 = 639;
        if (y1 > 479) y1 = 479;
        pcRendererSetScissor(1, x0, y0, x1 - x0 + 1, y1 - y0 + 1);
        return;
    }
    pcRendererSetScissor(0, 0, 0, 640, 480);
    return;
}

// 100% matching!
void    njGetSystemAttr(NJS_SYS_ATTR *attr)
{
    *(PS2_NJ_SAVE*)attr = Ps2_nj_save_current;
}

// 100% matching!
void    njSetSystemAttr(NJS_SYS_ATTR *attr)
{ 
    Ps2_nj_save_current = *(PS2_NJ_SAVE*)attr; 
} 

// 100% matching! 
void    njChangeSystem( Int mode, Int frame, Int count )
{
    printf("ChangeSystem\n");
}

// 100% matching!
void	njAdjustDisplay(Sint32 xadjust,Sint32 yadjust )
{ 
    /* En la PS2 movia el origen de la pantalla (DISPLAY.DX/DY de la GS). El renderer nativo siempre presenta la imagen completa
     * centrada en la ventana, asi que el ajuste de pantalla del juego no tiene efecto en PC. */
    (void)xadjust;
    (void)yadjust;
}
// 100% matching!
void	njSetBorderColor(Uint32 color )
{

}
