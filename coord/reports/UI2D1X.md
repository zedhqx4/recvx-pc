# UI 2D a 1x / escena 3D escalada

Rama: `experiment/ui-2d-1x`

## Implementacion

- `PcNativeDrawState.ui_layer` identifica explicitamente los draws procedentes de la ruta 2D nativa de `Ps2AddPrim`; no se usa `screen_space` como heuristica porque tambien identifica rutas 3D.
- El renderer mantiene el framebuffer principal a la resolucion escalada y crea un segundo framebuffer RGBA + profundidad a `output_width x output_height`.
- La capa UI se limpia transparente al inicio de cada frame.
- Los draws 2D marcados se envian al target 1x. Los transparentes conservan la cola OT y el estado capturado; al vaciarse, cada item selecciona su target.
- Al presentar, la capa UI se compone sobre la escena escalada con blending source-over y filtrado lineal. No se alteran UV, geometria, ni las dimensiones logicas del juego.
- El alfa del target UI se acumula con source-over para que las zonas transparentes permitan ver la escena inferior.

## Archivos

- `pc/include/pc_renderer.h`
- `pc/src/pc_renderer.c`
- `src/ps2/veronica/prog/ps2_dummy.c`

## Commits

- `0338c3c` [UI2D1X] Add explicit native-resolution UI draw state
- `7ecd781` [UI2D1X] Route native 2D primitives to the 1x layer
- `d63a29d` [UI2D1X] Render native 2D primitives into a 1x framebuffer
- `ef4bca7` [UI2D1X] Composite native-resolution UI over scaled scene
- `2519bc0` [UI2D1X] Preserve UI layer alpha while compositing

## Verificacion

- Comparacion GitHub `optimizacion...experiment/ui-2d-1x`: 5 commits, 3 archivos modificados.
- Revision estatica: se verifico la presencia de target 1x, viewport/scissor propios, enrutamiento de draws y OT, limpieza por frame y composicion final.
- No se ejecuto compilacion MSVC, CI ni el juego desde este entorno. No se afirma validacion visual.

## Prueba manual pendiente

1. Compilar la rama y arrancar con escala 1x, 2x y 3x.
2. Revisar menus, inventario, texto, iconos y transiciones; comparar especialmente UV de atlas entre escalas.
3. Revisar elementos 2D con transparencia, fades y paneles superpuestos.
4. Revisar escenas con render-to-texture, mapa y efectos 2D que deban quedar integrados con el mundo 3D. La capa marcada se compone sobre la escena, por lo que cualquier elemento de esa ruta que deba quedar ocluido por geometria 3D requerira una clasificacion mas especifica.
5. Confirmar que los overlays del renderer y las sombras siguen iguales.

