# claude_notes.md — Revisión de la rama `render-nativo`

Revisión estática de los commits `dff3bdb` … `e65174d` (render nativo OpenGL 3.3 que reemplaza al emulador GS).

> **Alcance y límites.** Solo leí código. No pude ejecutar el juego (necesita la ISO, el build MSVC/Windows y una GPU),
> así que **no hay una causa raíz de la pantalla negra confirmada en runtime**. Lo marcado **[CONFIRMADO]** es un defecto
> verificable leyendo el código; lo marcado **[SOSPECHA]** hay que comprobarlo con el diagnóstico de la sección 2.
> En el código dejé comentarios `CLAUDE-NOTE[Nx]` que remiten a los puntos de este archivo. No cambié ninguna lógica.

---

## 1. Diagnóstico general

La idea es correcta (FBO 640x480 + shader que reproduce la proyección de la VU1 + blit al final), y el bucle sí funciona:
`pcVideoPresent()` → `pcRendererPresent()` → `SDL_GL_SwapWindow()` → `pcRendererBeginFrame()` corre cada frame.
Eso explica los **60 fps**: se está presentando un FBO que está negro (o limpio).

El problema de fondo es de método: ChatGPT sustituyó el **punto de salida** de los primitivos (VU1 / `Ps2AddPrim*`
→ `pcRenderer*`), pero el emulador GS aportaba además un **contrato implícito** que nadie reemplazó:

| Lo que daba el emulador GS | Qué pasa ahora |
|---|---|
| Ordering Table (OT) de 4096 buckets, se dibujaba en `Ps2DrawOTag` | Se dibuja **inmediato**, en orden de envío; `Ps2DrawOTag` solo vacía la cola de transparencias (N8) |
| Estado GS **por primitivo** (ALPHA, TEST, FOGCOL, SCISSOR, ZBUF) | Estado **global** que se lee **tarde**, al vaciar la cola de transparencias (N3) |
| Clipping de la VU1 (`ScissorTriangle`) y culling por `fNz` | Se omiten; el shader tampoco recorta bien contra el plano cercano (N5, N6) |
| `sceGsExecStoreImage` / render-to-texture | `sceGsExecStoreImage` es un no-op y `StoreRenderTex` aún llama a `Ps2ScreenClear` (N7) |
| Escala de fog GS 0..255 | Se interpreta con escalas distintas en cada ruta (N4) |
| Cualquier fallo era visible en el volcado `RECVX_GSDUMP` | No hay **ninguna** instrumentación: ni `glGetError`, ni contadores, ni `glReadPixels` (N1) |

---

## 2. Cómo encontrar la causa en minutos (hacer esto primero)

1. **Clear color magenta** en `pcRendererBeginFrame()` (y en `pcRendererClear` solo para probar).
   - Pantalla magenta → el blit funciona y **no se dibuja nada** (o algo lo limpia): ir a N2/N7/N9.
   - Sigue negra → se dibuja algo negro encima (fade/overlay, fog, textura negra) o el blit no usa el FBO correcto.
2. **Contadores por frame** (2D / VU1 / 3D preparado / transparentes encoladas) logueados cada 60 frames en `recvx.log`.
   Si son 0, el problema está **antes** del renderer (descartes silenciosos, N9). Si son > 0, está en el renderer.
3. `glReadPixels` de un píxel del FBO **antes** del blit y otro del framebuffer por defecto **después**.
4. `glGetError()` tras `draw_immediate`, `create_targets` y el blit (solo en `debug=1`).
5. Leer `recvx.log` buscando: `native texture: ...` (rechazos de textura), `native projection: ...` (valores de cámara
   absurdos), `native renderer: shader ...`.
6. Experimentos de aislamiento: forzar `uFogEnabled=0`; ignorar `depth_override_*`; saltar `pcRendererFlushTransparent()`.
7. RenderDoc / apitrace sobre un frame: muestra en 1 captura si hay draws, con qué estado y contra qué FBO.

---

## 3. Hallazgos

### N1 — No hay forma de ver qué pasa [CONFIRMADO]
`grep` no encuentra `glGetError` ni `glReadPixels` en `pc/src`. `RECVX_GSDUMP` y probablemente `RECVX_SHOTS` dependían del GS
eliminado. Antes de seguir portando hay que añadir observabilidad (sección 2).

