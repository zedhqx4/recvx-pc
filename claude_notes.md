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

## 3b. Correcciones ya aplicadas (sin poder ejecutar el juego: verificar en runtime)

| Nota | Qué se cambió |
|---|---|
| N1 | Contadores de draws por frame (log cada 120 frames: `native renderer: last frame draws=...`) y `RECVX_CLEAR_DEBUG=1` (clear magenta) |
| N2 | `Ps2ScreenClear()` ya no limpia el FBO en PC (el frame se limpia en `BeginFrame`) |
| N3 | Cada transparencia guarda proyección, fog, blend, scissor y override de profundidad y se dibuja con ese estado |
| N4 | Fog `/255` en las rutas VU1 y 3D preparada |
| N5 | Vertex shader en espacio de clip real (sin `max(z,…)`, sin división): la GPU recorta contra el plano cercano |
| N9 | `pcNativeTextureLoad` ya no rechaza por `TpFlag` (es una marca de VRAM de la PS2) |
| N10 | El override de `Ps2Zbuff*` ya no activa profundidad en 2D ni hace escribir profundidad a las transparencias |
| N11 | `q` inicializado en 2D; `Shutdown` borra el VAO |

**Siguen pendientes:** N6 (culling por `fNz`), N7 (render-to-texture real), N8 (orden de pintado / OT), y el resto de N9
(que el 3D preparado descarte sin log cuando falta la textura). La pantalla negra **no está verificada como resuelta**.

## 3c. Análisis del primer `recvx.log` (build con contadores)

- `native renderer: last frame draws=1 (2D=1) vertices=4 queued_blend=1` en **todos** los frames: el renderer recibe una única
  primitiva por frame, un quad 2D de 4 vértices con blend (un fade o panel). **El renderer no está recibiendo nada más**,
  así que el fallo está antes de él (el juego no envía, o `Ps2AddPrim` descarta).
- `tk 300002` constante durante ~1200 frames: el juego no sale de `bhSysCallWarning()` → `Adv_FirstWarningMessage()`
  (pantalla de advertencia / carga de datos de sistema / mensaje "Press START"). No es la pantalla de título ni gameplay.
- No hay líneas `native texture:` → ninguna textura fue rechazada (o `pcNativeTextureLoad` nunca se llamó).
- Frames de 4–11 s (`worst ...`) y el watchdog en `ZwFlushBuffersFile`: bloqueos de E/S de disco (probablemente el volcado de
  `last_run_input.txt` o la memory card en `saves`), independientes del render pero que agravan la sensación de cuelgue.
- Añadido para el próximo log: `adv: mode ...` (modo de la pantalla Adv), `Ps2AddPrim: prim=... tex=...` (muestreo) y
  `Ps2AddPrim DROP (...)` (razón de cada descarte), y `native draw #n` (estado de cada draw).

## 3d. CAUSA RAÍZ del "pantalla negra" (segundo log)

`adv: mode 2 ... active 1` constante en miles de frames: `Adv_FirstWarningMessage` esperaba (modo 2) a que terminara
`RequestReadInsideFile(SYSTEM.AFS)`, y **la lectura nunca terminaba**. No era un problema de render.

El commit `d00e17a` ("start and present through native OpenGL") reescribió `Ps2SwapDBuff` y `Ps2Init` y perdió:
- `while (Ps2_vcount < 2) pcKernelBusyWait();` — el kernel EE emulado (`pc_kernel.c`: fibers, VBLANK, hilos del servidor ADX, IOP)
  **solo se ejecuta cuando se entra en él**. Sin esta espera nunca corren el VBLANK ni los hilos de ADX y toda lectura de
  archivo queda pendiente para siempre.
- `Ps2InitTexCache()`, `Ps2InitPS2_GS_SAVE()`, `njColorBlendingMode(0,8)`, `njColorBlendingMode(1,6)`, `njUserClipping(0,NULL)`.

Restaurado. El único draw por frame era el quad de fade (alpha 0) de la pantalla de advertencia: todo el resto no se
dibujaba porque la pantalla ni siquiera había cargado sus datos.

## 3e. Tercer log (ya hay gameplay)

- El juego llega a `rom 4/9`, `gm 2`; el renderer dibuja hasta ~925 draws/frame con textura y profundidad. El render nativo funciona.
- 114 504 líneas `native texture: invalid/unsupported TIM2 header`: Gindex 100044 (76 834×), 101017, 107140, 101015 → son texturas con
  `PictFormat` 5..8 (las "no-VQ", ver `isVQ()` en `ps2_texture.c`), que el cargador nativo rechazaba. Con textura 0 los draws
  salían planos o se omitían (personajes/objetos). Además hay Gindex "basura" (1073941826, 2147683650…): esos tienen `FileId != "TIM2"`.
- Cambio: `PictFormat` 5..8 ya no se rechaza; se intenta decodificar como TIM2 normal y se loguea **una vez por Gindex** con
  ImageType/ClutType/tamaño/bank. Los rechazos reales (FileId/FormatId) también se loguean una sola vez, con el motivo.
  Si la decodificación sale mal (colores raros), esas líneas dicen qué formato es para escribir un decoder correcto.
- Frames de ~33 ms: el juego corre a 30 fps (espera `Ps2_vcount >= 2`), coherente con el original.

## 4. Orden de trabajo sugerido

1. Instrumentar (sección 2). Confirmar si hay draws y si el FBO sale magenta/negro.
2. N9: log de rechazos de textura; arreglar `TpFlag` y formatos hasta que haya 0 rechazos en el título.
3. N2/N7: quitar `Ps2ScreenClear()` de `StoreRenderTex`/`PS2_Render_Tex_Sub` y no limpiar a mitad de frame.
4. N3: guardar estado completo en `NativeQueuedDraw` (proyección, fog, blend, scissor, depth).
5. N4, N5, N6: fog /255, shader con clip-space correcto, culling con `fNz`.
6. N8: reintroducir un orden de pintado para 2D (capas por prioridad) y ordenar transparencias en un único espacio.
7. N7 completo: render-to-texture real.
8. Rendimiento: un VBO persistente / buffer mapeado en lugar de `malloc + glBufferData` por draw.

---

## 5. Auditoria GS -> OpenGL (que hacia la GS y que hace hoy el renderer nativo)

Objetivo acordado: el renderer debe reproducir el **comportamiento** de la GS (estado de registros + orden de pintado), no
parchear sintomas. Registros que el juego escribe realmente (conteo de `SCE_GS_SET_*` en `src/`): TEST_1/2 (25), TEX0_1/2 (18),
PRIM (18), RGBAQ (10), UV (8), FRAME_2 (8), ALPHA_2 (3), TRX* (3), ZBUF_1/2 (4), TEX1_1/2 (4), CLAMP_1 (2), PABE (2), SCISSOR (2), TEXA (1).

