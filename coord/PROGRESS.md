# Registro de avance (lo mantiene Claude; leer esto primero al retomar)

Ultima actualizacion: 0.9.201 (sin probar). Base validada: 0.9.199. Hecho G4 pasos 5,6 + Ps2AddPrim/Ps2AddOT. Pendiente G4: Ps2_tp_tag/Ps2InitPS2_GS_SAVE tags, setImageTag (dudoso), pasos 1-4 (tipar PRIM/ALPHA/estados), 7.
Milestones: `milestone/0.9.181` (ultimo bueno), `0.9.180`, `0.9.171`... El hook "unpushed commits" es falso: comprobar con `git ls-remote origin optimizacion`.

## Hecho (resumen)
- 3d completo: pcVu0 eliminado; scissor/clip/color VU1 en C plano con estado propio (0.9.175-0.9.180).
- G1/G2/G3 de GPT integrados (coord/TASKS.md). Ramas PS2 retiradas (0.9.181, -6412 lineas).

## En curso (orden de trabajo acordado con el usuario)
1. Interruptores INI: HECHOS 0.9.183-0.9.191 (gl_shadow, gl_batch, rtt_blit_draw, tex_frame_memo, vu_trunc, gs_int, ui_snap_uv, ui_uv_crop_debug borrado, RECVX_CV_VN_BATCH). gpu_vertex HECHO 0.9.192 (solo se quita el interruptor; la conversion CPU sigue para sprites). Se quedan: rtt_gpu_copy, render_texture_scale, movies_hd, scale/vsync/etc (ajustes de usuario). Las claves vu0_native/floating_color/depth_float/vu1_color_native/ui_graphics_debug del INI del usuario ya no existen en el codigo.
2. Shims `sceGs*`/`sceDma*`/headers EE sin llamadores.
Cada paso: commit propio, version sube 1, notas en claude_notes.md, push, milestone si es riesgoso.

## Reglas
- Sin parches; referencia hasta validar; un commit por cambio revertible; no builds de diagnostico sueltos.
- Cambios sin efecto visual: validar con preprocesado (pc/tools/check_pp_equal.py) o harness gcc; los que tocan codigo vivo, harness o log del usuario.

## Registro cronologico
- 0.9.182 (2026-11-21): paso 2 parte 1 hecha: borrados 6 shims sin llamadores (ver claude_notes.md). Quedan con llamadores: sceGsSyncPath, Set/ExecLoadImage, sceDmaSend/Sync (ps2_dummy.c 3559-3572, evaluar si esas rutas son codigo muerto). Siguiente: paso 1 (INI) - pedir recvx.ini al usuario.
- 0.9.183-0.9.191 (2026-11-21): colapsados 8 interruptores INI + 1 env (un commit y version cada uno; detalle en claude_notes.md). Con el INI del usuario el comportamiento debe ser identico. Riesgo mayor: 0.9.188 (GLSL sin compilar aqui). Si algo falla, revertir ese commit.
- 0.9.192: gpu_vertex colapsado. Siguientes: rtt_gpu_copy (respaldo), revisar sprites->GPU para poder borrar la conversion CPU, luego G3 pasos 3-7.
- 0.9.192 validada por el usuario (2026-11-21). milestone/0.9.192.
- 2026-11-21: G3 paso 3 delegado a GPT como G4 (coord/TASKS.md, solo analisis -> coord/reports/G4.md). Al volver: `git fetch origin optimizacion-gpt:refs/remotes/origin/optimizacion-gpt`, leer G4.md, corregir, y ejecutar los pasos propuestos uno por uno.
- 0.9.193-0.9.196 (2026-11-21): G4 paso 5 (colas muertas + funciones huerfanas, ~-1700 lineas) hecho; sin validar por usuario ni CI. Siguiente segun G4.md: pasos de retirada de macros GIF (clases A/B/C), ver coord/reports/G4.md (rama origin/optimizacion-gpt).
- 0.9.197 validada (2026-11-21): CI ok tras restaurar Ps2BitCount; log sin errores. milestone/0.9.197. Siguiente: pasos de retirada de macros GIF segun coord/reports/G4.md (rama origin/optimizacion-gpt), clases A->B->C, con comprobacion de definiciones antes/despues en cada borrado.
- 0.9.200-0.9.201 (2026-11-21): Ps2AddPrim->Ps2AddPrim2D (clip_3d_on era constante 0), OT antiguo borrado. Probar CI + juego. Siguiente: tipar PRIM (G4 paso 1) con harness 2048 valores.
- 0.9.202 ok (usuario: funciona bien).
- 0.9.203-0.9.204 ok (usuario: funciona bien). G4 pasos 1,3,5,6 hechos. Quedan G4 2 (TEX0/CLAMP/TEST/ZBUF estado de textura/profundidad tipado), 4 (PC_GIF/ps2_MovieFunc paquete), 7.
- 0.9.208-0.9.209 ok (usuario: todo bien en 0.9.209).
- 0.9.211: borrados Ps2Vu*ProgSend, sceDma*, LoadImage (sin llamadores). Siguiente: reemplazar StoreImage por API tipada de captura, luego sceGsSyncV/SyncPath y poda de macros GS.
- 0.9.211 validada (log sin errores). 0.9.212: StoreImage -> pcTextureCaptureScreen (toca ruta viva: render textures; validar visualmente). Siguiente: sceGsSyncV/SyncPath, poda macros GS.
- 0.9.212 validada (log limpio, GPU-only capture 4x).
- 0.9.213: borrado pc_libgraph.c (shims inertes SyncPath etc). Siguiente: sceGsSyncV/Callback -> API vsync neutra; poda macros GS de headers.
- 0.9.213 validada (log limpio).
- 0.9.214: sceGsSyncV/Callback -> pcKernelWaitVblank/SetVblankCallback. Siguiente: poda de macros/tipos GS en libgraph.h, types.h, macros.h, sce_pc.h (sceGsDBuffDc sin uso etc).
- 0.9.214 validada (log limpio, 32k lineas). Siguiente: poda macros GS (types.h, macros.h, libgraph.h, sce_pc.h).
- 0.9.215: borrado libgraph.h; podados types.h/macros.h/sce_pc.h (macros GS/DMA/VIF/CD sin uso). Siguiente: Ps2ClearOT (15 llamadas), headers EE (eekernel/eeregs/libdma...), opcional renombrar sceVu0* tipos.
- 0.9.215 validada (log limpio).
- 0.9.216: Ps2ClearOT borrada. Siguiente: headers EE (eekernel/eeregs/libdma/...), opcional tipos sceVu0*.
- 0.9.216 validada (log limpio).
- 0.9.217: 21 headers EE de pega borrados (todos = sce_pc.h). Etapa GS/EE casi cerrada; queda opcional renombrar sceVu0*/sce* en sce_pc.h y revisar ADX/SFD (libs terceros).
- 0.9.217 validada (log limpio).
- 0.9.218: ui_uv_clamp (acotado UV 2D, estilo pc-port; INI, defecto 0). Probar ui_uv_clamp=1 con INI en blanco y con tus crop; milestone/0.9.217 es el punto seguro.
- 0.9.219: diag UIUV en rafaga F8 (sin cambio de comportamiento). 0.9.218 ui_uv_clamp NO corrige el menu: pendiente analizar UV/pixel con el log.
