# GS nativo en PC — plan por pasos

Alcance acordado: capa de render directa (el juego deja de generar paquetes GIF), manteniendo la ruta emulada como fallback activable por entorno (`RECVX_GS_NATIVE=1` activa lo nativo; sin él, nada cambia).

Estado actual: el renderer recibe `GsDrawState` + `GsVertex` (`gsRendererDraw`), pero el juego construye paquetes GIF/DMA y sube texturas a la VRAM emulada (`Send_Tim2_dataEx` → `LoadToVram`), que el renderer decodifica de vuelta (`gsDecodeTexture`/`gsDecodeIndices`).

## Pasos
1. **(hecho) Registro de texturas lineales** — `gs_native.[ch]`: guarda píxeles/índices + CLUT sin swizzle. Probado en host; aún no conectado.
2. **(hecho, sin ejecutar)** Almacén de planos de índices (`gsNativeIndex*`): con `RECVX_GS_NATIVE=1`, la identidad de una textura TIM2 indexada es (asset, hash de contenido), no las páginas de VRAM; el plano decodificado se guarda en CPU, el atlas se reconstruye desde él y el juego omite la subida solo si el plano está en el almacén. Pendiente la comparación píxel a píxel con la ruta emulada.
   Siguiente: conectar `Send_Tim2_dataEx`/`Ps2TexLoad` para registrar la textura nativa, y que `get_texture()` use `gsNativeTexFind` (por TBP/CBP/PSM/tamaño) en vez de leer la VRAM. Comparar contra la ruta emulada píxel a píxel.
3. CLUT/paletas nativas (CSA, CPSM, TEXA) sin pasar por `clut_buf`.
4. API de dibujo directa (vértices + estado) para las rutas 2D del juego, saltando GIF; luego 3D.
5. Retirar la dependencia de la VRAM emulada solo donde haya equivalencia demostrada.

Pendiente sin decidir: invalidación cuando el juego sobrescribe un TBP (el registro reemplaza por TBP+PSM; falta definir qué pasa si el GS lo sobrescribe por transferencia o render target).


## Estado en Claude-alpha (2026-11-08), con RECVX_GS_NATIVE=1
- **Paso 1 (carga):** `Ps2CheckTextureAlpha` (los bytes del TIM2 ya son finales) decodifica el plano de índices con `gsMemDecodeTim2Indices` y lo guarda por (Gindex, hash de contenido). Ningún dibujo vuelve a decodificar desde VRAM.
- **Paso 2 (pool del OT):** `Ps2DrawOTagSub` ya no sube la imagen al pool: sube solo paleta + metadatos (`Send_Tim2_dataExClutOnly`) y registra `pcGsNativeBindTbp(tex_addr, ...)`. `current_state()` del GS sustituye, para ese TBP, la identidad y también TBW/PSM/TW/TH por los del plano real (ya no valen los capturados al registrar el dibujo). El enlace se invalida si una escritura real toca sus páginas, y se limpia al reiniciar el pool y al terminar `Ps2DrawOTag`.
- `Ps2TexLoad` asegura el plano antes de consultar la caché, así que nunca sube la imagen.
- Reemplazo HD: el hash se calcula sobre el plano swizzleado (`gsMemSwizzleIndices`), idéntico bloque a bloque a la VRAM real (prueba de host).
- Sin cambios: paletas (CLUT) siguen por el GS; texturas no indexadas, PictFormat sin hashes válidos y todo lo que no sea TIM2 indexado siguen por la ruta emulada.
- Pendiente: la identidad global de `Ps2TexLoad` sigue borrándose con cualquier escritura TEX0 posterior (hoy inofensivo porque el plano ya existe, pero un enlace por TBP también ahí lo haría robusto); paletas nativas; sprites/UI/render targets.