| Registro / mecanismo | Que hacia la GS | Estado en PC |
|---|---|---|
| TEST (ATE/ATST/AREF/AFAIL) | alpha test por pixel (GREATER 0 para mascaras; GEQUAL 128 en ZbuffOff; AFAIL_FB_ONLY) | **Parcial**: ahora GREATER~0 en 3D/2D. Falta GEQUAL 128 (+AFAIL) y DATE/DATM |
| TEST (ZTE/ZTST) + ZBUF.ZMSK | test de Z siempre activo, ZbuffOn/Off solo cambian la mascara de escritura | **Corregido** (commits 3fcac93, 4d6de8b) |
| Z de 2D | sz = f(pri) en el mismo espacio que el 3D | **Corregido** |
| OT (Ps2AddOT/Ps2DrawOTag) | transparentes a 4096 buckets por sz>>4, FIFO dentro del bucket, pintados con ZbuffOff | **Corregido** (cola por bucket) |
| ALPHA (A,B,C,D,FIX) | ecuacion de blend general | **Hecho** (`pcRendererSetGsAlpha`): (A-B)*C/128+D descompuesto en factores GL + SUBTRACT/REVERSE_SUBTRACT + glBlendColor para FIX |
| PABE | blend por pixel segun alfa MSB | **Falta** |
| TEX0 TFX/TCC | MODULATE/DECAL/HIGHLIGHT, alfa de textura | Parcial (modulate/decal con TCC) |
| TEX0 CLUT / CSM1 / bank | seleccion de paleta (CLD, CBP) | Parcial: 8 bits permutado; `bank` ignorado para 8 bits (hay texturas con 512 colores) |
| TEXA | expansion de alfa en formatos 16/24 bits (AEM, TA0/TA1) | **Falta** (solo se usa en el sistema de sombras) |
| TEX1 (MMAG/MMIN/L/K) + MIPTBP | filtro y mipmaps | Parcial: lineal/nearest, sin mipmaps |
| CLAMP | repeat/clamp/region-clamp (MINU/MAXU) | Parcial (region clamp no) |
| FOG/FOGCOL, FGE | niebla por vertice | Parcial (global fog) |
| SCISSOR | recorte | Implementado (njUserClipping). Las coordenadas llegan /32 (SCISSOR = p*32, x1/y1 = p*32+32, inclusivo): hasta 0.9.105 el PC las usaba como pixeles y recortaba casi todo el visor de objetos |
| TEST_1 por pasada del OT (Ps2ZbuffOffI / Off2) | Ps2DrawOTagI recorre el OT dos veces: alfa >= 128 con Z, despues alfa < 128 con ZMSK=1 | Hecho (`pcRendererFlushTransparentI`, uniform `uAlphaPass`). `Ps2DrawOTag` (Off): por primitivo, fragmentos con alfa >= 128 escriben color y Z y los de alfa < 128 solo color (FB_ONLY); la mitad translucida solo se dibuja si el primitivo puede tenerla (alfa de vertice < 1 o textura con algun texel de alfa < 128: los bordes filtrados de una mascara tienen alfa intermedio). Antes ningun primitivo con ABE escribia Z: lo dibujado despues y mas lejos (fondo del menu) lo tapaba y las caras traseras de los personajes se pintaban encima |
| Z-buffer PSMZ16 | Z de 16 bits (casi-coplanares dan el mismo Z, gana el ultimo) | Hecho en el fragment shader (`gl_FragDepth` cuantizado a 65535 niveles) |
| OT del 2D opaco | Todo 2D pasaba por el OT (bucket de Z, despues orden de envio) | `ot_queue`: el 2D opaco se encola y se pinta en el flush, ordenado igual que el ABE |
| **FRAME_2 + ZBUF_2 (contexto 2)** | **render a buffers fuera de pantalla** (FBP 150, 380, 476) | **Falta por completo** |
| Ps2ShadowStart/Draw/End/Main0/Main1 | sombras: se dibujan a un buffer 16 bits (FBP 380) y luego se aplican a la escena | **Falta** (sin rama PC): explica la sombra gris del personaje |
| PS2_Render_Tex_Sub (FRAME_2 476) | copia reducida de la pantalla a 256x256 usada como textura por efectos de pantalla | **Hecho** (`pcRendererCopyScreenToRenderTexture` + enlace en `Ps2TexLoad` con el flag). Pendiente: `sceGsExecStoreImage` (lectura de VRAM) |
| PS2_swap contexto 2 (FBP 150) | efectos de post-proceso al cambiar de buffer (fades/blur) | **Falta** |
| TRXPOS/TRXREG/TRXDIR + loadImage/sceGsExecLoadImage | subida/lectura de VRAM (texto dinamico, framebuffer->textura) | **No-op**: todo texto generado en runtime y todo render-to-texture queda vacio |

Conclusion: ya no quedan "parches de color"; lo que falta son **tres subsistemas completos** que escriben en contexto 2
(sombras, render-to-texture, post-proceso) y la **ecuacion de blend general** (ALPHA+PABE). Orden propuesto de trabajo:
1. ALPHA/PABE generales (reemplazar la tabla por la ecuacion de la GS con shader de blend en dos pasos / dual-source).
2. Framebuffers fuera de pantalla (FRAME_2/ZBUF_2) como FBO + `loadImage` de lectura (`sceGsExecStoreImage`).
3. Sombras sobre esa base.
4. Render-to-texture / post-proceso.
5. Texto dinamico (TRX* de subida).

## VU1 emulada (0.9.120)

- `vsm/ps2_vu1.vsm` -> `pc/src/vu/vu1_micro.c` (generado por `pc/tools/vu_micro2c.py`, igual que la VU0). `pcVu1Micro(addr)`: 0 = DoubleMain, 700 = SingleMain.
- `pc/src/vu/pc_vif1.c`: VIF1 (cadena DMA cnt/end, BASE/OFFSET con TOPS, UNPACK V4-32, MSCAL) y `XGKICK` -> `pcVu1GifSink`.
- `ps2_dummy.c::pcVu1GifPacket`: decodifica el paquete GIF PACKED (ST/RGBAQ/XYZF2) y lo manda por `pcSubmitVertices`, **inmediato** (sin OT): como el original, solo `TransDouble` pasaba por el EE + OT.
- `njCnkCsUvh` (ps2_NinjaCnk.c) usa la VU1 salvo `TransDouble[I]`. `RECVX_VU1=1` activa la VU1 (por defecto OFF desde 0.9.122: mismo resultado visual que el EE, mas lenta).
- `njCnkCs` (tiras sin textura) NO usa la VU1: son funciones C del EE (`vu1DrawTriangleStrip*`).
- Punto de retorno si algo se rompe: rama `pre-vu1-backup` (cf41cec, 0.9.116).

## Nativizacion de las tiras 3D (0.9.123)

Objetivo: quitar capas de compatibilidad PS2 (GS/VU1/VU0/EE/GIF) donde no aportan comportamiento.
- **Hecho**: las tiras de los modelos (`vu1DrawTriangleStrip*` -> `Ps2AddPrim3DEx/1P/I`) ya NO pasan por `pcPs2Prim3DCore` (empaquetado GIF,
  punto fijo 12.4, ADC por recorte), ni por polígonos de scissor, ni por `_Clip_ViewVolume2` (VU0). `pcSubmitStrip3D` (ps2_dummy.c) manda
  floats al renderer en pantalla homogenea (`screen_space = 2`, w = 1/Q): la GPU recorta cercano/lejano/lados; Z = Z16 de la GS (afin en Q);
  color y niebla `noperspective` (la GS los interpola lineal en pantalla), UV con perspectiva de la GPU.
