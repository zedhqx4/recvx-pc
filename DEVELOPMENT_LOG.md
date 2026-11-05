# Development Log — reconstruction/1-scratchpadAndSDL3

**Repository:** `zedhqx4/recvx-pc`  
**Branch:** `reconstruction/1-scratchpadAndSDL3`  
**Base branch:** `pc-port`  
**Base commit:** `7146e57630b8cdb2a9c6e0443e25aa5a2ecce7a9`  
**Scope:** early scratchpad initialization, SDL3 migration, external Gamepad mappings, and a fixed 512x448 GS render base.

This is an intentionally narrow reconstruction branch. It starts directly from `pc-port`; it does not inherit the graphics-fixes, Alpha, SDL3, or GS-Rebuild feature sets wholesale.

## Goals

1. Reserve the PS2 scratchpad early enough that SDL/OpenGL driver initialization cannot claim its fixed virtual address.
2. Migrate the PC platform code and build dependencies from SDL2 to SDL3.
3. Load an optional external `gamecontrollerdb.txt` beside `recvx.exe`, before enumerating gamepads.
4. Preserve the game's 640x480 logical coordinate space while rasterizing GS render targets into a 512x448 base.

No renderer redesign, texture/cache work, CNK optimization, UV correction, decal/depth work, movie/video playback changes, profiling additions, or game-logic changes are in scope.

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

## 4. Fixed 512x448 GS render base

The GS renderer now keeps the game's **640x480 logical coordinate space** but uses a **512x448 backing base** for OpenGL render targets. This is a backing-buffer change, not a window-size change and not a change to game-side vertex coordinates or GS register values.

- `pc/src/pc_platform.c` defines the 640x480 logical dimensions and the 512x448 backing dimensions. It derives separate floating-point X/Y backing scales from the existing `scale=` setting and display aspect; the user-selected `pcGsScale` multiplier remains unchanged.
- `pc/include/pc_gs.h` exposes the fractional horizontal and vertical backing scales.
- `pc/src/gs/gs_gl.c` applies those scales to color/depth target allocation, viewport and scissor coordinates, GS-memory uploads, GPU readback, and debug screenshots. Fractional GS-memory uploads sample the source at output-pixel centers; integer scales retain the replication fast path.
- At 4:3 with the default `scale=3`, the visible 640x480 area maps to 1536x1344 (512x448 multiplied by 3). The render target retains the GS's full 512-row allocation; for the 640-pixel-wide target its complete color/depth texture is 1536x1434.

This is the isolated 512x448 base-resolution behavior from `experimental/gs-rebuild`; it does not import that branch's other renderer, cache, or performance changes.

## Files changed

| File | Purpose in this branch |
|---|---|
| `.github/workflows/build-windows.yml` | Build this branch and publish SDL3/libusb runtime DLLs |
| `pc/config/recvx_config.c` | Migrate the settings app to SDL3 |
| `pc/src/gs/gl_funcs.h` | Include SDL3 OpenGL declarations |
| `pc/src/gs/gs_gl.c` | Query drawable pixel size through SDL3; render into the fixed 512x448 GS backing base |
| `pc/src/pc_audio.c` | Migrate audio output to SDL3 audio streams |
| `pc/src/pc_platform.c` | SDL3 window/input/Gamepad APIs, early scratchpad reservation, external mappings loader, 512x448 backing-scale setup |
| `pc/include/pc_gs.h` | Expose separate fractional X/Y backing scales to the GS renderer |
| `pc/tools/build.py` | Link against SDL3 and stage its runtime dependencies |
| `pc/tools/fetch_deps.py` | Build SDL3 3.4.18 with HIDAPI/libusb enabled |

Only these nine implementation/build files are changed relative to the `pc-port` base, plus this `DEVELOPMENT_LOG.md`.

## Explicit exclusions

The following are intentionally not ported into this branch:

- Alpha's indexed texture atlas/CLUT and TIM2 source caches.
- GS renderer/OpenGL redundant-state optimization.
- GS VRAM transfer or swizzle experiments.
- GS draw-state caching or CLUT flush changes.
- CNK/VU0/SSE2 geometry optimizations or profiling.
- UI UV crop/tuning, font sampling, arbitrary/user-selectable base render dimensions (the 512x448 base is fixed), VSync configuration, and decal/depth changes.
- Any movie/video implementation changes.
- Startup tracing or unrelated diagnostic infrastructure.

## Validation status

The SDL3 + early-scratchpad baseline was verified by GitHub Actions run #480 on code commit `9938d7891508ee2a1f4740792872cf30166e62a1` (Build, executable verification, and artifact upload succeeded). That run predates the 512x448 backing-buffer change documented here.