### N2 — `Ps2ScreenClear` / `Ps2DispScreenClear` borran el frame a mitad de dibujo [CONFIRMADO + SOSPECHA de impacto]
En PC ambas llaman a `pcRendererClear()` **inmediatamente**. En PS2 esas funciones enviaban un paquete GIF también inmediato, pero
los primitivos estaban **diferidos en el OT** y se dibujaban después. Ahora el dibujo es inmediato, así que todo lo que se haya
dibujado antes del clear se pierde. Llamadores relevantes: `StoreRenderTex()` (`ps2_loadtim2.c`) y `PS2_Render_Tex_Sub()`
(`ps2_dummy.c`) terminan con `Ps2ScreenClear()`. Si algún camino del bucle principal llama a un clear después de dibujar, la
pantalla queda negra. **Candidato fuerte** para "negro a 60 fps".

### N3 — Estado diferido: las transparencias se dibujan con el estado de *después* [CONFIRMADO]
`NativeQueuedDraw` copia `PcNativeDrawState`, pero `apply_state()` lee **globales** en el momento del flush:
`blend_src/dst_*`, `projection_value`, `aspect`, `offset`, `near/far`, `global_fog_*`, `scissor_*`, `depth_override_*`.
Consecuencias: cualquier cambio de cámara, fog, blend mode o scissor entre el envío y `FlushTransparent()` altera cómo se
dibujan las transparencias ya encoladas. El juego cambia de cámara/pantalla varias veces por frame (espejo, mini-pantalla,
overlays), así que esto **no es teórico**. Un fade a negro con otro modo de blend puede acabar aplicándose a todo lo encolado.
Arreglo: guardar el estado completo (incluida proyección, fog, blend factors, scissor) dentro de `NativeQueuedDraw`.

### N4 — Escala de fog incorrecta y distinta en cada ruta [CONFIRMADO]
La tabla de fog (`fNaFogTbl`, `njCalcFogPower`) produce valores **0..255** (255 = sin niebla, 0 = niebla total).
- Ruta VU1 (`pcNativeSubmitVu1Strip`): `clamp(fFog, 0, 1)` sin dividir → casi siempre 1.0, la niebla prácticamente no existe.
- Ruta 3D preparada (`pcNativeSubmitPrepared3D`): `source[11] / 128` → escala equivocada (debería ser 255).
Debe ser `fog / 255.0f` en ambas. Además la niebla global se aplica a las transparencias con el estado de N3.

### N5 — El shader pierde la geometría que cruza el plano de cámara [CONFIRMADO]
`float z = max(aViewPosition.z, 0.00001)` y luego `gl_Position = vec4(n*z, depth*z, z)`. Un vértice con z ≤ 0 se **mueve** a
z=1e-5, así que un triángulo que cruza el plano de la cámara cambia de forma en lugar de recortarse. Además el camino VU1 saltó
`ScissorTriangle` (el recorte original). Solución estándar: no dividir en el vertex shader; calcular directamente coordenadas
de clip: `clip.xy = (x*proj*aspect.xy)*2/target + (offset*2/target - 1)*z`, `clip.z = A*z + B`, `clip.w = z`, y dejar que la GPU recorte.

### N6 — Sin culling de caras traseras [CONFIRMADO]
El original marcaba como no dibujable (`ulKick = 32768`) el triángulo cuando `0 < fNz` en sus tres vértices. La ruta nativa
fija `cull_backfaces = 0` e ignora `fNz`: se dibujan caras traseras (z-fighting, modelos "por dentro", sombras raras).
Hay que emular el criterio de `fNz` por triángulo o activar `GL_CULL_FACE` con el winding correcto (ojo con el flip de Y).

### N7 — Render-to-texture sin implementar [CONFIRMADO]
`sceGsExecStoreImage()` es un no-op (`pc_libgraph.c`) y `StoreRenderTex()` solo rellena una cabecera y limpia la pantalla. La
textura de render nunca existe en GL, así que `bhDrawFullScreenRenderTexture()` (`gm_flg & 0x100`: trampa de gas, agua) y
`bhDrawSmallScreenRenderTexture()` (`0x200`) dibujan nada o basura **y además borran el frame** (N2). Hace falta: copiar el FBO a
una textura GL (`glBlitFramebuffer`/`glCopyTexSubImage2D`) y asociarla al `NJS_TEXMEMLIST` de la textura de render.