- Se conserva (comportamiento del juego): kick por cara trasera (`ulKick`, Single), politicas de OT (promedio / por triangulo / fija).
- **REVERTIDO en 0.9.124**: `_Check_DisplayAreaPoint` y `_Clip_ViewVolume2` vuelven al original (iguales a 0.9.116). Desactivarlos rompio las
  escaleras de la pantalla de carga (mitad inferior cortada, tambien con RECVX_VU1=1): los poligonos que cruzan borde/plano cercano se
  descartaban en vez de recortarse. Hasta entender por que el recorte homogeneo no los cubre, el scissor del juego sigue activo.
- **Pendiente**: color (VU0_CALC_COLOR / pColorCalcFuncTbl: ojo, las 11 funciones EE y los programas VU0 difieren en modos 5, 7, 8 y 10),
  CALCPOINT (transformacion de vertices en VU0), el resto de Ps2AddPrim* (2D, NaGraphics3D) y retirar la VU1 emulada (vu1_micro.c, pc_vif1.c)
  cuando se verifique la ruta nativa.

- **0.9.125**: causa real de las escaleras de la carga: `pcSubmitStrip3D` omitio el filtro ADC del empaquetador original (`pcPs2Prim3DCore`:
  vertice fuera de ventana 4096 / rango Z => triangulo descartado, el juego dibuja los poligonos de scissor). Reproducido exacto en la ruta nativa.
  La 0.9.121 funcionaba porque tenia la VU1 por defecto; la ruta EE/OT (TransDouble) siempre uso este filtro.

- **0.9.126**: la causa de las escaleras (confirmada en el log del usuario: draws con z=-0.27 que no aparecian): `pcSubmitStrip3D` usaba el Z sin
  fijar, asi que la GPU recortaba lo cercano al plano cercano. El original fija sz a 0..65534 por vertice (aplasta, no recorta). Ahora
  usa `pcRendererDepthFromGsZ(zc)`, igual que `pcSubmitPrepared3D`. Nota: la premisa 'recorte homogeneo de Z exacto' era falsa por ese fijado.

- **0.9.127**: niebla en `pcSubmitPrepared3D` para todas las rutas (antes fog=1.0 salvo color flotante: Ps2AddPrim3D -> particulas njPtcl*,
  NaDraw, NaGraphics3D no recibian niebla aunque PRIM.FGE estuviera activo). Es lo que se ve como motas blancas brillantes en niebla.

- **0.9.128**: SPRITE (tipo 6, 2 vertices) en las rutas 3D preparadas. `pcAssemblePrepared` no tenia caso 6 y los sprites caian en 'lista de
  triangulos': vertices de particulas distintas se mezclaban (los 'palitos' de la tierra, la sangre al recibir dano). Ahora cada par es un
  rectangulo alineado (2 triangulos) con color/Z/niebla del 2o vertice y UV de esquina a esquina, como la GS. Origen: njPtclDrawPolygon/Sprite
  (prim 0x13/0x33/0x1B/0x3B -> tipo 6) via Ps2AddPrim3D.

- **0.9.129**: diagnostico `pcPrimDiag` (ps2_dummy.c): en los 2 primeros frames de la rafaga F8 (pcRendererBurstDiag) loguea cada llamada de
  Ps2AddPrim3D / Ps2AddPrim3DMod / Ps2AddPrim(2D) con PRIM decodificado (tipo, TME, ABE, FGE, FST) y los 4 primeros vertices crudos
  (stq, color, xyz, F). Sirve para identificar quien emite las particulas (palitos, sangre, llamas) y que niebla llega.

- **PENDIENTE (niebla, distinto de las particulas)**: en GL la niebla deja ver geometria lejana que en la GS queda tapada ("le falta thickness").
  No es brillo ni opacidad de efectos. Revisado: njCalcFogPower/Ex, copia de fFog a las tiras (rec[11]) y la mezcla del shader coinciden con la
  GS; falta comparar valores de F por vertice y FOGCOL con los de la GS (el diagnostico 0.9.129 imprime F). Dejado para despues.

- **0.9.130 (niebla)**: verificado en x86 que njGenerateFogTable3 + njCalcFogPower dan niebla lineal entre near y far (F=255 a near, 0 a far; difiere
  <1 de la formula lineal): la matematica del juego NO es la causa. Shader: la GS satura Cs (0..255) tras la funcion de textura y ANTES de la niebla;
  ahora tambien (antes Cs hasta 2.0 con vertice sobreexpuesto entraba sin saturar). Diagnosticos en la rafaga F8: `FOGDIAG` (estado, color, near/far,
  densidad y F a 12 distancias), linea `fog:` por draw (FGE, niebla global, color, rango de F de sus vertices) y `tiras ADC` (vertices fuera por
  z lejano / z cercano / xy). Hipotesis abierta: la GS no dibuja lo que pasa del plano lejano (ADC por z) y GL lo dibuja con FOGCOL.

- **0.9.131 (aritmetica entera de la GS en el shader)**: medido en las capturas de la escena de los barriles: la region totalmente nublada es (17,17,27)
  en el 100 % de los pixeles de la GS y (17,17,27)/(18,18,28) 58/41 % en GL; FOGCOL=(18,18,28) -> la GS hace `(F*Cs + (255-F)*FOGCOL) >> 8` con enteros
  (F=0 -> floor(255*18/256)=17; con F=255 resta 1). Ahora el shader reproduce en enteros de 8 bits: MODULATE `min((Ct*Cv)>>7, 255)` y la niebla
  `(F*Cs+(255-F)*FOGCOL)>>8`. Descartado con el log 0.9.130: densidad/estado de niebla (FOGDIAG coincide con la tabla del juego, F por vertice
  correcta, FGE/global bien), plano lejano (toda la escena tiene profundidad GL 0.93..0.992, nada se satura). PENDIENTE: la mezcla alfa de la GS
  (`(A-B)*C>>7 + D`, floor) sigue siendo mezcla fija en flotante de GL (redondeo al mas cercano): candidato a las barras de cinematicas que no son
  100 % negras; falta un F8 de una cinematica con barras para ver su alfa/ecuacion.

- **0.9.132**: `RECVX_GS_INT=0` desactiva la aritmetica entera de la GS (0.9.131) para comparar A/B contra la GS sin recompilar (log: 'aritmetica entera de la GS').
  Barras de cinematicas (log ff7ba64a): 4 draws negros, SRC_ALPHA/ONE_MINUS_SRC_ALPHA, Cs=0, alfa 0.95 (frame 1378) -> 0.99 (frame 1420): hacen fade-in,
  no estan fijas en 1.0. Con As=127/128 la GS da `Cd + ((0-Cd)*127 >> 7)` = floor(Cd/128) (0 para Cd<128) y GL redondea Cd*(1/128) al mas cercano
  (hasta 2 de 255): fuga <=1-2 LSB por el truncado de la mezcla alfa, que GL no puede hacer en funcion fija. Opciones: mezcla en shader leyendo el destino
  (copia del framebuffer, costosa) o dejarlo.

