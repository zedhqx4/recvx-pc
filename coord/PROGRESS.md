# Registro de avance (lo mantiene Claude; leer esto primero al retomar)

Ultima actualizacion: 0.9.191 (sin probar por el usuario/CI; probar que arranca y se ve igual; revisar log por 'shader' errores). Base validada: 0.9.181.
Milestones: `milestone/0.9.181` (ultimo bueno), `0.9.180`, `0.9.171`... El hook "unpushed commits" es falso: comprobar con `git ls-remote origin optimizacion`.

## Hecho (resumen)
- 3d completo: pcVu0 eliminado; scissor/clip/color VU1 en C plano con estado propio (0.9.175-0.9.180).
- G1/G2/G3 de GPT integrados (coord/TASKS.md). Ramas PS2 retiradas (0.9.181, -6412 lineas).

## En curso (orden de trabajo acordado con el usuario)
1. Interruptores INI: HECHOS 0.9.183-0.9.191 (gl_shadow, gl_batch, rtt_blit_draw, tex_frame_memo, vu_trunc, gs_int, ui_snap_uv, ui_uv_crop_debug borrado, RECVX_CV_VN_BATCH). PENDIENTE: gpu_vertex (usuario usa 1; borrar ruta CPU pcSubmitStrip3D en ps2_dummy.c ~1745-1990 + modo gpu_vertex=0 en pc_renderer.c:143; milestone antes). Se quedan: rtt_gpu_copy, render_texture_scale, movies_hd, scale/vsync/etc (ajustes de usuario). Las claves vu0_native/floating_color/depth_float/vu1_color_native/ui_graphics_debug del INI del usuario ya no existen en el codigo.
2. Shims `sceGs*`/`sceDma*`/headers EE sin llamadores.
Cada paso: commit propio, version sube 1, notas en claude_notes.md, push, milestone si es riesgoso.

## Reglas
- Sin parches; referencia hasta validar; un commit por cambio revertible; no builds de diagnostico sueltos.
- Cambios sin efecto visual: validar con preprocesado (pc/tools/check_pp_equal.py) o harness gcc; los que tocan codigo vivo, harness o log del usuario.

## Registro cronologico
- 0.9.182 (2026-11-21): paso 2 parte 1 hecha: borrados 6 shims sin llamadores (ver claude_notes.md). Quedan con llamadores: sceGsSyncPath, Set/ExecLoadImage, sceDmaSend/Sync (ps2_dummy.c 3559-3572, evaluar si esas rutas son codigo muerto). Siguiente: paso 1 (INI) - pedir recvx.ini al usuario.
- 0.9.183-0.9.191 (2026-11-21): colapsados 8 interruptores INI + 1 env (un commit y version cada uno; detalle en claude_notes.md). Con el INI del usuario el comportamiento debe ser identico. Riesgo mayor: 0.9.188 (GLSL sin compilar aqui). Si algo falla, revertir ese commit.
