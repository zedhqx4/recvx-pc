# claude_report.md — OpenGL 4.3 Core: revisión técnica y estabilización

Rama: `review/claude-opengl-4.3` (base `c46b743`). Alcance: creación de contexto, loader, `gs_gl.c`, `gs_replace.c`, shaders, workflows.

## Qué se pudo y qué no se pudo verificar

| Verificación | Resultado |
|---|---|
| Lectura del código fuente del repo | Hecha |
| Lectura del código fuente de SDL 3.4.18 (versión fijada en `pc/tools/fetch_deps.py`) | Hecha (`SDL_video.c`, `SDL_windowsopengl.c`) |
| `gcc -fsyntax-only` de `gs_gl.c` con cabeceras SDL 3.4.18 (con `-include stddef.h`) | Limpio antes y después de los cambios |
| Prueba con mocks de `verify_gl_context()` (9 casos, gcc en Linux) | 9/9 correctos |
| Compilación de `pc_platform.c` completo | **No ejecutada**: depende de `<io.h>`/MSVC (Windows) |
| Compilación MSVC / workflows de GitHub Actions | **No ejecutada** |
| Ejecución real en Windows o Linux con GPU | **No ejecutada** (sin GPU ni Windows en este entorno) |
| `pc/tests/gs_mem_blockpath_test.c` | No ejecutada: no toca código modificado |

---

## Hallazgos

### CGL-001 — Alto — Confirmado (por lectura de código) — Corregido, sin verificar en ejecución
- **Archivo/función:** `pc/src/pc_platform.c`, `pcVideoInit()` (comprobación de versión tras `SDL_GL_CreateContext`).
- **Problema:** la comprobación de "OpenGL 4.3 Core" usaba `SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR/MINOR_VERSION, PROFILE_MASK)`. En SDL 3.4.18 (`SDL_video.c`) esos atributos devuelven `gl_config.*`, es decir **lo solicitado**, no lo concedido por el driver. La comprobación era tautológica: nunca podía fallar.
- **Evidencia:** `case SDL_GL_CONTEXT_MAJOR_VERSION: *value = _this->gl_config.major_version;` en SDL 3.4.18.
- **Consecuencia:** un contexto degradado pasaba la verificación y el fallo aparecía más tarde, en el loader, con un mensaje engañoso.
- **Solución aplicada:** nueva `verify_gl_context()` que comprueba contexto actual, lee `GL_VENDOR/GL_RENDERER/GL_VERSION` con `glGetString`, exige ≥ 4.3 y perfil Core mediante `glGetIntegerv(GL_CONTEXT_PROFILE_MASK)`. Registra vendor/renderer/versión en `recvx.log`.

### CGL-002 — Alto — Probable (causa de `OpenGL: missing glActiveTexture` en Windows) — Diagnóstico mejorado, causa de entorno no verificada
- **Archivos/funciones:** `pc/src/gs/gs_gl.c` `pcGlLoad()`; SDL `WIN_GL_CreateContext`.
- **Mecanismo (verificado en el código de SDL 3.4.18):** si `wglCreateContextAttribsARB` no existe, SDL fija el error "GL 3.x is not supported", **conserva el contexto heredado OpenGL 1.1 y lo devuelve como válido**. Con ese contexto solo resuelven las funciones de GL 1.1 (exportadas por `opengl32.dll`); `SDL_GL_GetProcAddress` devuelve NULL para el resto.
- **Evidencia indirecta:** en `PC_GL_FUNCS` las seis primeras funciones (`glGenTextures`…`glTexParameteri`) son GL 1.1 y `glActiveTexture` es la **primera** que no lo es: exactamente la que el log reporta como ausente. Es coherente con un contexto 1.1, no con una única función faltante en un driver 4.3. Combinado con CGL-001 (que ocultaba la degradación), explica el síntoma.
- **Lo que NO está demostrado:** que en la máquina del usuario el renderer sea el GDI genérico de Microsoft (sin driver GPU, escritorio remoto, VM o runner de CI sin GPU). Otras causas no descartadas: contexto no actual en el momento de cargar (se comprobó que no hay hilos del SO: los hilos PS2 son fibras del mismo hilo; `SDL_GL_CreateContext` deja el contexto actual), o fallo del driver.
- **Cómo confirmarlo:** con esta rama, `recvx.log` mostrará `video: GL_VENDOR=… GL_RENDERER=… GL_VERSION=…`. Si aparece `GDI Generic` / `1.1.0`, queda confirmado.
- **Consecuencia:** el juego no arranca; esta corrección **no lo hace arrancar** en un equipo sin OpenGL 4.3 real (esa no es una corrección posible sin ocultar el error), solo lo falla antes y con la causa correcta.
- **Solución aplicada:** ver CGL-001 y CGL-003.

### CGL-003 — Bajo — Confirmado — Corregido
- **Archivo/función:** `pc/src/gs/gs_gl.c`, `pcGlLoad()`.
- **Problema:** se detenía en la primera función ausente, sin indicar cuántas faltaban ni si había contexto actual.
- **Solución aplicada:** registra todas las ausentes, su número y si hay contexto actual; sigue devolviendo 0 (el error no se oculta).

### CGL-004 — Bajo — Confirmado — Corregido parcialmente
- **Archivos:** `gs_gl.c` (`gsRendererInit`), `gl_funcs.h`, `pc_gs.h`.
- **Problema:** mensajes y comentarios decían "OpenGL 3.3" con un contexto 4.3 solicitado.
- **Solución aplicada:** textos corregidos. **Pendiente:** `README.md` (raíz, línea 38) sigue diciendo "OpenGL 3.3"; no se tocó por ser documentación fuera del alcance de la revisión técnica.