- **0.9.133 (motion blur opcional)**: `motion_blur=1` en recvx.ini (casilla en recvx-config.exe) y `motion_blur_strength=0..90` (defecto 50). Acumulacion temporal en
  `pcRendererPresent` (pc_renderer.c: blur_prepare/blur_apply): dos texturas RGBA8 ping-pong, salida = mix(actual, historia, k) con k=intensidad, y k cae a 0 donde
  la diferencia con la historia es enorme (cortes de camara sin fantasma). Solo afecta lo que se ve en la ventana: el Z, la niebla, la aritmetica de la GS y las
  capturas F8 quedan intactos. Limites conocidos: mezcla tambien la UI/menus (estela en texto que se mueve); como todo desenfoque por mezcla de frames, se nota
  mas cuanto mayor sea la tasa de presentado respecto a la del juego. Un desenfoque real por velocidad (solo 3D) exigiria vectores de movimiento por vertice.

## Milestone y rama de optimizacion (15 nov)

- `milestone/0.9.131` y tag `milestone-0.9.131` = commit 05f9663 (aritmetica entera de la GS). Punto estable al que volver; no se hizo revert destructivo: 0.9.132-0.9.134 siguen en `render-nativo` (3c85081).
- `optimizacion` parte de 3c85081 (0.9.134). `render-nativo` queda como estaba.
- Motion blur: ya es `motion_blur=0` por defecto (pc_renderer.c, recvx_config.c quita la clave al desmarcar). El log 492e3171 dice "motion blur desactivado".
- Log 492e3171 (blur apagado): todos los tramos a 33.4 ms (tope de 30 fps), GPU esperando 0.1 ms. El coste esta en CPU: `draw-model` hasta 23/35 ms de media/max en la sala con agua (room geometry 10.3 ms, water 6.2 ms, CNK plist 9.8 ms con 113 chunks/frame). Los 24 fps del blur son esos ~41 ms: CPU ya al limite + pasada extra de pantalla completa.
- Objetivos de optimizacion: (1) CNK plist/room geometry, (2) agua, (3) blur a media resolucion y sin copias extra.

### 0.9.135 (rama `optimizacion`): primeros pasos hacia quitar VU0
- Hallazgo: la ruta por defecto de njCnkCsUvh/Uvn (RECVX_VU1=0) es la del EE: copia por vertice + `_CVV` (recorte) + `pVu1Func` (vu1Draw*, ~3500 lineas de asm2c sobre `pcVu0.vf`). El bucle traducido de la ruta VU1 solo corre con RECVX_VU1=1.
- `_CVV` nativo: x*M0+y*M1, +z*M2, +M3 con `vu_f` y los 6 bits de recorte, sin `vu_clip`/historial de banderas (solo se leen los 6 bits bajos). Mismo resultado.
- `pcVu0StripPack` (vu0_calcpoint.c): empaquetado nativo de tiras (UV 12.4->float, Z de la GS fijado a 0..65534) que sustituye al bucle traducido en las dos rutas VU1.
- No verificado en ejecucion (solo revision de codigo; sin compilar con MSVC). Si algo cambia visualmente, volver a `milestone/0.9.131` o revertir este commit.
- Siguiente: nativizar `vu1Draw*`/`pColorCalcFuncTbl` (vf24-27 como struct C estatica en vez de `pcVu0.vf`), luego retirar VU1 emulada.
- Sobre el EE: no hay que "nativizarlo" aparte; el juego ya corre como C en x86. Lo que sobra son las capas (`PcGpr`, `A2C_*`, `a2c_ldl/ldr`) de las zonas traducidas de asm; se eliminan al nativizar cada funcion.

### Medicion 0.9.135 (log 26fbbd70, perfil activo con debug=1 + profile_game=1)
- plist por chunk CNK: 31-93 us (antes de 0.9.135: 28-95 us). `_CVV` nativo no cambia nada medible: NO era el cuello.
- Cuello real (por lectura de codigo): por cada vertice de tira, `vu1DrawTriangleStrip*` llama a `pFunc` (vu1GetVertexColor*), `_Clip_ViewVolume2` (pcVu0Micro 269, 2 vertices) / `_Check_ClipViewAll` (pcVu0Micro 237, el resto) y `_Get_ClipViewVolume2`; ~1 us/vertice con ~60 vertices por chunk.
- OJO al nativizar 237: `VU0_CLIP_VIEW_VOLUME_ALL` hace `ILW vi01,187(vi00); JALR vi15,vi01`, es decir salta a la rutina de color VU0 elegida en tiempo de ejecucion (`vu0GetVertexColor*`, escrita en mem[187] por la rutina de init de 0x0C48-0C58 con la tabla en mem[176+modo]) y vuelve por `JR vi15` a 0x07D0. Es recorte + iluminacion en una sola llamada. Hay que nativizar la familia vu0GetVertexColor* a la vez (pendiente "colour calc VU0_CALC_COLOR"), y verificar vi02 (plano) con los FCOR: constantes 0xDF7DF, 0xEFBEF, 0x7DF7, 0xBEFB, 0xDF7D, 0xEFBE sobre el clip visto (|imm)&0xFFFFFF==0xFFFFFF.
- 269 (VU0_CLIP_VIEW_VOLUME) es sencillo (vf19<-vf18<-vf15; vf15 = M*vf14; 3 CLIPw) pero solo afecta a 2 vertices por tira: ganancia pequena.

### 0.9.136 (rama `optimizacion`): VU0 por vertice nativo, verificado
- `pc/src/vu/vu0_native.c`: `VU0_CLIP_VIEW_VOLUME_ALL` (237), `VU0_CLIP_VIEW_VOLUME` (269) y `VU0_CALC_COLOR` (394) en C, con las 11 rutinas `vu0GetVertexColor*` (el salto indirecto de mem[187] se resuelve buscando el valor en la tabla mem[176..186]; si la tabla no esta inicializada cae al interprete). `pcVu0Micro` (generado por vu_micro2c.py, que ya emite el gancho) llama primero a `pcVu0NativeMicro`.
- Reglas de float iguales al interprete: un redondeo por operacion, `vu_f` (denormal->0, inf/NaN->max), MINI/MAX con comparacion explicita (max(-0,+0)=+0 como la VU), ACC por etapas, huecos de retardo de JR/JALR, FCOR con las seis constantes, vi01/vi02/vi15, CLIP de 24 bits.
- Verificacion hecha aqui (no hace falta correr el juego): `pc/tools/test_vu0_native.c`, 200.000 casos aleatorios (denormales, inf, ceros) en los 3 microprogramas x 11 modos: 0 diferencias en vf, ACC, vi, CLIP e I (solo se tolera el signo de cero), con redondeo normal y con truncacion. Microbenchmark gcc -O2: 1.9-2.5x mas rapido por llamada (63->25 ns en Dif, 310->167 ns en DifSpe3).
- Interruptor: `RECVX_VU0_NATIVE=0` interprete, `1` nativo (defecto), `2` A/B en el juego (corre las dos y escribe `vu0 native MISMATCH ...` en recvx.log).
- Hallazgo sobre redondeo: la FPU del EE y las FMAC de la VU truncan hacia cero y vacian denormales; el port usa redondeo al mas cercano (los dos caminos, interprete y nativo). `vu_trunc=1` en recvx.ini (o `RECVX_VU_TRUNC=1`) pone MXCSR en RC=11+FTZ+DAZ SOLO dentro de las funciones `vu1Draw*` y lo restaura antes de enviar a GL. Apagado por defecto hasta compararlo visualmente (puede mover 1 LSB en colores/niebla; es lo que haria el hardware).
- Perfil nuevo `PROFILE vu1Draw <tipo>: ... us/vertex total / submit / color+clip+rest` cada 4000 tiras (debug=1, profile_game=1). El microbenchmark indica que VU0 es solo ~60-300 ns de ~1 us por vertice; este log dira donde esta el resto (submit vs. traduccion asm2c de vu1Draw*).
- Sin compilar con MSVC; solo gcc sobre vu0_native.c/vu0_micro.c y revision de ps2_Vu1Strip.c.

