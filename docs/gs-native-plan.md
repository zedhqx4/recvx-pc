# GS nativo en PC — plan por pasos

Alcance acordado: capa de render directa (el juego deja de generar paquetes GIF), manteniendo la ruta emulada como fallback activable por entorno (`RECVX_GS_NATIVE=1` activa lo nativo; sin él, nada cambia).

Estado actual: el renderer recibe `GsDrawState` + `GsVertex` (`gsRendererDraw`), pero el juego construye paquetes GIF/DMA y sube texturas a la VRAM emulada (`Send_Tim2_dataEx` → `LoadToVram`), que el renderer decodifica de vuelta (`gsDecodeTexture`/`gsDecodeIndices`).

## Pasos
1. **(hecho) Registro de texturas lineales** — `gs_native.[ch]`: guarda píxeles/índices + CLUT sin swizzle. Probado en host; aún no conectado.
2. Conectar `Send_Tim2_dataEx`/`Ps2TexLoad` para registrar la textura nativa, y que `get_texture()` use `gsNativeTexFind` (por TBP/CBP/PSM/tamaño) en vez de leer la VRAM. Comparar contra la ruta emulada píxel a píxel.
3. CLUT/paletas nativas (CSA, CPSM, TEXA) sin pasar por `clut_buf`.
4. API de dibujo directa (vértices + estado) para las rutas 2D del juego, saltando GIF; luego 3D.
5. Retirar la dependencia de la VRAM emulada solo donde haya equivalencia demostrada.

Pendiente sin decidir: invalidación cuando el juego sobrescribe un TBP (el registro reemplaza por TBP+PSM; falta definir qué pasa si el GS lo sobrescribe por transferencia o render target).
