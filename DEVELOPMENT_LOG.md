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



## 2026-11-01 — Reuse a Dedicated 1 MiB GS Surface for Normal FMVs

**Category:** GS renderer / normal movie texture memory  
**Status:** Implemented; Windows build and runtime validation pending  
**Branch:** `experimental/gs-rebuild`

### Change

- Allocate one persistent 512x512 `GL_RGBA8` texture for normal (non-`MOVIE_HD`) movie playback.
- After each frame is transferred into emulated GS VRAM, decode the active movie rectangle from that VRAM and update the same GL texture with `glTexSubImage2D()`.
- While the movie is active, sample the persistent texture for its GS texture base pointer instead of creating entries in the normal GS texture cache.
- Flush pending renderer work before replacing the movie surface and disable the special sampling path after playback.
- Leave the generic GS texture cache, render targets, and `MOVIE_HD` path unchanged.

### Memory Scope

The dedicated movie texture is 512x512 RGBA8: 1,048,576 bytes (1 MiB) nominal GPU storage. It is allocated once and updated in place; no per-frame GL textures are created. The existing shared `decode_buf` remains CPU-side scratch storage and is not another video texture. The emulated GS VRAM remains its independent 4 MiB allocation.

No movie-specific runtime counters, periodic debug messages, or per-frame timing instrumentation were added.

### Validation

- **Source review:** Confirmed the movie-only path bypasses the generic texture cache only while the normal FMV is active.
- **Windows build:** Pending GitHub Actions.
- **Runtime:** Pending. Verify short and long normal FMVs, transition back to menus/gameplay, and that ordinary texture cache behavior is unchanged.


## 2026-11-01 — Step 5: Optimize 32-bit Host-to-GS Image Transfers

**Category:** GS memory / transfer performance  
**Status:** Implemented; Windows build passed; performance improvement reported by user  
**Branch:** `experimental/gs-rebuild`  
**Reference:** Alpha branch `pc/src/gs/gs_mem.c` implementation of `transfer32Run()` and page-touch coalescing  
**Implementation commit:** `c1158bd`

### Problem

The host-to-GS IMAGE path for PSMCT32 and PSMZ32 originally handled every 32-bit pixel separately. For each pixel, it called the generic memory-write path and advanced the transfer coordinates. That repeats address/swizzle and transfer-advance work even when a transfer contains many complete, aligned GS blocks.

The GS stores 32-bit pixels in 8×8 blocks (64 pixels / 256 bytes), with a fixed swizzled column order. When a transfer is aligned and contains complete blocks, the emulator can assemble one block in that physical order and compare/copy it as a unit instead of resolving and writing all 64 pixels independently. Partial or misaligned transfers still need a per-pixel fallback.

There was also redundant page-generation work. `gsPageGen` is used as a texture-cache invalidation generation, not as a count of pixel writes. Incrementing the same page's generation for every changed pixel in one transfer batch provides no additional invalidation information after the first increment, but repeats the bookkeeping.

### Scope and Rationale

Apply the Alpha 32-bit transfer optimization to GS-Rebuild while keeping the change isolated to GS memory:

- Use complete 8×8 block assembly for aligned PSMCT32/PSMZ32 data.
- Retain the horizontal run fallback for partial rows and misaligned rectangles.
- Coalesce page-generation increments during each call to `gsMemTransferData()`. This applies to every format handled by that function, because all of those writes use the same `touch()` invalidation path—not only the 32-bit fast path.
- Leave writes made outside `gsMemTransferData()` with their original per-write generation increments.

The expected benefit is less CPU work in bulk host-to-GS transfers and less repeated invalidation bookkeeping. This is a performance hypothesis; the actual gain and any scene-specific effect still require measurement.

### Change

In `pc/src/gs/gs_mem.c`:

- Added Alpha's `transfer32Run()` path for PSMCT32 and PSMZ32 host-to-GS IMAGE transfers.
- Complete aligned 8×8 blocks are assembled in GS swizzled column order, then compared and copied as 256-byte blocks.
- Transfers that do not meet the complete-block conditions use the horizontal run path, processing up to the next 8-pixel block boundary while reusing the page/block calculation.
- Added Alpha's `pageTouchEpoch` invalidation coalescing. Each modified 8 KB page advances `gsPageGen` at most once per call to `gsMemTransferData()`, regardless of how many pixels in that batch change.
- Kept the existing handlers for the non-32-bit transfer formats unchanged; they benefit only from the shared page-generation coalescing.

### Files Changed

- `pc/src/gs/gs_mem.c`
  - Added `transfer32Run()`.
  - Updated `touch()` and `gsMemTransferData()` for page-generation coalescing.
- `DEVELOPMENT_LOG.md`
  - Recorded the motivation, scope, implementation, and pending validation.

### Explicitly Excluded

- No INI setting or runtime mode selection.
- No additional transfer buffer or texture cache.
- No renderer, movie, VRAM-size, or unrelated profiling changes.

### Validation

- **Source comparison:** Confirmed the 32-bit column table and `addr32()` implementation match Alpha before copying its fast path.
- **Code review:** Confirmed page-generation coalescing is active only inside `gsMemTransferData()` and is reset at the end of the call. Non-32-bit transfer handlers remain in place.
- **Build:** Passed on GitHub Actions run [41383491046](https://github.com/zedhqx4/recvx-pc/actions/runs/41383491046) for implementation commit `c1158bd`.
- **Runtime performance:** User reports that the 8×8 transfer optimization raises performance from approximately 22–25 FPS to 30 FPS in many areas.
- **Further regression:** Continue checking scene transitions and texture correctness; the reported improvement is not a claim that every area has been measured.


## 2026-11-01 — Step 6: Add Configurable OpenGL VSync

**Category:** Video presentation / frame synchronization  
**Status:** Implemented; Windows build and runtime validation pending  
**Branch:** `experimental/gs-rebuild`  
**Reference:** Alpha branch `pc/src/pc_platform.c` VSync configuration and swap-interval handling

### Problem

GS-Rebuild currently calls `SDL_GL_SetSwapInterval(0)` unconditionally after creating the OpenGL context. This requests unsynchronized buffer swaps regardless of the user's display configuration.

The game's emulated VBLANK/frame pacing and the display's vertical refresh synchronization are separate things. Timing the emulated GS around VBLANK does not by itself request that OpenGL present each completed frame at a display refresh boundary. With swap interval zero, presentation may occur mid-refresh and produce visible tearing.

### Scope and Rationale

Restore the same user-controlled VSync behavior already present in Alpha:

- Enable VSync by default, matching Alpha.
- Allow the user to disable it through `vsync=0` in `recvx.ini`.
- Apply the requested swap interval only after the OpenGL context exists.
- Query the active interval and log the actual result, rather than assuming that the request succeeded.

This keeps the change limited to presentation synchronization. It does not alter the game's emulated VBLANK scheduler or introduce a new frame limiter.

### Change

In `pc/src/pc_platform.c`:

- Added `video_cfg.vsync` to the video settings structure.
- Load `vsync` from `recvx.ini` with a default of `1`.
- After creating the OpenGL context, call `SDL_GL_SetSwapInterval(1)` when enabled or `SDL_GL_SetSwapInterval(0)` when disabled.
- Query `SDL_GL_GetSwapInterval()` and log the active interval. If SDL rejects the request, log the SDL error and the interval reported by the driver.

### Configuration

```ini
vsync=1
```

Use `vsync=0` to request unsynchronized presentation. If the key is absent, VSync is enabled by default. The active interval reported in the log is authoritative if the graphics driver cannot honor the requested setting.

### Files Changed

- `pc/src/pc_platform.c`
  - Added the configuration field and INI load.
  - Replaced the unconditional swap-interval-zero request with Alpha's configurable request, active-interval query, and result logging.
- `DEVELOPMENT_LOG.md`
  - Recorded the rationale, behavior, configuration, and validation status.

### Explicitly Excluded

- No change to emulated VBLANK timing or frame pacing.
- No additional frame limiter or FPS cap.
- No settings-app UI changes; this follows Alpha's `recvx.ini` configuration behavior.
- No GS transfer, renderer, or texture-cache changes in this step.

### Validation

- **Source comparison:** Matched the VSync field, default, INI loading, swap-interval request, active-interval query, and logging against Alpha.
- **Build:** Pending GitHub Actions.
- **Runtime:** Pending. Test with the default setting and with `vsync=0`; confirm the log reports the active swap interval and check for tearing/frame-pacing differences.


## 2026-11-01 — Step 7: Add Runtime Controls for Experimental Decal Priority

**Category:** GS renderer / depth testing / visual diagnostics  
**Status:** Implemented; Windows CI and runtime A/B validation pending  
**Branch:** `experimental/decals-wip`  
**Base:** `experimental/gs-rebuild` at `1ae919a`  
**Reference:** Alpha decal-priority implementation, especially `d91cb966`, `51f77b8`, and `4553e48`

### Problem

Alpha's coplanar decal adjustment was made permanent and lost its runtime switch. The user observed a small visual regression and wants to determine whether polygon offset is responsible, while retaining the ability to tune its magnitude in a live game session.

### Scope and Rationale

Keep Alpha's eligibility conditions and bounded per-frame draw ordering unchanged. Make only the enable state and offset magnitude adjustable at runtime, so A/B testing does not require rebuilding or restarting. The experimental branch starts with the Alpha behavior enabled; the selected values are saved only on an explicit keypad command so a test run cannot silently change the next run's baseline.

### Changes

- Ported Alpha's eligibility predicate: active depth testing, `GEQUAL`, `Z16S`, textured non-FST STQ drawing, blending enabled, and primitive types 3 through 5.
- Preserved Alpha's draw-order priorities: `+1` through `+4`, clamped at `+4`, with the counter reset after each frame's GS flush.
- Added `decal_priority` (default `1`) and `decal_offset_multiplier` (default `1.00`) to `recvx.ini`.
- Kept Alpha's ordinal priority (1..4) unchanged, derived the same base offset from it, then multiplied that offset before passing it to `glPolygonOffset(0.0f, offset)`; the factor remains zero as in Alpha.
- Added runtime numpad controls:
  - `*`: toggle decal priority.
  - `+` / `-`: adjust offset multiplier by `0.25`.
  - `/`: save both current values to `recvx.ini`, then read them back to verify the write.
- Offset multiplier is clamped to `-2.00..4.00` and quantized to quarter-unit steps. Zero neutralizes the offset; negative values reverse its direction.
- Added title-bar feedback and `recvx.log` entries for changes and save verification.
- Consumed these keys in the platform event pump and excluded them from keyboard-to-pad bindings, so they do not reach game controls even if mapped in `recvx.ini`; they are also absent from recorded pad input.
- Enabled the Windows build workflow for this experimental branch.

### Files Changed

- `pc/src/gs/gs_gl.c`
  - Added INI-backed runtime state, offset multiplier normalization, save/readback verification, Alpha's bounded polygon-offset application, and per-frame draw-order reset.
- `pc/src/gs/gl_funcs.h`
  - Added `glPolygonOffset` to the SDL OpenGL function resolver table.
- `pc/include/pc_gs.h`
  - Declared the renderer controls consumed by the platform event pump.
- `pc/src/pc_platform.c`
  - Added the numpad event bindings and transient title-bar feedback.
- `pc/README.md`
  - Documented the branch-specific configuration and controls.
- `.github/workflows/build-windows.yml`
  - Added `experimental/decals-wip` to push-triggered Windows builds.
- `DEVELOPMENT_LOG.md`
  - Recorded the rationale, exact behavior, controls, and pending validation.

### Explicitly Excluded

- No change to Alpha's decal eligibility predicate.
- No change to the +1..+4 draw-order cap or frame reset behavior.
- No changes to GS memory transfers, texture caches, render-target readback, or unrelated rendering state.
- No dependency on `debug=1`; controls are always active on this experimental branch.
- Runtime changes are not written to `recvx.ini` unless Numpad `/` is pressed.

### Validation

- **Source comparison:** Matched the eligibility predicate, zero polygon-offset factor, +1..+4 bounded draw order, and per-frame reset against Alpha.
- **Code review:** Confirmed the polygon-offset enable is disabled immediately after the eligible draw, including its alpha-test multipass.
- **Build:** Pending GitHub Actions for this branch.
- **Runtime:** Pending. Test toggle A/B and offset multiplier values in the exact scene showing the reported visual regression; record the smallest offset multiplier that reproduces or eliminates it.


## 2026-11-01 — Step 8: Restore GS-Native Depth Buffer Precision for Decal Tests

**Category:** GS renderer / depth buffer / decal-priority parity  
**Status:** Implemented; Windows CI and runtime comparison pending  
**Branch:** `experimental/decals-wip`  
**Reference:** Alpha's `get_db()` implementation in `pc/src/gs/gs_gl.c`

### Problem

The decal-priority predicate, draw-order counter, and polygon-offset call had been compared with Alpha, but the two renderers were not using equivalent depth attachments. The WIP renderer created every depth texture as `GL_DEPTH_COMPONENT24`, regardless of the GS `ZBUF.PSM`. Alpha preserves the requested GS depth format. That difference changes the effective granularity of `glPolygonOffset(..., units)`, so equal numeric offset values need not produce the same depth adjustment.

### Changes

- Added the GS depth pixel-storage mode (`ZBUF.PSM`) to the `DepthBuffer` cache key. A buffer created for one precision is no longer reused for another PSM at the same ZBP and dimensions.
- Ported Alpha's format selection:
  - PSM 0 / Z32: `GL_DEPTH_COMPONENT32` with `GL_UNSIGNED_INT`.
  - PSM 1 / Z24: `GL_DEPTH_COMPONENT24` with `GL_UNSIGNED_INT`.
  - Other supported depth formats, including Z16S: `GL_DEPTH_COMPONENT16` with `GL_UNSIGNED_SHORT`.
- Updated the draw path to pass `GSF(st->zbuf, 24, 4)` into `get_db()`.
- Left the decal eligibility predicate, ordinal priorities (+1..+4), offset multiplier, and per-frame reset unchanged.

### Files Changed

- `pc/src/gs/gs_gl.c`
  - Added `DepthBuffer.psm`, PSM-specific GL depth texture formats/types, PSM-aware cache lookup, and the PSM argument at the draw callsite.
- `DEVELOPMENT_LOG.md`
  - Recorded why the precision mismatch matters and the exact Alpha behavior restored.

### Explicitly Excluded

- No change to the decal predicate or draw-order algorithm.
- No change to the offset multiplier or keypad controls.
- No changes to texture caches, GS VRAM transfers, or render-target color attachments.

### Validation

- **Source comparison:** Ported the `get_db()` format selection and PSM-aware identity from Alpha.
- **Code review:** Confirmed the draw path supplies the active `ZBUF.PSM`.
- **Build:** Pending GitHub Actions for this commit.
- **Runtime:** Pending. Compare the same scene against Alpha at multiplier `1.00`; only after parity is confirmed should the multiplier be tuned.


## 2026-11-01 — Step 9: Add an Independent Priority Multiplier Control

**Category:** GS renderer / decal-priority experiments  
**Status:** Implemented; Windows CI and runtime validation pending  
**Branch:** `experimental/decals-wip`

### Request

Allow the decal priority itself to be scaled independently from the polygon-offset units, so the two effects can be tested separately while retaining the Alpha ordinal draw-order sequence.

### Changes

- Added `decal_priority_multiplier` (default `1.00`) alongside the existing `decal_offset_multiplier` (default `1.00`).
- Kept Alpha's ordinal counter and cap unchanged: increment once for each qualifying draw, then clamp the ordinal to 1..4.
- The renderer now calculates:
  1. `priority_value = ordinal_priority * decal_priority_multiplier`.
  2. `offset = priority_value * decal_offset_multiplier`.
  3. `glPolygonOffset(0.0f, offset)`.
- Added Numpad `7` to decrease the priority multiplier and Numpad `9` to increase it, both in `0.25` steps.
- Kept Numpad `+` and `-` dedicated to the offset multiplier. Numpad `*` still toggles the effect; Numpad `/` saves the enabled state and both multipliers.
- Priority multiplier is clamped to `0.00..4.00`; offset multiplier retains its `-2.00..4.00` range. Both are quantized to quarter steps.
- Reserved Numpad `7` and `9` in keyboard-to-pad bindings as well as the event pump.
- Added title-bar and log feedback for both independent values.

### Files Changed

- `pc/src/gs/gs_gl.c`: settings, normalization, persistence/readback verification, independent priority scaling and final offset calculation.
- `pc/include/pc_gs.h`: expanded settings getter and declared priority-multiplier adjustment.
- `pc/src/pc_platform.c`: Numpad 7/9 controls, display feedback and input reservation.
- `pc/README.md`: documented the additional INI key and independent controls.
- `DEVELOPMENT_LOG.md`: recorded rationale and behavior.

### Explicitly Excluded

- No changes to Alpha's decal eligibility predicate, ordinal cap, per-frame counter reset, or depth-buffer precision.
- No changes to the polygon-offset factor (remains zero).
- No changes to unrelated GS texture, transfer, or render-target behavior.

### Validation

- **Source review:** Confirm declarations, definitions and platform call sites agree; verify the ordinal cap occurs before the new priority multiplier.
- **Build:** Pending GitHub Actions for this commit.
- **Runtime:** Pending. Test priority multiplier with offset multiplier held at `1.00`, then test offset multiplier independently with priority multiplier held at `1.00`.


## 2026-11-02 — Step 10: Add an Interactive GS Material-State Inspector

**Category:** Graphics diagnostics / decal identification  
**Status:** Implemented in source; build and in-game validation pending  
**Branch:** `experimental/decals-wip`  
**Base commit:** `d6b90be5a441a0688ba8954cc10325bfa248ae7d`

### Motivation

The broad renderer-side decal predicate and polygon-offset tuning do not identify which
source materials actually represent decals. Before designing an automatic offset rule, collect
a small set of known decal examples and compare their original GS texture, blend, alpha-test and
depth properties.

### Changes

- Disabled the previous automatic decal-priority / polygon-offset experiment with `#if 0`
  blocks, retaining its implementation in the source for reference.
- Added a runtime inspector that collects unique GS material-state signatures from 3D triangle
  batches whose post-projection bounds overlap the 640x480 viewport, GS scissor and normalized
  depth range.
- Added magenta wireframe highlighting for the selected state. The overlay uses the existing
  geometry and shader, bypasses texture/fog/alpha sampling only for the diagnostic pass, and
  restores the GS depth, blend and color-mask state afterward.
- Added a bounded sample table (512 candidates, 64 marked states). Numpad `/` logs the selected
  state's primitive type, TEX0 fields, TEX1, CLAMP, ALPHA, TEST, ZBUF, TEXA, TEXCLUT and PABE.
- Repeated marks of the same GS state are ignored.
- Reassigned the numpad controls: `*` toggles the inspector, `+`/`-` cycle candidates, and
  `/` marks the selected candidate. The previous offset and priority multiplier controls are
  no longer active.

### Files Changed

- `pc/src/gs/gs_gl.c`
  - Retained the old settings and priority implementation under disabled `#if 0` blocks.
  - Added viewport/scissor/depth candidate filtering, GS-state signature comparison, sample
    registration, mark logging and magenta wireframe rendering.
- `pc/src/gs/gl_funcs.h`
  - Added the OpenGL `glPolygonMode` entry required for the wireframe pass.
- `pc/include/pc_gs.h`
  - Replaced the old decal-priority control declarations with material-inspector controls.
- `pc/src/pc_platform.c`
  - Rebound Numpad `*`, `+`, `-` and `/`; these remain consumed before game input recording.
- `pc/README.md`
  - Documented the inspector controls, candidate semantics, recorded GS properties and limits.
- `DEVELOPMENT_LOG.md`
  - Recorded this diagnostic step and its validation status.

### Important Scope Notes

- The candidate is a GS material-state signature, not a source-level CNK material pointer.
  Distinct game materials that produce identical GS state can share one candidate.
- The frustum filter is a conservative post-projection bounding-box intersection against the
  viewport, GS scissor and depth range; it is not exact polygon clipping.
- No depth offset, polygon offset, material replacement, texture mutation, UV change or geometry
  mutation is performed by the inspector.
- The old automatic decal heuristic is deliberately inactive so it cannot bias the inspection.

### Validation

- **Source review:** Pending final review of generated diff.
- **Build:** Not run in this environment; GitHub Actions should build the branch after push.
- **Runtime:** Pending. Verify activation, candidate cycling, wireframe visibility, marking/log
  output, and that disabling the inspector restores ordinary rendering.


## 2026-11-02 — Step 11: Restore the Bounded GS Decal Depth-Priority Rule

**Category:** GS renderer / decal depth priority  
**Status:** Implemented in source; build and runtime validation pending  
**Branch:** `experimental/decals-wip`  
**Reference:** Fixed eligibility and +1..+4 polygon-offset behavior from `alpha`

### Motivation

Three runtime material-inspection runs produced repeatable GS-state signatures for surfaces
reported as decal problems. The marked states include both conventional and additive GS blend
equations, while sharing the broad depth/texture conditions used by the previous Alpha rule.
The inspector is diagnostic only; the priority heuristic had been retained under disabled
`#if 0` blocks on this branch.

### Changes

- Restored the Alpha fixed eligibility predicate in `gsRendererDraw()`:
  - Depth testing is active and `ZTST=GEQUAL`.
  - `ZBUF.PSM=10` (Z16S).
  - Texturing is enabled, texture coordinates are perspective-correct (`FST=0`), and ABE is enabled.
  - Primitive type is a 3D triangle class (`3..5`).
- For each eligible draw, increment a frame-local ordinal, clamp it to 1..4, and apply
  `glPolygonOffset(0.0f, priority)` with `GL_POLYGON_OFFSET_FILL` enabled only for that draw.
- Disable polygon offset immediately after the normal draw, including the GS alpha-fail multipass.
- Reset the draw-order counter after flushing each presented frame.
- Kept the material inspector, its candidate/mark tables, keyboard controls and log format unchanged.
- Did not add checks for `ALPHA`, texture `TBP`, texture PSM or texture dimensions. Both blend-equation variants
  observed in the logs remain eligible when the broad GS predicate matches.
- Did not restore the old runtime multipliers or their INI/keypad controls.

### Files Changed

- `pc/src/gs/gs_gl.c`: active predicate, draw-order counter, bounded offset application/cleanup and per-frame reset.
- `pc/README.md`: documented the independent runtime heuristic and its broad-match limitation.
- `DEVELOPMENT_LOG.md`: recorded the restored rule and pending validation.

### Validation

- **Source comparison:** Compared the predicate, +1..+4 cap, zero factor and per-frame reset against Alpha.
- **Log evidence:** Marked examples in the latest capture use textured perspective 3D triangles, ABE=1,
  ZTST=GEQUAL and ZBUF.PSM=10; the captured ALPHA equations include both `0x44` and `0x48`.
- **Build:** Pending GitHub Actions.
- **Runtime:** Pending. Confirm the log message `gs: coplanar decal priority active` appears, retest the known
  problem surfaces, and check that ordinary transparent geometry does not show unacceptable regressions.
