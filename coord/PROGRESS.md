# Registro de avance (lo mantiene Claude; leer esto primero al retomar)

Ultima actualizacion: 0.9.182 (shims muertos borrados; pendiente de probar compilacion CI). Base validada: 0.9.181.
Milestones: `milestone/0.9.181` (ultimo bueno), `0.9.180`, `0.9.171`... El hook "unpushed commits" es falso: comprobar con `git ls-remote origin optimizacion`.

## Hecho (resumen)
- 3d completo: pcVu0 eliminado; scissor/clip/color VU1 en C plano con estado propio (0.9.175-0.9.180).
- G1/G2/G3 de GPT integrados (coord/TASKS.md). Ramas PS2 retiradas (0.9.181, -6412 lineas).

## En curso (orden de trabajo acordado con el usuario)
1. Inventario de interruptores del INI / rutas paralelas -> decidir cuales colapsar.
2. Shims `sceGs*`/`sceDma*`/headers EE sin llamadores.
Cada paso: commit propio, version sube 1, notas en claude_notes.md, push, milestone si es riesgoso.

## Reglas
- Sin parches; referencia hasta validar; un commit por cambio revertible; no builds de diagnostico sueltos.
- Cambios sin efecto visual: validar con preprocesado (pc/tools/check_pp_equal.py) o harness gcc; los que tocan codigo vivo, harness o log del usuario.

## Registro cronologico
- 0.9.182 (2026-11-21): paso 2 parte 1 hecha: borrados 6 shims sin llamadores (ver claude_notes.md). Quedan con llamadores: sceGsSyncPath, Set/ExecLoadImage, sceDmaSend/Sync (ps2_dummy.c 3559-3572, evaluar si esas rutas son codigo muerto). Siguiente: paso 1 (INI) - pedir recvx.ini al usuario.
