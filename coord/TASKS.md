# Cola de tareas (la mantiene Claude)

Estado: `abierta` | `en curso` | `entregada` (GPT escribio informe) | `integrada` | `descartada`

## G1 - Inventario de alcanzabilidad de ps2_Vu1Strip.c  [abierta, SOLO ANALISIS]
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

## G2 - Retirar rama `#else` muerta de Ps2TexLoad  [abierta, CODIGO]
Archivo permitido: `src/ps2/veronica/prog/ps2_dummy.c`. La rama `#else` (estilo PS2) de `Ps2TexLoad` usa nombres de la
API `pcGs*` ya eliminada y no se compila en PC. Retirala dejando intacta la rama `TARGET_PC`.
Verifica: `gcc -fsyntax-only` del bloque (ver README) antes/despues y que `git diff` solo toque esa rama.
Un solo commit `[G2] ...`. Informe en `coord/reports/G2.md`.
