# Development Log — GS Rebuild

This log records the staged reconstruction of the PC port from the clean `pc-port` baseline.
Changes are recorded chronologically, with working fixes kept separate from diagnostics and
experiments. Each step should remain small enough to build, test, and either retain or revert
independently.

## Working Rules

- Keep `pc-port` unchanged; all reconstruction work belongs on `experimental/gs-rebuild`.
- Restore known-good fixes selectively. Do not merge entire experimental branches.
- Separate required compatibility fixes, correctness fixes, diagnostics, and performance experiments.
- For each step, record the hypothesis, source/reference commit, files changed, validation performed,
  and outcome.
- Do not describe a change as validated until it has been built and exercised in the game.

---

## 2026-11-01 — Step 1: Reserve the PS2 Scratchpad Before OpenGL Initialization

**Category:** Startup / address-space compatibility fix  
**Status:** Implemented; startup validated by user; full regression pending  
**Reference:** `experimental/videofixing` commit `d5bfca1`  
**Implementation commit on this branch:** `0d084e8`

### Problem

The game requires its PS2 scratchpad at the fixed virtual address `0x70000000`.
The previous PC startup sequence created the SDL window and OpenGL context before reserving
that address range. Loading the graphics driver can occupy the required range, causing the
scratchpad allocation to fail during startup.

### Change

In `pc/src/pc_platform.c`, `pcVideoInit()` now reserves the scratchpad immediately after
successful `SDL_Init()`, before creating the window or OpenGL context.

The resulting order is:

1. Install diagnostics and initialize SDL.
2. Reserve the PS2 scratchpad at `0x70000000`.
3. Configure OpenGL and load video settings.
4. Create the SDL window and OpenGL context.
5. Initialize the GS renderer with the previously reserved scratchpad.

The allocation failure remains an explicit fatal startup error. This change moves the reservation
earlier; it does not guarantee that the address range is available on every Windows process layout.

### Files Changed

- `pc/src/pc_platform.c`
  - Added a function-scope scratchpad pointer in `pcVideoInit()`.
  - Moved `pcOsAllocScratchpad()` before window/context creation.
  - Passed the reserved pointer to `pcGsInit()` after the OpenGL context is ready.

No GS memory, texture-cache, VRAM, renderer, or game texture-management logic was changed.

### Validation

- **Source review:** Confirmed the change is limited to `pc/src/pc_platform.c`.
- **Branch comparison:** At implementation commit `0d084e8`, the branch differed from
  `pc-port` only in that file.
- **Build:** Windows Actions build passed for the SDL3 migration tip, which includes this scratchpad change.
- **Runtime startup:** User confirmed that the resulting SDL3 build starts successfully.
- **Game/scene regression:** Not yet completed.

### Next Checkpoint

The SDL3 build now launches successfully according to the user. Continue validating gameplay and
individual platform subsystems separately; do not reopen the scratchpad allocation strategy unless
a reproducible startup or address-space failure appears.

## 2026-11-01 — Step 2: Migrate the Clean PC Port to SDL3

**Category:** Platform / dependency migration  
**Status:** Implemented; Windows build and startup validated; subsystem regression pending  
**Reference migration:** `SDL3` branch commit `d9497ce`  
**Additional SDL3 ownership correction:** `SDL3` branch commit `3eee5d1`  
**Implementation commit on this branch:** `2bbe52c`

### Scope and Rationale

Move the clean PC port from SDL 2.32.10 to SDL 3.4.18 before continuing the GS memory reconstruction.
The existing SDL3 branch was not cherry-picked because its ancestry includes the `graphics-fixes`
work. Instead, the SDL3 API and build changes were selectively ported onto this branch, preserving
the scratchpad fix and leaving graphics/renderer experiments out.

SDL3 Gamepad support and the HIDAPI/libusb build configuration were retained so the Wii U GameCube
adapter path remains available for later controller testing.

### Changes

- Updated window, display, event, cursor, threading, and Gamepad APIs for SDL3.
- Replaced the SDL2 audio-device callback with an SDL3 audio stream callback while retaining the
  existing float stereo mixer and voice/resampling logic.