### 0.9.137: truncar como el hardware, de punta a punta (rama `optimizacion`)
Principio: EE/VU/GS truncan hacia cero y nunca redondean al mas cercano; el port debe dar el mismo entero en cada paso.
- Modo de redondeo VU/EE ACTIVO por defecto (`vu_trunc=0` o `RECVX_VU_TRUNC=0` para comparar): MXCSR RC=trunc + FTZ + DAZ mientras corre matematica de VU: `pcVu0Micro` (generado: el gancho esta en vu_micro2c.py), `pcVu0StripClip`, `pcVu0CalcPointN`, `pcVu0StripPack`, `_CVV` y las cuatro `vu1Draw*`; se restaura antes de enviar a GL/SDL. NO cubierto aun: la matematica del EE fuera de esas rutas (animacion, camara, matrices del juego). Hacerlo global exige quitar el modo alrededor de cada llamada a GL/SDL/driver (riesgo de conversiones raras en el driver); queda como paso aparte.
- CPU (pcSubmitStrip3D / pcSubmitPrepared3D): color RGBA y F de niebla = FTOI0 (floor tras saturar 0..255) en vez de pasar el float; Z entero (floor); XY a 1/16 px con floor (FTOI4 / XYZ2 12.4). Antes pasaban fraccionarios y el shader los redondeaba al mas cercano. Recorte propio de sprites: la interpolacion de color ya no suma 0.5.
- Shader (modo `uGsInt`): `floor(x*255+0.5)` -> `floor(x*255+0.001)` para color interpolado, textura, F y FOGCOL (el 0.001 solo absorbe el error del float al reconstruir enteros exactos, no redondea); Z de 16 bits: `floor(... + 0.01)` en lugar de +0.5.
- Sin tocar y pendiente: mezcla alfa (la hace GL con redondeo; la GS trunca con >>7, hay que hacerla en shader con copia del framebuffer), comparacion de alfa en enteros 0..255, `ps2_dummy.c` ~500: snapping de UV de sprites (heuristica propia, no matematica GS), division S/Q.

### 0.9.138: criterio por operacion (no "truncar todo")
Regla: cada paso hace lo que hace el hardware. Truncar: FMAC de VU/FPU del EE (chop), FTOI0/FTOI4, MODULATE `>>7`, niebla `>>8`, mezcla `>>7`, Z entero, interpolacion de color/F/Z en la GS. Redondear: solo donde el original redondea (hay que comprobarlo en el codigo del juego, p. ej. sumas de 0.5 antes de FTOI). Natural (float sin tocar): ST/Q, calculos propios del port que no existen en la GS (vertices sinteticos del recorte propio, escalado de ventana, viewport). Revertido: el `+0.5` en la interpolacion de color del recorte propio de sprites (no es hardware). Por verificar contra el codigo/hardware antes de darlo por bueno: truncado del resultado del filtro bilineal, y cuantizacion de F interpolado.

### 0.9.139: opciones tambien en recvx.ini
Las opciones de depuracion solo existian como variable de entorno (`getenv`), incomodas en Windows. Ahora el entorno manda y, si no existe, se lee `recvx.ini`: `vu1=1` (RECVX_VU1), `vu0_native=0|1|2` (RECVX_VU0_NATIVE), `gs_int=0|1` (RECVX_GS_INT), `vu_trunc=0|1` (ya leia el ini). Siguen solo por entorno: RECVX_VU0_BATCH, RECVX_CV_VN_BATCH, RECVX_FRAME_DUMP y las de volcado/diagnostico.

### 0.9.140: lo que dicen los logs vu0_native=2 y =1 (usuario, 16 nov)
- A/B (`vu0_native=2`): 0 `vu0 native MISMATCH` sobre ~6.8 millones de vertices reales (OpaqueDouble/TransDouble) con truncacion activa: el VU0 nativo da el mismo estado que el interprete en datos de juego. Con `vu0_native=1` va a 33,4 ms (tope 30 fps), ventanas de hasta 38 ms en 5 de 59.
- Perfil vu1Draw (nativo): ~0.95 us/vertice, de los que 0.63 son el envio (`Ps2AddPrim3DEx`: 5 malloc/free + `draw_immediate`, un draw call por tira de ~6 vertices, ~550 tiras/frame = ~2 ms/frame) y 0.31 color+recorte+resto (~1 ms/frame). Total vu1Draw ~3 ms/frame; el tipo 8 de CNK cuesta ~12 ms/frame, o sea ~9 ms estan FUERA de vu1Draw: bucle por vertice de la ruta del EE (`copia 48 B + _CVV`) sobre tiras que luego se descartan por frustum.
- 0.9.137 puso LDMXCSR (enter/leave del modo de redondeo) dentro de `_CVV`, es decir POR VERTICE: caro. Ahora se hace una vez por chunk (bucle por defecto de njCnkCsUvh/Uvn).
- Perfil nuevo `PROFILE cnk strips`: tiras vistas / descartadas por frustum / vertices por _CVV. Si el % descartado es alto, el siguiente paso es el descarte por chunk (caja envolvente) antes de tocar vertices.

## 0.9.141 (rama optimizacion): coste de envio por tira
Medicion previa (escena pesada): ~11.9us/tira, ~1.6us/vertice en el envio, ~527 tiras/frame, tiras de ~6 vertices => un draw GL por tira con 15-20 llamadas GL de estado y 4-5 malloc/free.
- `apply_state()` ahora lleva una sombra del estado GL (FBO, viewport, prueba/mascara de profundidad, mezcla, culling, scissor, programa) y solo emite lo que cambia. Las funciones que tocan estado fuera de apply_state (clear, sombras, copias, lectura, desenfoque, present, creacion de targets) invalidan la sombra. `gl_shadow=0` en recvx.ini la desactiva para comparar. Resultado de imagen identico: solo se omiten llamadas redundantes.
- ps2_dummy.c: buffers de trabajo reutilizados (`pcScratch`) en lugar de malloc/free por tira.
- Pendiente: medir con debug=1 + profile_game=1 (PROFILE vu1Draw submit us/vertice; antes 0.63 y 1.6); despues, agrupar tiras contiguas compatibles.