**The new 512x448 code has not yet been build-verified.** A fresh Actions run is required for this revision. CI validates compilation and packaging, not rendered output; no runtime image/parity test for the reduced backing base is claimed here.

## Maintenance rule

Keep this branch limited to the three goals above. Record any later changes here, and do not merge unrelated Alpha/GS-Rebuild work into this reconstruction.


# reconstruction/2-Vsync_and_moviesSD_fix

Based on `reconstruction/1-scratchpadAndSDL3` at `8d556cf868530b1f5a10722662486ae2ac86b181`.
The parent branch's Windows Actions build #483 passed before this branch was created.

## Changes

- `pc/src/pc_movie.c`: added `movies_sd_gl=0|1` selection for standard-definition movies. The default GS route preserves the GIF transfer into GS VRAM and refreshes the persistent GS-VRAM texture view. The GL route ports Alpha's direct frame upload into the dedicated movie texture. Upscaled movies remain on their existing GL path controlled independently by `movies_hd`.
- `pc/src/gs/gs_gl.c`: added the persistent 512x512 movie-VRAM texture used by the GS route. It is allocated once and updated with `glTexSubImage2D`; the GS texture address selects this view while the movie is active.
- `pc/src/pc_platform.c`: added `vsync=0|1`, defaulting to `1`, and applied it with SDL3 after GL context creation.
- `pc/README.md`: documented the new INI settings.

## Validation

Windows build for this branch is pending.


## Default movie scaling preference

- The temporary change to default `movies_hd=0` was reverted.
- The branch now preserves the `pc-port` behavior: `movies_hd` defaults to `1`, the settings checkbox is checked when the key is absent, and disabling the option writes `movies_hd=0`.


# reconstruction/3-UI_Fixes

Based on `reconstruction/2-Vsync_and_moviesSD_fix` at `55d0ec649801e8e444fff781d63b6dabc16cf63f`.

This branch restores only the font and menu/UI graphics fixes from the final `experimental/gs-rebuild` source. The original SDL3 platform, movie paths, VSync setting, and 512x448 GS backing base from reconstruction/2 remain intact.

## Restored fixes

- **Font glyph UV correction:** `ps2_NaDraw2DM` now applies the PC glyph-edge offsets `left=4, right=-4, top=4, bottom=-4` (GS 1/16-texel units). The correction is always active; `fontUVfix=1` enables the live tuner, not the correction itself.
- **Font filtering:** `fontFilter` defaults to `1` (bilinear). The FONT tuner can switch to nearest with `R`; the font-only marker is preserved through deferred OT/DMA processing and consumed by the GS draw state and GL sampler.
- **Menu/UI UV crop:** restored the guarded per-edge crop that maps screen X/Y to the corresponding U/V axis, including mirrored and clockwise-90-degree mappings. Defaults copied from the final GS-rebuild source are standard `left=-8, right=18, top=-14, bottom=16` and clockwise-90 `left=-4, right=8, top=10, bottom=12` (all in 1/16-texel units).
- **Runtime tuning:** `fontUVfix=1` and `menuUVfix=1` enable their respective modes in the shared F10 selector. Escape exits; Tab selects an edge; Up/Down adjust it; Shift changes the adjustment step to 4; Backspace resets. In MENU mode, `R` switches between standard and clockwise-90 profiles. In FONT mode, `R` toggles font filtering. Tuned values persist in `recvx.ini`.

## Files

| File | Change |
|---|---|
| `pc/include/pc_platform.h` | Export font and menu UV/filter controls |
| `pc/src/pc_platform.c` | Font correction settings, font filter state, shared F10 tuner and SDL3 event routing |
| `pc/src/gs/gs_internal.h` | Carry the transient font-filter marker in `GsDrawState` |
| `pc/src/gs/gs.c` | Preserve the marker across GS DMA/GIF transfer and deferred draw state |
| `pc/src/gs/gs_gl.c` | Force nearest sampling only for tagged font glyph draws |
| `src/ps2/veronica/prog/ps2_NaDraw2D.c` | Apply per-edge PC font glyph UV correction and bracket font draws |
| `src/ps2/veronica/prog/ps2_dummy.c` | Restore guarded menu/UI UV crop profiles and tag font OT payloads |

No movie/video, general texture-cache, VRAM, renderer-performance, or non-UI UV changes from GS-rebuild were imported.

## Validation

The source comparison against `experimental/gs-rebuild` confirms that `ps2_NaDraw2D.c` and `ps2_dummy.c` are copied verbatim, as are the narrowly scoped GS state files where their only GS-rebuild additions are font-filter support. The platform tuner code was transplanted into the reconstruction/2 SDL3 platform implementation rather than replacing that file.

**Windows build and runtime validation are pending.** The current Windows build workflow push filter does not yet include this branch, so the progress-report Actions runs are not evidence of a successful game build.