- Updated the settings application to SDL3 display-mode and Gamepad APIs.
- Updated OpenGL integration to include SDL3 headers and use
  `SDL_GetWindowSizeInPixels()` instead of the removed drawable-size API.
- Updated the build to use SDL 3.4.18, build SDL from source with HIDAPI/libusb enabled, link
  `SDL3.lib`, and package `SDL3.dll` plus `libusb-1.0.dll`.
- Updated the Windows workflow to run on `experimental/gs-rebuild`, verify the SDL3 runtime DLLs,
  and include both DLLs in the branch/commit-named artifact.
- Fixed SDL3 base-path ownership: `SDL_GetBasePath()` returns SDL-owned cached data, so
  `pcPlatformExeDir()` no longer calls `SDL_free()` on it.

### Files Changed

- `.github/workflows/build-windows.yml`
- `README.md`
- `pc/config/recvx_config.c`
- `pc/src/gs/gl_funcs.h`
- `pc/src/gs/gs_gl.c`
- `pc/src/pc_audio.c`
- `pc/src/pc_platform.c`
- `pc/tools/build.py`
- `pc/tools/fetch_deps.py`

### Explicitly Excluded

The migration deliberately does **not** bring across the unrelated `graphics-fixes` work:
custom internal resolution, UV crop/tuning and font fixes, UI wireframe diagnostics, or configurable
VSync. It also does not import GS VRAM, texture-cache, render-target, movie, or frame-rate
optimizations from `alpha` or `performance-sdl3`.

The scratchpad reservation remains immediately after successful `SDL_Init()` and before
`SDL_CreateWindow()` / OpenGL context creation.

### Validation

- **Source review:** The SDL3 migration was applied to the nine files listed above.
- **Static checks:** Confirmed SDL 3.4.18 dependency selection, SDL3 audio-stream/Gamepad APIs,
  SDL3 event names, absence of the checked SDL2 API/link symbols, and SDL3 runtime DLL packaging.
- **Scratchpad order:** Confirmed `pcOsAllocScratchpad()` remains before `SDL_CreateWindow()`.
- **Branch comparison:** This migration commit changes only the nine files listed above relative
  to the preceding log commit.
- **Windows build:** Passed on GitHub Actions run 41332210307; the workflow verified both executables and the SDL3/libusb runtime DLLs and uploaded the artifact.
- **Runtime startup:** User confirmed that the generated build starts successfully.
- **Audio, keyboard/mouse, controller, GameCube adapter and in-game regression:** Not yet fully verified.

### Next Checkpoint

The Windows Actions build and user-confirmed startup checkpoint have passed. Validate audio and
input behavior during subsequent targeted checks. Keep graphics and GS changes out of this step.

## 2026-11-01 — Step 3: Load Optional SDL3 Gamepad Mappings at Startup

**Category:** Platform / controller configuration  
**Status:** Implemented; build and controller runtime validation pending  
**Reference API:** SDL3 `SDL_AddGamepadMappingsFromFile()` (available since SDL 3.2.0)  
**Implementation commit on this branch:** `bb3d105`

### Problem

SDL3 and HIDAPI/libusb support were enabled in Step 2, but the port did not yet load a
user-provided SDL gamepad mapping database. This left controller recognition dependent only
on SDL's built-in mappings.

### Change

In `pc/src/pc_platform.c`, a new `load_gamepad_mapping_database()` helper checks for
`gamecontrollerdb.txt` beside `recvx.exe` and loads it with
`SDL_AddGamepadMappingsFromFile()`.

The helper is called from `pcPadInit()` after SDL initialization and before joystick enumeration
and `SDL_IsGamepad()`. This allows custom entries to add mappings or replace an existing mapping
before the port chooses a gamepad.

The file is optional:
- If it is absent, the game logs that SDL's built-in mappings will be used.
- If loading fails, the error is logged and startup continues.
- If loading succeeds, the number of accepted mappings is logged.

No database file is bundled or copied into the build artifact; the user-provided
`gamecontrollerdb.txt` must be placed beside `recvx.exe`.

### Files Changed

- `pc/src/pc_platform.c`
  - Added `load_gamepad_mapping_database()`.
  - Called it from `pcPadInit()` before `SDL_GetJoysticks()` and `SDL_IsGamepad()`.