## 0.9.142 (optimizacion): agrupacion de draws inmediatos
Log 038a2a65 (0.9.141): el coste de envio NO bajo (1.6us/vertice, ~3950 tiras por ventana). Sombra de estado + buffers no eran el cuello: queda el glBufferSubData+glDrawArrays por tira de ~6 vertices (llamadas de driver) -> se agrupan.
- `draw_immediate` acumula draws consecutivos TRIANGLE_STRIP/TRIANGLES con estado identico (memcmp de PcNativeDrawState + destino) y los emite en un draw; las tiras se unen con vertices repetidos (degenerados) y paridad par, asi cada triangulo conserva orden y sentido (verificado con harness /tmp: 20000 casos aleatorios, 0 diferencias). Mismo orden de envio => misma profundidad/mezcla por pixel.
- Se vacia el lote antes de cualquier cambio de estado global o GL (setters, sombras, copias, texturas, clear, present) y durante el vaciado del OT no se agrupa (`batch_lock`). `gl_batch=0` en recvx.ini lo apaga.
- El log "last frame draws=N (GL batches=M)" compara draws logicos contra draws GL reales.
- Dato nuevo del log: en la sala del gas "render-texture" (17.6 ms) es bhAllDrawModel dentro de bhDrawFullScreenRenderTexture, no una copia: es la misma geometria. plist de sala 15 ms/frame (123us/chunk).

## 0.9.143: el redondeo de la VU ya no envuelve el envio a GL
Reporte (agua): 0.9.131 ~29 fps, 0.9.141 cae a ~20 fps. Revision del codigo:
- DEFECTO: en 0.9.140 puse pcVuRoundEnter() una vez por chunk alrededor de TODO el bucle de tiras de njCnkCsUvh/Uvn. Como los enter de vu1Draw* anidaban sobre ese, su "leave" restauraba el estado YA truncado: Ps2AddPrim3DEx, el renderer y el driver de GL corrian con RC=truncar + FTZ + DAZ. Contradice el invariante de pc_vu.h ("solo mientras corre matematica de VU; el driver de GL, SDL y el resto siguen con el redondeo normal").
- Ahora el enter/leave envuelve solo el bucle de vertices (_CVV + U/V) y las funciones vu1Draw* hasta justo antes del envio. Sin cambio de resultados de VU (mismas operaciones, mismo modo).
- No esta demostrado que esto explique los ~9 fps; pendiente medir con vu_trunc=0 / gl_batch=0 / gl_shadow=0 / vu0_native=0 en la misma sala.
- Dato del log f8f5fe28 (agua): ~1270 draws GL por frame, ~8000 vertices (6.4 por draw) => el coste esta en la cantidad de draws (llamadas de driver en un FX-8350), no en la matematica de VU.
- Acoplamiento a OpenGL: la interfaz publica (pc_renderer.h) ya es neutra (PcNativeVertex/PcNativeDrawState, handles de textura enteros, registro ALPHA de la GS); GL vive en pc_renderer.c (1872 lineas), pc_gl.c/pc_gl.h (cargador), pc_platform.c (contexto SDL) y el legado gs/gs_gl.c (3112 lineas, GS emulada, deprecado). Fugas a limpiar para otro backend: GLSL embebido, estado GL mezclado con el estado del juego en apply_state, uso directo de FBO para mascara de sombra y render-texture.

## 0.9.144: render target por GPU (rama optimizacion-rtt-135) integrado
Revision de 7ba674d (base 0.9.135): StoreRenderTex ya no hace glReadPixels de 512x512 + subida a textura (parada sincrona de la GPU por cada captura del agua/gas); copia por franjas (255/255/2 filas) con glBlitFramebuffer a UNA textura GL persistente ligada al payload TIM2 (pcTextureStoreScreenRect), con alfa 0x80 => 1, y mapa imagen->textura con remapeo al compactar la memoria de texturas (Ps2TextureGarbageCollectionAll) y olvido al liberarla. Fallback a lectura por CPU si falla el blit (`rtt_gpu_copy=0` lo fuerza). La textura de render del juego (256x256) pasa a tener almacenamiento fisico escalable (`render_texture_scale`, defecto 2; UV logicas sin cambio).
- Correcto en lo revisado: orden de franjas validado (y == filas capturadas), cambio de tamano reinicia la entrada, 16 entradas maximas, fallback no mezcla franjas.
- Integracion con esta rama: pcRendererCopyScreenRectToTexture hace batch_flush() + gl_shadow_invalidate() (cambia FBO/viewport/scissor/colormask fuera de apply_state); pcRendererFlushTransparent{,I} vacian el lote aunque el OT este vacio.
- A vigilar: render_texture_scale=2 amplia con filtro lineal desde 640x480 (doble filtrado, algo mas suave que la copia 1:1 de la GS); scale=1 es lo exacto con render 640x480. Mismo comportamiento que en la rama probada por el usuario.

## 0.9.145: medicion de 0.9.144 y arreglo del contador de lotes
Log 8d44cc06 (0.9.144, render_texture_scale=4, GPU-only capture activa): 91 ventanas de 60 frames, promedio 19.3 ms (mediana 20.7, la peor ventana 28.5 ms), 0 ventanas por encima de 33 ms. Envio de tiras opacas: 0.34 us/vertice (antes 1.5 en 0.9.141 con 1.6/0.63 segun escena) => ~4x menos. plist 74.6 us/chunk.
- Contador "GL batches" del log era acumulado (no se reiniciaba por frame): ahora se reinicia junto con los demas de stat_*.

## 0.9.146: ruta GPU paralela para la conversion de vertices 3D (gpu_vertex) + mediciones separadas
Objetivo del usuario: datos del juego -> geometria nativa -> GPU, sin conversiones/emulacion PS2 innecesarias; NO sustituir la ruta actual: ruta paralela activable por INI para comparar con F8; medir por separado CPU por vertice, envio y GPU.
- Opcion: `gpu_vertex=0|1` en recvx.ini (o RECVX_GPU_VERTEX). Defecto 0 = ruta CPU de siempre, intacta. Reiniciar el juego para alternar; F8 en cada modo y comparar.
- Ruta GPU (`screen_space=3`): pcSubmitStrip3D copia el registro crudo de la VU1 (S,T,Q,R,G,B,A,X,Y,Z,F; 11 floats = mismo PcNativeVertex) y calcula en CPU solo lo que es logica de ensamblado: el filtro de recorte ADC (kick) y, SOLO en tiras con mezcla (para ordenar el OT), la Z entera. El vertex shader hace con las mismas operaciones y orden que la CPU: XY punto fijo 12.4 (floor(x*16)/16 - 1728/1808), Z de 16 bits (sz=-a+q*-b, clamp 0..65534, floor, 1-zc/65535), FTOI0 de color/niebla (floor(clamp(x,0,255))/255, alfa /128 con tope 1), S/Q*tam en texels, w=1/q. Constantes de Z de la GS (Ps2_zbuff_a/b) viajan en PcNativeDrawState (entran en la clave de agrupacion de draws).
- Verificacion (pc/tools/test_gpu_vertex.c, 10M vertices, 5 camaras, incl. Q~0, colores fuera de rango): GLSL transcrito vs CPU = 0 diferencias. Variante "GPU" (multiplicar-sumar fusionado + division como reciproco), tras la cuantizacion del fragment shader: 0 diferencias de Z de 16 bits, color y niebla; U/V hasta 1 ulp. LIMITE: es una transcripcion y una simulacion de libertades de la GPU, no ejecuta el driver; la confirmacion real es F8 en ambos modos.
- Mediciones (debug=1 + profile_game=1), tres lineas separadas:
  1. `PROFILE strips3D [CPU|GPU]`: CPU por vertice ("convert": conversion o copia + recorte ADC) y envio por vertice (ensamblado + cola/lote + GL).
  2. `PROFILE gpu scene`: tiempo de GPU de la escena con GL_TIME_ELAPSED en anillo de 6 consultas (lectura sin bloquear), junto a vertices/draws/lotes por frame y ns/vertice.
  3. `PROFILE vu1Draw ...` y `PROFILE CNK ...` (ya existentes).
