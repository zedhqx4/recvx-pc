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
- Siguiente tarea para GPT: G3 (inventario GS/GIF/VIF/EE). Los bloques `PcGpr`/asm2c ya no aparecen en `src/` (grep=0).

## G3 - Inventario de las capas de emulacion PS2 que quedan (GS, EE, GIF, VIF)  [abierta, SOLO ANALISIS]
Contexto: tras 0.9.180 ya no existe `pcVu0` ni emulacion de VU0/VU1. Queremos saber que queda de GS, EE, GIF y VIF en el
repositorio y en que orden se puede retirar, con los mismos resultados visuales. Rama de trabajo: `optimizacion-gpt`
(primero `git merge origin/optimizacion` para partir de 0.9.180). Alcance: NO modificar codigo fuente.
Entregable: `coord/reports/G3.md`. Un solo commit `[G3] ...` con el informe.

Que hay que inventariar (busca por nombres de funcion, tipos y macros, no solo por archivo):
  1. GS: registros/paquetes (`GS_*`, `sceGs*`, `GIF` tags, `PRIM`/`RGBAQ`/`ST`/`XYZ2`, `ulType` con bits de PRIM, TEX0/TEX1,
     ALPHA, TEST, FRAME/ZBUF), emulacion de memoria de video (`Ps2TexLoad`, `pcNativeTextureLoad`, CLUT, PSM).
  2. GIF/VIF: construccion de paquetes (DMA chains, `Ps2AddPrim*`, `Ps2_DRAW_TMP`, `vu1ScessorBuf`, `VU1_PRIM_BUF`), cualquier
     decodificador de paquetes que aun exista en `pc/src`.
  3. EE: capa de compatibilidad (tipos `u_long128`, `sce*`, DMA, scratchpad `SPR`, cache flush, `asm volatile` de EE,
     `PcGpr`/`A2C_*`/`pc_asm2c.h`).
  4. Cualquier otro modulo que simule hardware (timers, SPU/IOP si se ve de paso, solo listarlo).

Para cada elemento indica: (a) archivo:linea de definicion, (b) quien lo usa (archivo:linea, al menos 3 llamadores o "ninguno"),
(c) si se ejecuta en `TARGET_PC` (condiciones: macros, flags de INI, modos), (d) veredicto VIVA / MUERTA / DUDOSA con evidencia,
(e) a que otro elemento esta acoplado, (f) tamano aproximado en lineas.

Ademas:
  - Una tabla resumen por capa (GS, GIF/VIF, EE) con lineas totales, lineas VIVAS y lineas MUERTAS.
  - Un grafo de dependencias en texto (A -> B) entre subsistemas vivos.
  - Una propuesta de orden de retirada, de menor a mayor riesgo, con para cada paso: que se borra o reescribe, que prueba
    lo validaria (harness gcc contra la version actual, o log del juego) y que podria salir mal.
  - Lista de lo que NO debe tocarse (hace falta para que el juego funcione: dilo explicitamente).

Reglas: cuenta las lineas con `wc -l` o un script y pon el comando; cita archivo:linea en todo veredicto; si no puedes
comprobar algo (porque requiere compilar con el SDK o correr el juego), marcalo DUDOSA y ponlo bajo `## Preguntas`.
Recuerda las correcciones de G1: antes de declarar MUERTA una funcion, busca llamadores en TODO `src/` (incluidos
`itemview.c`, `ps2_NaDraw.c`, `ps2_NinjaCnk.c`) y verifica con la lista de usos de INI.
Terminado cuando: las cuatro areas estan cubiertas, la tabla resumen y la propuesta de orden existen, y las dudas estan
bajo `## Preguntas`.

