# Corregir UVs de la interfaz 2D (guia para el sabado)

Todo es Python 3 estandar, sin instalar nada. En Windows usa `py` (o `python`) desde la raiz del repositorio.

## 0. Comprobar que las herramientas funcionan (10 segundos, sin el juego)
    py pc\tools\test_uiuv_tools.py
Debe terminar en `RESULTADO: TODO BIEN`. Sin gcc avisa de que omite la parte de C, es normal.

## 1. Capturar las piezas (en el juego, build 0.9.223 o mas nueva)
1. Entra a una pantalla con UVs mal (opciones, terminal Umbrella, inventario, mapa...).
2. Pulsa **F8** una vez. Repite en cada pantalla. Todo se acumula en `recvx_uiuv.tsv` (junto al exe), sin duplicados.
3. Para ver cada pieza sobre su atlas hacen falta los volcados de textura `recvx_tex_NN_g<Gindex>_f<fmt>.jpg` (los que ya generas); ponlos todos en una carpeta.
Mandame tambien `recvx.log` para revisar primero lo sistematico.

## 2. Volcado -> OBJ para Blender
    py pc\tools\uiuv_dump_to_obj.py recvx_uiuv.tsv menus.obj --atlas-dir C:\ruta\a\las\texturas
Crea `menus.obj` y `menus.mtl` (cada pieza = un objeto `pN_<clave>`, con el atlas de fondo). Opcional: `--gindex 2222001` para solo una textura.

## 3. Editar en Blender
- File > Import > Wavefront (.obj). Edit Mode > UV Editing: se ve el atlas y las UV de cada pieza.
- Mover SOLO las UV (no tocar los vertices). Las UV por defecto ya son las que usa el juego hoy.
- File > Export > Wavefront (.obj): marcar **Objects as OBJ Objects** y **UV Coordinates**; desmarcar **Triangulate Faces**. Guardar como `menus_editado.obj`.

## 4. OBJ editado -> tabla
    py pc\tools\uiuv_obj_to_table.py menus_editado.obj recvx_uiuv.tsv ui_uv_table.txt
Solo escribe las piezas cuyas UV cambiaste. Copia `ui_uv_table.txt` junto al exe del juego (el INI `ui_uv_table=0` la desactiva).

## Notas
- La tabla se aplica por la geometria CRUDA de cada pieza (textura, posicion y UV originales). Si el juego cambia una pieza, deja de coincidir y vuelve a lo normal.
- Una pieza recortada por el ajuste de pantalla ancha no se corrige con la tabla.
- Registro del trabajo y lo pendiente: `coord/UIUV.md`.