### CGL-005 — Medio — Confirmado — Pendiente (no corregido)
- **Archivo:** `pc/src/gs/gs_gl.c` (todo el renderer).
- **Problema:** no hay ninguna llamada a `glGetError` ni uso de `KHR_debug` (disponible en 4.3 Core). Solo `gs_replace.c` consulta `glGetError`. Los errores GL (p. ej. FBO incompleto, formato no válido) pasan inadvertidos.
- **Solución propuesta:** contexto de depuración + `glDebugMessageCallback` activable por variable de entorno. No se implementó: cambia el flujo de inicialización y no puedo verificarlo en ejecución.

### CGL-006 — Bajo — Confirmado — Pendiente
- **Archivo:** `pc/src/pc_platform.c`.
- **Problema:** no existe `SDL_GL_DestroyContext` ni `SDL_Quit` en ninguna ruta; el renderer no libera VAO/VBO/programas/texturas/FBOs al salir.
- **Consecuencia:** ninguna práctica hoy (limpieza al terminar el proceso); relevante solo si se añade reinicio del renderer.

### CGL-007 — Bajo — Confirmado — Pendiente
- **Archivos:** `.github/workflows/*.yml`.
- **Problema:** los workflows se disparan en `experimental/opengl-4.3`, no en `review/claude-opengl-4.3`, de modo que esta rama no compila en CI hasta que se lance manualmente (`workflow_dispatch`). Además, ningún workflow ejecuta el renderer: un runner Windows de GitHub sin GPU usaría el GDI genérico y reproduciría CGL-002 (hipótesis, no comprobada).
- **Solución propuesta:** lanzar `build-windows.yml` manualmente sobre esta rama. No se modificaron workflows.

### CGL-008 — Bajo — Confirmado — Pendiente
- **Archivo/función:** `gs_gl.c`, `compile()`/`link()`.
- **Problema:** los shaders no se eliminan tras enlazar (fuga única de 4 objetos al arrancar). Sin impacto práctico.

### Revisado sin encontrar evidencia de fallo
- Shaders `#version 330 core` en contexto 4.3 Core: válido; todas las funciones de `PC_GL_FUNCS` son GL ≤ 3.3 (el requisito 4.3 es una decisión de política, no una dependencia técnica actual).
- Hilo/contexto: los hilos PS2 son fibras (`pcOsFiberCreate`) del hilo principal; no hay otros hilos que usen GL.
- Caché de estado: `gs_replace.c` usa `glBindTexture` sin caché, pero `pcGsInvalidateTextureBindings()` se llama antes de `gsReplaceLookup`; el cache de unidad activa no se ve afectado.
- Orden de inicialización: la reserva del scratchpad ocurre antes del contexto, y `pcGsInit` después de él.

---

## Auditoría de texturas (fase 2)

### CGL-009 — Medio — Confirmado (harness host) — Corregido, sin verificar en ejecución
- **Archivo/función:** `pc/src/gs/gs_gl.c`, `get_texture()` / `gen_sum_rect()`.
- **Problema:** la caché de texturas decide si los datos cambiaron hasheando las páginas de 8 KB `page_lo = tbp>>5` + `ceil(rows/ph)` bandas × `ceil(cols/pw)` columnas. Si `tbp & 31 != 0` la textura empieza dentro de una página y desborda a una fila/columna de páginas adicional que no se hasheaba.
- **Evidencia:** harness (escribe cada píxel con `gsMemWrite` y compara páginas con `gsPageGen` modificado contra las hasheadas): 140 casos (CT32/CT16/T8/T4, 64–512 px, varios TBP); los 92 casos con `tbp&31 != 0` dejan ≥1 página tocada sin cubrir; con TBP alineado, 0 fallos.
- **Consecuencia:** una subida parcial a esa página extra no invalida la caché → textura obsoleta. Con la carga TIM2 del juego (TBP 190*64, alineado) no se dispara; afecta a otras rutas con TBP no alineado.
- **Solución:** +1 banda y +1 columna cuando `tbp & 31`, en ambos usos de `gen_sum_rect`. Revalidar con el harness tras el cambio queda pendiente.

### Pendientes sin confirmar (hipótesis de la lectura de código)
- **CGL-010** `texture_extent` con REGION_CLAMP/REGION_REPEAT puede devolver extensión menor a la muestreable (harness no ejecutado).
- **CGL-011** Ruta HD (`uHD`): `texture()*255.0` deja alfa en 255 mientras el resto del pipeline usa 0x80 = 1.0 (no verificado).

### CGL-012 — Alto — Probable (por lectura; reproduce los síntomas descritos) — Corregido, sin verificar en ejecución
- **Archivo/función:** `src/ps2/veronica/prog/ps2_dummy.c`, `Ps2EnsureTextureImage()`.
- **Problema:** un acierto en la caché de assets TIM2 omite la subida de la imagen a VRAM (190*64) pero deja `Ps2_tex_cache_buff[0] = timp`. En la carga siguiente de la misma textura, si el alias del renderer ya se evictó (`asset_cache_hit == 0`), la rama `timp == Ps2_tex_cache_buff[0]` tampoco sube nada. Invariante rota: "buff[0] == imagen residente en VRAM".
- **Consecuencia:** `get_texture()` falla el alias y decodifica de la VRAM la imagen de otra textura con la paleta de la nueva → índices desincronizados / caras con textura equivocada.
- **Solución:** `pc_vram_image_resident` registra qué TIM2 se subió de verdad; para texturas elegibles, si `timp != residente` se fuerza la ruta de subida.
- **Verificación:** solo lectura de código + revisión del diff. No se pudo compilar (`ninja.h`/MSVC) ni ejecutar. Confirmar con `RECVX_PS2_TEX_CACHE=0` (si desaparece, apunta a esta ruta) y comparando antes/después.
