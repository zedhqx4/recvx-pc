# Cola de tareas (la mantiene Claude)

Estado: `abierta` | `en curso` | `entregada` (GPT escribio informe) | `integrada` | `descartada`

## G1 - Inventario de alcanzabilidad de ps2_Vu1Strip.c  [integrada; ver correcciones]
Objetivo: saber que codigo de `src/ps2/veronica/prog/ps2_Vu1Strip.c` (y sus llamadores) se ejecuta de verdad en la
build de PC y cual es codigo muerto, para decidir que se reescribe y que se borra en el paso 3d.
Alcance: NO modificar codigo. Entregable: `coord/reports/G1.md`.
Para cada una de estas funciones indica: (a) quien la llama (archivo:linea), (b) bajo que condiciones (macros,
`clipswitch`, modos, flags de INI), (c) si es alcanzable desde `vu1Draw*`/`NaDraw`, (d) veredicto: VIVA / MUERTA /
DUDOSA, con la evidencia.
  `ScissorTriangle`, `DrawScissorPolygonOpaque`, `DrawScissorPolygonTrans1P`, `PushTriangleNodeArray`,
  `_Clip_ViewVolume`, `_Clip_Screen`, `_Check_ClipViewAll`, `_Clip_ViewVolume2`, `_Get_ClipViewVolume2`,
  `_Get_ClipVolumePlane`, `InitNodeArraySet2`, y las rutas `vu1Draw*` que las usan.
Extra: lista los usos de `pcVu0` en cada funcion VIVA (cuenta y que registros: vf/vi/mem).
Terminado cuando: el informe cubre todas las funciones, con archivo:linea; las dudas estan bajo `## Preguntas`.

## G2 - Retirar rama `#else` muerta de Ps2TexLoad  [integrada en 0.9.174]
Archivo permitido: `src/ps2/veronica/prog/ps2_dummy.c`. La rama `#else` (estilo PS2) de `Ps2TexLoad` usa nombres de la
API `pcGs*` ya eliminada y no se compila en PC. Retirala dejando intacta la rama `TARGET_PC`.
Verifica: `gcc -fsyntax-only` del bloque (ver README) antes/despues y que `git diff` solo toque esa rama.
Un solo commit `[G2] ...`. Informe en `coord/reports/G2.md`.

## Correcciones de Claude al informe G1
- `_Clip_Screen` SI tiene llamador: `ps2_NaDraw.c:337` (`cflag &= _Clip_Screen(&scr.x)`). Es VIVA.
- Dudas resueltas por grep: `njCnkEasyMultiDrawObjectI` se llama desde `itemview.c:265,272` (visor de items) -> `vu1DrawTriangleStripTransDoubleI` y `DrawScissorPolygonOpaque` alcanzables; `_Init_ScissorSystem` en `ps2_NinjaCnk.c:220`; ruta 1P: `NaDraw.c:292 -> njDrawTexture3DExSetData1P -> njDrawTextureSub3D1P -> vu1DrawTriangleStripTransDouble1P (NaDraw.c:393)`.
- Conclusion: todo el scissor/clip viejo de ps2_Vu1Strip.c esta VIVO: se reescribe, no se borra.

## Estado 3d (0.9.180)
- `pcVu0` eliminado. Todo el scissor/clip/color de ps2_Vu1Strip.c y ps2_Vu1Scissor2.c es C plano con estado propio (pcClipRegs, pcScreen, pcCN).
- Pendiente de Claude: nada del 3d. Pendiente del usuario: probar 0.9.175..0.9.180 (ver notas).
- Candidatas para GPT (independientes, con harness): residuos de bloques `PcGpr`/asm2c en otros archivos (inventario por grep de `PcGpr`, `A2C_PTR`), sin tocar pc/src/vu.