### Validation

- **Source review:** Confirmed the mapping loader runs after SDL initialization and before
  controller enumeration.
- **Build:** Passed on GitHub Actions run 41333335884 for commit bb3d105.
- **Runtime mapping test:** Pending. Test with a known custom mapping file and confirm the log
  reports accepted mappings and the expected controller name/layout.
- **Missing-file behavior:** Source path is non-fatal; runtime log behavior still needs testing.

### Next Checkpoint

Verify the Windows build, then place a valid `gamecontrollerdb.txt` beside `recvx.exe` and
confirm that the custom mapping is loaded before the controller is opened. Also test startup
without the file to confirm SDL's built-in mappings remain available.


## 2026-11-01 — Step 4: Use a 512x448 Native GS Rendering Base

**Category:** Graphics correctness / internal render resolution  
**Status:** Implemented; Windows build in progress; visual/runtime validation pending  
**Reference:** Earlier internal-resolution implementation in `graphics-fixes` commit `dc2bf1a`  
**Implementation commit on this branch:** `1fa70a3`

### Problem

The clean PC port currently rasterizes its 640x480 logical GS frame at a 640x480 base.
The intended native PS2 GS backing base for this game is 512x448. Changing the logical game
dimensions themselves would be incorrect because game coordinates, display layout, and UI
code continue to use the 640x480 coordinate space.

### Change

Keep the game's logical coordinate space at 640x480, but derive independent floating-point
horizontal and vertical backing-buffer scales from a 512x448 native GS base:

- Horizontal backing scale retains the existing aspect-ratio adjustment and applies the
  512/640 base-width ratio.
- Vertical backing scale applies the 448/480 base-height ratio.
- The existing `scale=` setting remains a supersampling multiplier; it does not redefine
  the native logical GS base.

In `pc/src/gs/gs_gl.c`, render-target and depth-texture allocation, VRAM-to-render-target
loading, local-to-local readback, viewport, scissor, and debug screenshots now use the
independent floating-point scales. VRAM-to-render-target loads use nearest-neighbour sampling
when converting the logical GS buffer into the smaller backing buffer. Integer supersampling
factors retain the replication fast path.

This is a renderer-coordinate change, not a rewrite of game coordinates or a change to the
PC window size.

### Files Changed

- `pc/src/pc_platform.c`
  - Defines the 512x448 native GS backing base.
  - Calculates the effective horizontal and vertical scales while leaving 640x480 logical
    coordinates and window defaults unchanged.
- `pc/include/pc_gs.h`
  - Changes the horizontal scale declaration to floating point and adds the vertical scale.
- `pc/src/gs/gs_gl.c`
  - Adds coordinate-to-backing-pixel helpers.
  - Applies the new scales to render targets, depth buffers, VRAM transfers, viewport,
    scissor, and screenshots.

### Explicitly Excluded

- No UI UV crop, font sampling, VSync, or other graphics-fixes changes were imported.
- No GS VRAM allocator, page-generation, texture-cache, movie, or memory-management behavior
  was changed.
- The logical 640x480 game coordinate space and PC window configuration were not changed.

### Validation

- **Source review:** Confirmed that the logical 640x480 dimensions remain unchanged and
  backing dimensions are derived independently.
- **Windows build:** GitHub Actions run 41333587254 is in progress.
- **Runtime/visual validation:** Pending. Check menus, 3D scenes, render-target effects,
  FMVs, and screenshots at the default scale and aspect ratio.

### Next Checkpoint

Complete the Windows build, then inspect the generated build and test representative game
scenes. Confirm that the 512x448 base does not introduce clipping, incorrect aspect mapping,
render-target desynchronization, or screenshot regressions before starting the GS/VRAM audit.



## 2026-11-01 — Step 5: Add Opt-In GS VRAM / Texture-Coherency Trace

**Category:** Diagnostic instrumentation only  
**Status:** Implemented on `debug/gs-memory`; build and A/B runtime capture pending  
**Purpose:** Compare the same game session with movies skipped versus movies played, observing
the common GS memory path used by movie frames, TIM2 model textures, CLUTs, render-target
writebacks, and the OpenGL texture cache.

### Changes