## Correcciones de Claude al informe G3
- Conclusion de G3 confirmada: la build PC no emula GS/GIF/VIF/DMA; quedan shims de API (`sceGs*`, `sceDma*`, `FlushCache`), layouts/ABI y ramas PS2 sin compilar.
- Error: `vu1ScessorBuf` SI existe (`ps2_Vu1Strip.c:65`, usado en `ps2_Vu1Strip.c:306,309` y `ps2_Vu1Scissor2.c`); no es errata.
- Lineas "muertas" medidas con script (ramas `#else` de `#ifdef TARGET_PC` y cuerpos de `#ifndef TARGET_PC`, 283 113 lineas totales en src/ e include/): **5 334** (NaMatrix 1417, NinjaCnk 833, ps2_dummy 651, njplus 406, NaMath 350, Vu1Strip 343, face_bh 306, Scissor2 274, expand 196, objitm 148). Las estimaciones por rangos de G3 no se usan.
- Quedan 121 `asm volatile` en src/ (todas en ramas PS2 o rutas no PC; por verificar caso a caso al retirarlas).
- Los pasos 1-2 de su orden de retirada (headers EE, stubs inertes) quedan en espera de la decision del usuario sobre la pregunta 1 (conservar o no la compilacion PS2).

- Decision del usuario (G3, pregunta 1): se borran las ramas PS2. Hecho en 0.9.181 (-6412 lineas, preprocesado identico). Siguiente: shims `sceGs*`/`sceDma*` sin llamadores y limpieza de interruptores de INI.

## G4 - Uso vivo de macros/tipos GIF y GS en el codigo del juego (paso 3 de G3)  [abierta, SOLO ANALISIS]
Contexto: 0.9.192 validada. Queremos sustituir los macros `SCE_GIF_*`/`SCE_GS_*`/`GIF_REGLIST` y los tipos de paquete GIF por
estructuras/constantes propias del renderer (sin cambiar resultados). Antes hay que saber exactamente que se usa y para que.
Rama: `optimizacion-gpt` (primero `git merge origin/optimizacion`, parte de 0.9.192). NO modificar codigo fuente.
Entregable: `coord/reports/G4.md`, un solo commit `[G4] ...`.
Inventario (cita archivo:linea; recuenta con `git grep -c`):
  1. Todos los usos de `SCE_GIF_*`, `SCE_GS_*`, `GIF_REGLIST`, `SCE_GS_SET_*` en `src/` y `include/` (hay ~270 en ps2_dummy.c,
     52 en ps2_loadtim2.c, 26 en ps2_MovieFunc.c, 35 en types.h, 190 en pc/include/libgraph.h). Clasifica cada uso:
     (A) arma un paquete que luego NADIE interpreta en PC (la build PC no decodifica GIF) -> candidato a borrar;
     (B) el valor `prim`/`ulType`/bits SI se lee despues (p.ej. `prim & PC_GIF_PRIM_TME`, `(prim>>47)&7`, ABE, FST) -> hay que
         conservar la informacion, en que funcion se consume;
     (C) registros GS (TEX0/TEX1/ALPHA/TEST/FRAME/ZBUF/CLAMP) cuyo valor llega a pc_renderer/pc_texture -> listar el camino.
  2. Para cada grupo (B) y (C): la lista de campos que de verdad se leen (bits y funcion lectora) y una propuesta de struct
     explicita (nombres de campo, no bits) que lo sustituya. No escribas el struct en codigo, solo en el informe.
  3. Los `PC_GIF_PRIM_*` y `PcPrim3D`: donde se definen, quien los lee.
  4. Funciones que construyen buffers (`Ps2_DRAW_TMP`, `WORKBASE`, `p = (unsigned long*)...`) y son codigo inalcanzable tras un
     `return` temprano en PC (ejemplo: Ps2AddPrim3D tras `pcSubmitPrepared3D(...); return;`). Lista las funciones/bloques MUERTOS
     de ese tipo con lineas.
  5. Orden de retirada propuesto en pasos pequenos (1 commit cada uno), de menor a mayor riesgo, con la prueba para cada uno
     (preprocesado identico, harness, o log del usuario).
Reglas: como G1-G3 (busca llamadores en todo `src/`, marca DUDOSA lo que requiera compilar con el SDK, dudas bajo `## Preguntas`).
Terminado cuando: clasificacion A/B/C completa con conteos, lista de bloques muertos, propuesta de structs y orden de retirada.
