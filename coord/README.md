# Coordinacion Claude (jefe) / GPT (asistente)

Proyecto: port de PC de Resident Evil Code: Veronica X (`recvx-pc`). Objetivo actual ("nativizar"): eliminar la
dependencia de la emulacion de hardware de PS2 (VU0/VU1/GS/EE) con resultados identicos, en C escalar sobre CPU
moderna, sin convertir todo a shaders y sin APIs graficas concretas (mismo codigo debe servir para Vulkan/DX/Metal).

## Roles y ramas
- **Claude** (jefe): trabaja en `optimizacion`. Decide el plan, escribe `coord/TASKS.md`, revisa y integra.
- **GPT** (asistente): trabaja SOLO en `optimizacion-gpt`. Nunca hace push a `optimizacion`.
- Integracion: Claude revisa `origin/optimizacion-gpt` y trae los commits que aprueba (cherry-pick). GPT hace
  `git fetch origin && git merge origin/optimizacion` al empezar cada tarea para ver las tareas nuevas.
- El usuario solo pasa las ordenes; todo lo demas viaja por archivos de este directorio.

## Canal (cada lado escribe solo sus archivos: sin conflictos de merge)
- `coord/TASKS.md` (lo escribe Claude): cola de tareas con id, alcance, archivos permitidos y criterio de terminado.
- `coord/reports/<id>.md` (lo escribe GPT): informe de cada tarea: que hizo, comandos ejecutados con su salida
  literal, dudas, cosas que NO pudo comprobar. Si una tarea es solo de analisis, el informe ES el entregable.
- Si GPT duda o encuentra algo inesperado: lo escribe en el informe bajo `## Preguntas` y se detiene en ese punto.

## Limites del entorno (nadie puede hacer esto)
- Nadie compila con MSVC ni ejecuta el juego; el usuario compila en su CI y prueba. Por eso: cambios pequenos, uno por
  commit, reversibles, y todo lo verificable se verifica con gcc.
- Varios archivos no compilan solos (falta `ninja.h`, el SDK de Sega). Se verifica extrayendo el bloque a un archivo
  de prueba con tipos minimos (`gcc -fsyntax-only -msse2 -Ipc/include -Iinclude -DTARGET_PC ...`) y con un harness
  que compare contra la version actual. Compilacion MSVC: C89 (declaraciones al inicio del bloque), `uint64_t` en vez
  de `long`, funciones GL solo por `pc/include/pc_gl.h`.

## Reglas del proyecto (no negociables)
1. Sin parches ni "band-aids": se arregla la causa, no el sintoma.
2. Rutas paralelas conmutables por INI hasta validar; la referencia se conserva hasta que el usuario valide con log.
3. Cada cambio en su propio commit, independientemente revertible. Un commit = una idea.
4. Mensajes de commit: `[<id de tarea>] resumen` y al final las lineas de atribucion que indique el entorno.
5. GPT NO sube la version (`pc/res/version.h`) ni toca `claude_notes.md`: lo hace Claude al integrar.
6. No optimizar por optimizar (el rendimiento ya es bueno: tope de 30 fps). No portar texturas HD.
7. No inventar: si algo no se pudo ejecutar, el informe lo dice. "No lo comprobe" es una respuesta valida.
8. No tocar archivos fuera del alcance de la tarea. Archivos de Claude por defecto: `ps2_Vu1Strip.c`, `ps2_NaMatrix.c`,
   `ps2_NaView.c`, `ps2_NinjaCnk.c`, `ps2_Vu1Scissor2.c`, `pc/src/vu/*`, `pc/include/pc_vu*.h`, `pc_clip.h`, `pc_scissor.h`.

## Contexto tecnico minimo
- `pcVu0` (`PcVu0`: `vf[32]`, `vi[16]`, `mem[1024]`) es el estado de VU0 emulado. Se esta eliminando: ver
  `claude_notes.md` (secciones 0.9.159 a 0.9.171). Ya tienen ruta pura validada: color VU1, CALCPOINT (micros 7/49/42),
  clip (269/237/394), scissor (302/327/333/339). Queda el paso "3d": borrar `pcVu0`, `vu0_micro.c`, `vu0_native.c`.
- `vu_f(x)`: float -> VU (inf/NaN a maximo, denormal a 0). `pcVuRoundEnter/Leave`: truncado de la VU (MXCSR).
- Las pruebas de equivalencia que hemos usado: harness en C que ejecuta el codigo viejo y el nuevo con miles de casos
  aleatorios y compara bit a bit. Si hay diferencias de 1 ULP, se investiga el modo de redondeo antes de aceptarlas.