- Added an opt-in trace file selected by `RECVX_GS_TRACE`. It is opened in overwrite mode once
  per process and uses buffered writes, flushing every 128 events. Normal process shutdown flushes
  the remainder. Without the variable, no trace file is created.
- Added source labels for normal movie uploads (`MOVIE`) and TIM2 uploads
  (`TIM2_CLUT` / `TIM2_IMAGE`). HD movie frames are marked as bypassing GS VRAM.
- Logged host-to-local transfer registers, completion/abortion, pixels processed, and the physical
  8 KiB GS pages whose contents actually changed. A page-writer transition means same-page reuse,
  not proven byte-level overlap.
- Logged local-to-local copies and render-target-to-VRAM writebacks, including changed pages.
- Logged texture-cache hits (once per entry per frame), misses, evictions, sample extents, page
  geometry, generations, and CLUT hashes.
- Logged CLUT load decisions, before/after hashes, TEXFLUSH observations, and render-target
  overlap/invalidation decisions.
- No allocator, transfer dimensions, VRAM contents, cache policy, decoder, movie lifetime, or
  cleanup behavior was intentionally changed.

### Files Changed

- `pc/src/gs/gs_internal.h` — shared trace declarations.
- `pc/src/gs/gs_mem.c` — trace writer, page tracking, transfer and local-copy events.
- `pc/src/gs/gs.c` — CLUT lifecycle and TEXFLUSH events.
- `pc/src/gs/gs_gl.c` — texture-cache and render-target coherency events.
- `pc/src/pc_movie.c` — normal/HD movie route and termination events.
- `src/ps2/veronica/prog/ps2_loadtim2.c` — TIM2 CLUT/image source labels and asset metadata.
- `DEVELOPMENT_LOG.md` — this entry.

### How to Capture the A/B Logs

Use two fresh game processes and separate paths. The trace path is relative to the process working
directory unless absolute; each launch overwrites its selected trace file.

For this experiment, set `movies_hd=0` in `recvx.ini` for **both** tests. Otherwise, when an
upscaled movie exists, the game may select the HD route, which uses a separate OpenGL texture and
does not write movie frames into GS VRAM. The trace will show `MOVIE_HD_FRAME` if that happens.

**Windows CMD — test A (skip every movie):**

```bat
set RECVX_GS_TRACE=vram_no_movies.log
recvx.exe
```

**Windows CMD — test B (play every movie):**

```bat
set RECVX_GS_TRACE=vram_with_movies.log
recvx.exe
```

Reach the same reference room from the same starting save, follow the same route, wait the same
duration (2 minutes), inspect the same area, then close normally. Do not change the executable,
graphics settings, movie conversion files, or other configuration between tests.

For PowerShell, set `$env:RECVX_GS_TRACE` to the corresponding filename before launching each
process.

### Interpreting the Trace

- Compare `TRANSFER_BEGIN` / `TRANSFER_END` by source, TBP, TBW, PSM, rectangle, and changed pages.
- `PAGE_WRITER_CHANGE` means two source labels changed some bytes in the same 8 KiB GS page.
  Sharing a page alone does not establish byte-level overlap.
- Compare texture-cache hits/misses to preceding page changes and each texture's page geometry.
- Compare CLUT snapshots and render-target writeback/invalidation events.
- Confirm the normal movie path appears as `source=MOVIE`, not `MOVIE_HD_FRAME`.

### Validation

- **Source review:** Instrumentation is opt-in; no functional policy change was intended.
- **Build:** Not run in this environment.
- **Runtime A/B capture:** Pending the two controlled sessions.
- **Diagnostic limit:** Page ownership is tracked at 8 KiB granularity, so page transitions are
  candidates for reuse, not exact byte-overlap reports. Each transfer retains its GS coordinates
  and dimensions for further reconstruction.


### Post-commit review

A follow-up review corrected escaped line terminators in the trace writer, initializes the trace
frame label before the first rendered frame, and includes raw TEX0 plus CLUT page metadata in
cache events. Movie-open events now also record whether the HD bypass was selected. These are
diagnostic-output corrections only.


The trace writer uses a 64 KiB output buffer and flushes every 128 events to limit synchronous
disk I/O during the capture. Closing the game normally flushes the remaining buffer.