### N8 — Se perdió el ordering table [CONFIRMADO]
`Ps2DrawOTag()` en PC solo hace `pcRendererFlushTransparent()`. Todo el orden de pintado de 2D/UI que dependía de la prioridad
del OT ahora es orden de llamada. Las transparencias se ordenan por `sort_depth`, pero eso mezcla **espacios distintos**:
z de vista en unidades de mundo (3D) contra profundidad 0..1 (screen-space/2D), con lo que UI translúcida y 3D translúcido se
ordenan arbitrariamente. Además las transparencias 2D (fades, paneles) se dibujan **después** de todo el 2D opaco aunque el
juego las enviara antes.

### N9 — Descartes silenciosos de primitivos con textura [SOSPECHA fuerte]
- `Ps2AddPrim`: si `textured && Ps2_now_tex == NULL` hace `return` sin log.
- `pcNativeTextureLoad` rechaza `TpFlag != 0`, `PictFormat` 5..8, cabeceras "raras", y deja `bind(0)`.
- `pcNativeSubmitPrepared3D`: `textured && texture == 0` → `return`. Las rutas 2D y VU1 en cambio dibujan **sin textura**
  (un cuadro plano con el color de vértice) cuando falta la textura.
Un filtro demasiado agresivo aquí vacía la escena o la rellena de colores planos. Revisar `recvx.log` para las líneas
`native texture:`; si salen para casi todo, esta es la causa.
Nota: la ruta original tenía un manejo especial de `TpFlag` (recargaba con `Ps2_tex_load_tp_cancel`); el nativo lo trata como error.

### N10 — Estado de profundidad global que se queda pegado [CONFIRMADO + SOSPECHA]
`Ps2ZbuffOff/Off2/On` ahora llaman a `pcRendererSetDepthMode()`, que fija un **override global** que tiene prioridad sobre el
estado de cada draw. `Ps2ZbuffOn()` fuerza `depth_write=1` incluso para primitivos transparentes (escriben profundidad y se
tapan entre sí) y `depth_test=1` incluso para 2D. En PS2 `Ps2DrawOTag` envolvía la OT con Off/On; en PC ya no lo hace, así que
el override depende de quién llame (`sub1.c` los llama sueltos). Mejor: que el override sea un parámetro de la pasada, no global.

### N11 — Defectos menores [CONFIRMADO]
- 2D (`Ps2AddPrim`, ruta no-sprite): `vertices[i].q` queda **sin inicializar** (malloc sin memset). Hoy no se usa porque
  `texture_perspective = 0`, pero es UB latente.
- El estado de combine de textura (`current_texture_function/alpha`) y la textura actual son globales y se copian al
  `PcNativeDrawState` en el momento del envío (bien), pero `pcRendererSetTextureCombine` recorta a 0..3.
- `pcRendererShutdown` no borra el VAO, el programa ni las texturas nativas.
- `draw_immediate` hace `malloc/free` y `glBufferData` por cada draw (cientos por frame): lento, no es la causa del negro.
- `pcRendererConfigureTexture` cambia parámetros de la textura **compartida** en cada `Ps2TexLoad` (el estado de sampler
  depende del último que cargó la textura).

### N12 — Lo que sí está bien (no tocar)
- La fórmula de proyección del shader coincide con la de la VU1 (`x*proj/z*aspect + offset`) y la conversión
  `offset - 1728/1808` es coherente con `njSetScreen` (`fNaViwOffsetX = cx + 1728`).
- Flip de Y y blit al final son correctos (el FBO se muestra derecho).
- `fog`/colores 0..255 → 0..1 y el factor `1.9921875` (=255/128) del shader son correctos para MODULATE/DECAL.
- Capa de carga dinámica de GL (`pc_gl.c`) y la creación del FBO están bien.

---

## 4. Orden de trabajo sugerido

1. Instrumentar (sección 2). Confirmar si hay draws y si el FBO sale magenta/negro.
2. N9: log de rechazos de textura; arreglar `TpFlag` y formatos hasta que haya 0 rechazos en el título.
3. N2/N7: quitar `Ps2ScreenClear()` de `StoreRenderTex`/`PS2_Render_Tex_Sub` y no limpiar a mitad de frame.
4. N3: guardar estado completo en `NativeQueuedDraw` (proyección, fog, blend, scissor, depth).
5. N4, N5, N6: fog /255, shader con clip-space correcto, culling con `fNz`.
6. N8: reintroducir un orden de pintado para 2D (capas por prioridad) y ordenar transparencias en un único espacio.
7. N7 completo: render-to-texture real.
8. Rendimiento: un VBO persistente / buffer mapeado en lugar de `malloc + glBufferData` por draw.