- Que NO hace todavia (siguiente): colores de vu1Draw*/iluminacion siguen en CPU (la VU1 emulada en C), el filtro ADC sigue en CPU, el OT de transparencias sigue ordenando en CPU, y las tiras 2D/sprites (pcSubmitPrepared3D) no pasan por la ruta cruda.

## 0.9.147 - gpu_vertex: la cola de transparencias leia el alfa crudo
`item_may_be_translucent()` (pc_renderer.c) decidia si un item del OT tiene fragmentos con alfa < 1 leyendo `vertices[i].a < 0.9961`.
Con `gpu_vertex=1` (screen_space 3) `a` es el registro crudo de la GS (0..128+), asi que 63.75 contaba como opaco: los paneles
translucidos (tex 45, alfa 0.49) se dibujaban en la pasada opaca (con escritura de Z) y salian lechosos. Ahora se aplica la misma
conversion que el vertex shader (floor/clamp/128) antes de comparar. Leccion: ningun codigo del renderer puede leer r,g,b,a,x,y,z,u,v
de un vertice en modo 3 sin convertirlo; auditado, solo existia esta lectura con efecto (el resto es log).

## 0.9.148 - gpu_vertex: auditoria de consumidores de vertices
Tras el fallo del alfa (0.9.147) se audito todo lo que lee PcNativeVertex despues de pcSubmitStrip3D: renderer (draw_immediate,
queue_transparent, item_may_be_translucent, logs), pcAssemblePrepared, pc_movie.c. Hallazgo: los sprites (topologia 6) copian
Z/color/niebla del 2o vertice pero la Q del 1o en dos esquinas; con vertices crudos la Z sale de la Q en el shader, asi que el
resultado cambiaba. Las tiras de topologia 6 se envian por la conversion CPU aunque gpu_vertex=1. El resto es equivalente.

## 0.9.149 - gpu_vertex tambien para el empaquetador del EE (Ps2AddPrim3D / Ps2AddPrim3DMod)
Commit independiente (revertible solo): `pcSubmitPrepared3DGpu` + `screen_space 4`. Con gpu_vertex=1 la CPU ya no ejecuta
pcPs2Prim3DCore ni la conversion por vertice de pcSubmitPrepared3D; copia el registro de 12 floats y el vertex shader hace lo
mismo que la ruta CPU: XY=(int)(X*16)/16 (trunca hacia cero, NO floor como el modo 3), Z=(int)(sz*16)/16 (con fracciones de 1/16,
distinto del Z entero del modo 3), color/niebla FTOI0, S,T*tam. Posicion NO homogenea (w=1) y S/Q en el fragmento, como el modo 1.
La CPU conserva solo el kick (ADC de 6 bits, copiado de pcPs2Prim3DCore) y la Z media del OT (solo con ABE). Color entero
(floating_color=0): la CPU pasa (bits&0xFF) como float 0..255. Sprites (topologia 6) siguen por la ruta CPU.
Consumidores auditados: item_may_be_translucent (convierte alfa en modos 3 y 4), pcSubmitVertices (zbuff y texture_perspective para 4),
queue_transparent (solo lee z si !screen_space), pcAssemblePrepared (solo copia; sprites excluidos), logs. Perfil: "PROFILE prepared3D".
Prueba numerica: pc/tools/test_gpu_vertex4.c, 0 diferencias en 2M casos (sin FMA). La ruta CPU sigue siendo referencia y fallback.

## 0.9.150 - OT: items consecutivos con el mismo contexto salen en un solo draw
Commit independiente. Antes, `flush_transparent` ponia `batch_lock` y emitia un draw por item de la cola. Ahora `draw_queued_item`
compara el item con el anterior (`queue_same_context`: estado de dibujo, estado global capturado, topologia y mitad de alfa efectiva)
y, si coincide, lo acumula en el lote de draws inmediatos. Si el contexto cambia, `batch_flush()` ANTES de `restore_frame_state`,
porque el lote se emite con los globales vigentes (alpha_pass, ot_writes_depth, mezcla, scissor, niebla, Z). El orden del OT no
cambia nunca. `batch_lock` desaparece (ya nadie lo usa). Se ve en el log: "GL batches" baja frente a "draws" y a `queued`.
Reversible con `git revert` de este commit. gl_batch=0 tambien lo desactiva (cada item emite su draw).

## 0.9.151 - Perfil de GPU por pasada (solo con debug=1 + profile_game=1)
Commit independiente, solo medicion: no cambia lo que se dibuja. Marcas `glQueryCounter(GL_TIMESTAMP)` (nucleo de GL 3.3) entre
categorias: directo (limpiar, tiras opacas, 2D), OT (flush_transparent), sombras (ShadowBegin..End y Compose) y copia a textura
(CopyScreenToRenderTexture / CopyScreenRectToTexture). `gpu_mark()` vacia el lote antes de marcar. El intervalo entre dos marcas se
atribuye a la categoria activa. Nueva linea cada 120 frames: "PROFILE gpu passes". Incluye tambien el tiempo de CPU de la escena
(begin..end): `GL_TIME_ELAPSED` cuenta tambien los huecos en que la GPU espera a la CPU, asi que si el total de GPU ~ el CPU de la
escena, el "GPU scene" estaba inflado por espera y no es coste real de GPU. Es la primera cosa a mirar en el log.

## 0.9.152 - Copia a textura (render_texture_scale): desglose y menos relleno de alfa
Log f5286098 (cuarto del agua, scale 4, gpu_vertex=1): por frame directo 1-7 ms, OT 0.5-1.7, sombras 0.07, **copia a textura 9-19 ms
(3 copias de 2048x2048)**. En los frames de captura el CPU de bhMainSequence "render-texture" tambien marca ~20 ms: la CPU espera a esa GPU.
Cambios: (1) el perfil parte la copia en preparar / limpiar / blit / alfa. (2) la limpieza de alfa (A=1, mascara de color solo A) se
hacia sobre TODA la textura en cada una de las 3 bandas; ahora solo sobre el rectangulo recien copiado (scissor x*scale, y*scale,
vw*scale, vh*scale). Resultado identico: y==0 ya limpia RGBA=(0,0,0,1) y las bandas anteriores ya repusieron su alfa.
No se asume que esto explique los 6 ms por copia; el siguiente log dira que fase pesa (linea "PROFILE gpu passes").

