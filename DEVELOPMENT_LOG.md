# Development Log — reconstruction/1-scratchpadAndSDL3

**Repository:** `zedhqx4/recvx-pc`  
**Branch:** `reconstruction/1-scratchpadAndSDL3`  
**Base branch:** `pc-port`  
**Base commit:** `7146e57630b8cdb2a9c6e0443e25aa5a2ecce7a9`  
**Scope:** scratchpad initialization order, SDL3 migration, and external Gamepad mapping database only.

This is an intentionally narrow reconstruction branch. It starts directly from `pc-port`; it does not inherit the graphics-fixes, Alpha, SDL3, or GS-Rebuild feature sets wholesale.

## Goals

1. Reserve the PS2 scratchpad early enough that SDL/OpenGL driver initialization cannot claim its fixed virtual address.
2. Migrate the PC platform code and build dependencies from SDL2 to SDL3.
3. Load an optional external `gamecontrollerdb.txt` beside `recvx.exe`, before enumerating gamepads.

No renderer redesign, texture/cache work, CNK optimization, UV correction, decal/depth work, video changes, profiling additions, or game-logic changes are in scope.

## 1. Early PS2 scratchpad reservation

The game uses a fixed 16 KB scratchpad at virtual address `0x70000000` through literal addresses used by the PS2-side code (`WORKBASE`, DMA tags, and related paths).

In `pcVideoInit()`, the reservation now occurs:

1. SDL is initialized.
2. `pcOsAllocScratchpad()` reserves the fixed 16 KB region.
3. OpenGL attributes are configured.
4. SDL creates the window and OpenGL context.
5. `pcGsInit(spr)` receives the already-reserved pointer.

This is the early ordering used by `experimental/gs-rebuild`: reserve after SDL initialization but before creating/loading the OpenGL window/context and driver. The allocation happens exactly once; the pointer is retained until the GS renderer is initialized.

The allocator itself remains in `pc/src/pc_os_win32.c`; this branch changes when it is called, not the address, size, or allocation mechanism.

## 2. SDL2 to SDL3 migration

The PC application and its build now use **SDL 3.4.18**.

### Runtime/API migration

- SDL headers are now included through `SDL3/`.
- Window creation, window positioning, fullscreen selection, display enumeration, and drawable pixel-size queries use SDL3 APIs.
- Window events use SDL3's individual `SDL_EVENT_*` event types.
- Keyboard and controller event fields use the SDL3 event structures.
- The controller path uses SDL3's Gamepad APIs (`SDL_Gamepad`, `SDL_OpenGamepad`, `SDL_GetGamepadButton`, `SDL_GetGamepadAxis`, and related functions).
- SDL3's cursor API is adapted at the platform boundary so existing internal query/enable/disable call sites retain their behavior.
- `SDL_GetBasePath()` is treated as SDL3-owned cached storage; the caller no longer frees it.
- The audio mixer uses an `SDL_AudioStream` and SDL3 stream callback/data APIs while preserving the existing float stereo mix path.
- The settings application (`recvx-config.exe`) is migrated to the SDL3 display, window, event, and Gamepad interfaces.
- The GS renderer obtains the drawable pixel dimensions with `SDL_GetWindowSizeInPixels()`.

### Build and runtime dependencies

`pc/tools/fetch_deps.py` now builds SDL 3.4.18 from its source archive for 32-bit Windows. It enables joystick/HIDAPI support and uses the x86-windows vcpkg triplet to provide libusb for the Wii U GameCube adapter path.

`pc/tools/build.py` links against `SDL3.lib` and stages these runtime files beside the executables:

- `SDL3.dll`
- `libusb-1.0.dll`

The Windows workflow includes this branch and publishes both runtime DLLs with the executables.

## 3. External Gamepad mapping database

The platform now checks for:

```text
gamecontrollerdb.txt
```

in the same directory as `recvx.exe`.

`load_gamepad_mapping_database()` uses `pcPlatformExeDir()` to construct that path and calls `SDL_AddGamepadMappingsFromFile()` when the file exists. It is invoked by `pcPadInit()` after loading the game's own binding settings and **before** `SDL_GetJoysticks()` enumerates devices or the platform attempts to open a Gamepad.

Behavior:

- If the file is present and readable, accepted custom mappings are logged.
- If the file is missing, the platform logs that it will use SDL's built-in mappings.
- If SDL rejects or cannot load the file, the error is logged and initialization continues with the mappings SDL already has.
- The file is external and optional; this branch does not bundle or generate a mapping database.

The loader follows the implementation in `experimental/gs-rebuild`, without importing that branch's unrelated renderer work.

## Files changed

| File | Purpose in this branch |
|---|---|
| `.github/workflows/build-windows.yml` | Build this branch and publish SDL3/libusb runtime DLLs |
| `pc/config/recvx_config.c` | Migrate the settings app to SDL3 |
| `pc/src/gs/gl_funcs.h` | Include SDL3 OpenGL declarations |
| `pc/src/gs/gs_gl.c` | Query drawable pixel size through SDL3 |
| `pc/src/pc_audio.c` | Migrate audio output to SDL3 audio streams |
| `pc/src/pc_platform.c` | SDL3 window/input/Gamepad APIs, early scratchpad reservation, external mappings loader |
| `pc/tools/build.py` | Link against SDL3 and stage its runtime dependencies |
| `pc/tools/fetch_deps.py` | Build SDL3 3.4.18 with HIDAPI/libusb enabled |

Only these eight files are changed relative to the `pc-port` base, plus this `DEVELOPMENT_LOG.md`.

## Explicit exclusions

The following are intentionally not ported into this branch:

- Alpha's indexed texture atlas/CLUT and TIM2 source caches.
- GS renderer/OpenGL redundant-state optimization.
- GS VRAM transfer or swizzle experiments.
- GS draw-state caching or CLUT flush changes.
- CNK/VU0/SSE2 geometry optimizations or profiling.
- UI UV crop/tuning, font sampling, custom render resolution, VSync configuration, and decal/depth changes.
- Any movie/video implementation changes.
- Startup tracing or unrelated diagnostic infrastructure.

## Validation status

The source/API migration and branch diff have been inspected. The current diff against `pc-port` is limited to the eight files listed above. The SDL2 event/controller identifiers in the migrated platform file were checked for remaining references.

**A Windows build has not yet been confirmed for this branch.** The configured Actions workflow should provide the compilation check; until a successful run is observed, this log does not claim that the branch builds or that runtime controller/audio behavior has been tested.

## Maintenance rule

Keep this branch limited to the three goals above. Record any later changes here, and do not merge unrelated Alpha/GS-Rebuild work into this reconstruction.
