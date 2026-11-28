# Registro: UVs de la interfaz 2D (LEER PRIMERO al retomar este tema)

Ultima actualizacion: 2026-11-24 (tarde). Version de codigo: 0.9.223 (rama optimizacion). Usuario prueba en el cuarto de agua / menus.

## Objetivo del usuario
UVs de las piezas 2D (menus, inventario, opciones, terminal Umbrella) correctas a 1x Y a escala alta, sin valores `ui_uv_crop_*` generales.
Decision del usuario: la solucion definitiva es reconstruir las piezas en un OBJ editable en Blender, corregir UVs a mano y exportarlas a una
tabla que el juego importe. "200 quads no son nada".

## Hechos establecidos (NO volver a analizar)
1. Convencion GS: el juego pone UV = texel + 0.5 y la GS evalua en el pixel ENTERO. GL evalua en el centro (+0.5). 0.9.220 `ui_uv_gs_sample=1`
   resta medio pixel (gradiente por primitiva). A 1x el inventario queda bien sin recorte manual.
2. 0.9.218 `ui_uv_clamp=1` acota el muestreo al rectangulo UV de CADA PRIMITIVA (bbox, medio texel dentro). Mejora el menu de Status, no otras pantallas.
3. Los 3 logs UIUV que tengo (76c34952, 8c7639f9, be02afd6) son TODOS del inventario: 200 piezas, todas n=4 FST, 197 con bordes UV .5/.5, 3 piezas
   (143,145,169: columnas de 8 px en x=540) con u 7.5 -> 0.0 espejadas y 7.5 de ancho UV para 8 px. Escala UV = pantalla salvo 141 y 194 (rellenos).
   Con estos datos NO se puede ver el fallo del menu de opciones (linea negra central, 2 islas) ni de la terminal (linea vertical en la barra inferior).
4. Limite del diagnostico actual (0.9.219): solo FST, num<=64, 200 piezas por rafaga, sin Gindex, sin sprites ni piezas ST (float). Insuficiente.
5. Posible causa en otras pantallas: (a) piezas ST (FST=0) no pasan por gs_sample ni clamp; (b) tiras con varios quads (n>4) usan un solo bbox para el
   clamp; (c) sprites. Sin datos no se sabe: HACE FALTA EL VOLCADO NUEVO.

## Plan (en orden; marcar al terminar)
- [x] 0.9.223 (HECHO, sin compilar con MSVC) modulo `pc/src/pc_uvtable.c`: volcado `recvx_uiuv.tsv` de TODAS las piezas 2D texturizadas (FST y ST, sprites, cualquier n<=64, con Gindex y
      sin tope de 200) en la rafaga F8 + tabla de correcciones `ui_uv_table.txt` (INI `ui_uv_table`), clave = hash(gindex, tex, topologia, fst, n, x,y,u,v crudos).
- [x] (HECHO, probado con gcc: `python3 pc/tools/test_uiuv_tools.py` -> TODO BIEN) tools: `pc/tools/uiuv_dump_to_obj.py` (volcado -> OBJ+MTL con atlas), `pc/tools/uiuv_obj_to_table.py` (OBJ editado -> tabla), `pc/tools/test_uiuv_tools.py`.
- [ ] Usuario: F8 en cada pantalla problematica (opciones, terminal, inventario, mapa...) -> `recvx_uiuv.tsv` + `recvx.log` + volcado de texturas.
- [ ] Segun el volcado: si el fallo es sistematico (ST sin shift, multi-quad), corregirlo en el codigo y reducir la tabla a excepciones.
- [ ] Usuario edita UVs en Blender -> tabla -> el juego la carga. Validar a 1x y escala alta. Borrar pc_snap_uv, recorte, afinador F10 y claves ui_uv_crop_*.

## Reglas para no atascarse
- Los logs UIUV viejos ya estan analizados: no repetir.
- Cada paso termina en un commit con entregable; no mas de 3 comandos de exploracion seguidos sin escribir codigo o documentacion.
- Actualizar ESTE archivo al terminar cada paso.

## Estado actual (0.9.223)
- Codigo: `pc/src/pc_uvtable.c` + `pc/include/pc_uvtable.h`; integracion en `Ps2AddPrim2D` (ps2_dummy.c): pieza cruda -> volcado (rafaga F8) -> tabla.
  Las piezas con entrada en la tabla usan sus UV finales y SIEMPRE se acotan (medio texel dentro de su bbox). INI nuevo: `ui_uv_table` (1 por defecto; 0 la ignora);
  archivo `ui_uv_table.txt` junto al exe (o env RECVX_UI_UV_TABLE). `ui_uv_clamp` ahora tambien acota piezas ST (FST=0).
- Flujo del usuario: F8 en cada pantalla -> `recvx_uiuv.tsv` (se acumula, sin duplicados) -> `uiuv_dump_to_obj.py recvx_uiuv.tsv m.obj --atlas-dir <carpeta con recvx_tex_*>`
  -> Blender (editar UV; exportar con "Objects as OBJ Objects" + UV, sin triangular) -> `uiuv_obj_to_table.py editado.obj recvx_uiuv.tsv ui_uv_table.txt` -> copiar junto al exe.
- PENDIENTE (usuario): compilar 0.9.223 en el CI; F8 en opciones, terminal Umbrella, inventario, mapa...; mandar `recvx_uiuv.tsv` + `recvx.log` (+ volcados de textura de los Gindex que salgan).
- PENDIENTE (Claude): con el volcado real, ver n/FST/ST de las piezas problematicas (hipotesis 5 de arriba) y corregir lo sistematico en codigo antes de editar a mano.

## Actualizacion 2026-11-25
- El usuario NO puede probar hasta el sabado (fuera de casa). Se reforzo `pc/tools/test_uiuv_tools.py` para que corra con solo Python 3 (sin gcc) y se escribio `pc/tools/UIUV_README.md`
  (guia paso a paso con comandos de Windows). Probado aqui con y sin gcc: TODO BIEN.
- Nada mas que hacer de mi lado hasta recibir `recvx_uiuv.tsv` + `recvx.log` (+ CI compilando 0.9.223). No tocar el codigo del juego antes de ver esos datos.

## Actualizacion 2026-11-27
- CI de 0.9.223 FALLO: C2632 en pc_uvtable.c (`long long`, el archivo no estaba en NATIVE_LONG de pc/tools/common.py). Corregido en 0.9.224 (solo se anade a la lista). Pendiente: que el CI compile 0.9.224.