## 0.9.153 - Copia a textura: el coste no es de relleno, es de espera (orphaning al inicio de captura)
Logs de114157f9 (scale 4) y 0104dfe3 (scale 2), cuarto del agua: por frame de captura el "blit" cuesta 8-17 ms para 3 copias y es IGUAL con
escala 2 (1024x1024) que con 4 (2048x2048); preparar/limpiar/alfa suman < 0.3 ms. Un blit de 640x480 a 1024^2 no puede costar 3-6 ms
de GPU: el coste no depende de los pixeles, asi que es una espera (la textura destino sigue en uso por los draws del frame anterior,
o el driver la prepara). Cambio: al empezar una captura (y==0) se re-especifica la textura con glTexImage2D(NULL) del mismo tamano
("orphaning"): el contenido viejo ya no sirve porque la limpieza de y==0 rellena toda la textura, asi que el resultado es identico.
Tambien se mide el CPU dentro de glBlitFramebuffer ("CPU dentro de glBlitFramebuffer" en PROFILE gpu passes): si es ~ el tiempo de
blit, la CPU espera dentro de la llamada (sincronizacion); si es ~0, el tiempo es de GPU. Si el orphaning no lo arregla, el
siguiente paso es doble buffer (ping-pong) de la textura destino por render target.

## 0.9.154 - rtt_blit_draw=1: copia a textura por draw (ruta paralela, opt-in)
Log 5eb925c7 (0.9.153, scale 4): el orphaning NO bajo el blit (6-18 ms por frame de captura) y "CPU dentro de glBlitFramebuffer" = 0.00,
asi que no es una espera de CPU ni de relleno (el coste era igual a escala 2 y 4). Hipotesis que queda: el blit escalado/invertido con
LINEAR de glBlitFramebuffer toma una via lenta del driver (NVIDIA GTX 1050 Ti). Para comprobarlo sin tocar la ruta de referencia:
INI `rtt_blit_draw=1` dibuja un triangulo sobre el rectangulo destino y el fragmento muestrea color_target con el mismo mapeo
(centro de pixel destino -> origen + (px+0.5)*(ancho_origen/ancho_destino), Y invertido, LINEAR, borde fijado). Por defecto 0 (blit).
Se compara con F8 (misma escena, 0 vs 1) y con "blit X ms" de PROFILE gpu passes. Si no cambia el tiempo, el coste no esta en el
blit sino en otra cosa (p.ej. reloj de la GPU en frames ligeros) y la ruta se descarta. El orphaning de 0.9.153 queda (inofensivo).

## 0.9.155 - rtt_blit_draw=1 por defecto
Log 4b6f76c2 (0.9.154, scale 4, rtt_blit_draw=1): blit 8-18 ms -> 0.2 ms por frame de captura; GPU de escena (~4300 vertices) 18-20 -> 7.2 ms.
Usuario: F8 sin diferencias visibles. `rtt_blit_draw=0` vuelve a glBlitFramebuffer (referencia). El CPU de la seccion render-texture
(~21 ms, redibujo de la escena) no cambia: el siguiente cuello es CPU. Los picos de 144 ms GPU / 38 ms CPU eran el volcado de la rafaga F8.

## 0.9.156 - Perfil de CPU: draws GL y enlace de texturas (solo medicion)
Log 4b6f76c2 (cuarto del agua): en los frames de captura bhMainSequence "render-texture" = ~22 ms de CPU = bhAllDrawModel (room 11.8 ms,
objetos 6.7, jugador 1.75). Dentro de room, "PROFILE CNK type": tipo 8 (njCnkCtTid = cambio de textura, 42 llamadas/frame) = 10 ms
(0.24 ms por llamada) y tipo 66 (tiras) solo 1.3-1.7 ms. El chunk de textura solo llama a njSetTextureNumSys -> Ps2TexLoad ->
pcNativeTextureLoad (los contadores "PROFILE TEX" de Ps2TexLoad estan en la rama #else de PC y siempre salen 0). Candidatos:
(a) hash_bytes de imagen+CLUT en cada enlace (bind_memo solo recuerda UNA superficie por frame), (b) vaciado del lote GL pendiente
al cambiar de textura (CPU del driver), (c) pcRendererConfigureTexture (glTexParameteri en cada carga), (d) decodificar+subir.
Nuevos contadores (solo con profile_game=1): "PROFILE texture bind" (total / hash / decodificar+subir / enlazar+sampler, binds/frame) y,
en "PROFILE gpu passes", "CPU en draws GL X ms (N draws/frame)". No cambia lo que se dibuja.

## 0.9.157 - Enlace de texturas: hash de contenido mas rapido + memo por frame opcional
Log d419d0ed (cuarto del agua, 0.9.156): "PROFILE texture bind" total 8.8-14.0 ms/frame, de los cuales **hash 8.7-13.8 ms** (99%),
decodificar+subir 0.00, enlazar+sampler 0.07-0.17, ~100-137 enlaces/frame; "CPU en draws GL" solo 0.21-0.37 ms (125-155 draws/frame).
O sea: el coste de CPU de los chunks de textura (CtTid) era hash_bytes de imagen+CLUT en cada enlace (bind_memo recordaba una sola superficie).
1) `hash_bytes` ahora usa 4 acumuladores de 32 bits independientes (estilo xxHash32), pensado para el build x86 de 32 bits
   (el FNV de 64 bits se expande en imul encadenadas). Mismo criterio exacto (contenido completo), solo cambia la funcion;
   el hash no se guarda en disco. pc/tools/test_texture_hash.c: 0 fallos en cambios de 1 byte, bloques intercambiados, +x/-x, desplazados.
   En x86-64 (host de pruebas) 1.5x mas rapido; el beneficio esperado en 32 bits es mayor, sin medir.
2) `tex_frame_memo=1` (INI, por defecto 0): bind_memo pasa de 1 a 64 entradas por frame (misma suposicion ya documentada: sin recargas
   ni subidas de CLUT entre medias). Ruta paralela, se compara con F8. Con 0 el comportamiento es el de siempre.

## 0.9.158 - tex_frame_memo: memorizar el hash de la IMAGEN, no el enlace completo
Logs b0e7bf80 (tex_frame_memo=0) y ffdc6f1f (tex_frame_memo=1), cuarto del agua, 0.9.157. Hash de contenido (ventanas con ~110-137 enlaces/frame):
0.9.156 8.7-13.8 ms -> 0.9.157 con memo=0 **6.0 ms** (hash de 4 acumuladores de 32 bits, ~2x); render-texture de CPU 21.9 -> 12.1-12.7 ms.
El memo de 64 entradas de 0.9.157 apenas ayudo (5.45 ms con 110 enlaces): la clave incluia vram_clut_generation, que cambia con cada
subida de paleta (el juego sube la paleta de cada textura paletizada), asi que casi nunca acertaba.
Ahora `tex_frame_memo=1` memoriza solo el hash de la imagen por (puntero, tamano, frame), hasta 64 imagenes; el hash del CLUT (<=1 KB) se
recalcula siempre, de modo que los cambios de paleta se detectan exactamente igual. bind_memo vuelve a una sola entrada (como siempre).
