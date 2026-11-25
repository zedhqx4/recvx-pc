# Registro: UVs de la interfaz 2D (LEER PRIMERO al retomar este tema)

Ultima actualizacion: 2026-11-24. Version de codigo: 0.9.222 (rama optimizacion). Usuario prueba en el cuarto de agua / menus.

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
- [ ] 0.9.223 modulo `pc/src/pc_uvtable.c`: volcado `recvx_uiuv.tsv` de TODAS las piezas 2D texturizadas (FST y ST, sprites, cualquier n<=64, con Gindex y
      sin tope de 200) en la rafaga F8 + tabla de correcciones `ui_uv_table.txt` (INI `ui_uv_table`), clave = hash(gindex, tex, topologia, fst, n, x,y,u,v crudos).
- [ ] tools: `pc/tools/uiuv_dump_to_obj.py` (volcado -> OBJ+MTL con atlas), `pc/tools/uiuv_obj_to_table.py` (OBJ editado -> tabla), `pc/tools/test_uiuv_tools.py`.
- [ ] Usuario: F8 en cada pantalla problematica (opciones, terminal, inventario, mapa...) -> `recvx_uiuv.tsv` + `recvx.log` + volcado de texturas.
- [ ] Segun el volcado: si el fallo es sistematico (ST sin shift, multi-quad), corregirlo en el codigo y reducir la tabla a excepciones.
- [ ] Usuario edita UVs en Blender -> tabla -> el juego la carga. Validar a 1x y escala alta. Borrar pc_snap_uv, recorte, afinador F10 y claves ui_uv_crop_*.

## Reglas para no atascarse
- Los logs UIUV viejos ya estan analizados: no repetir.
- Cada paso termina en un commit con entregable; no mas de 3 comandos de exploracion seguidos sin escribir codigo o documentacion.
- Actualizar ESTE archivo al terminar cada paso.
