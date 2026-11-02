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

## 2026-11-02 — Step 7: Port the Allowlisted Decal Depth Correction

**Category:** GS renderer / coplanar decal depth priority  
**Status:** Implemented in source; Windows build and runtime validation pending  
**Branch:** `experimental/gs-rebuild`  
**Reference:** `882fea544de02290194fd436559ef7887a86f9cb` (`experimental/decals-wip`)

### Changes

- Ported the 25 exact GS material signatures and their common register-state checks.
- Kept the exact TEX0 and ALPHA comparisons; the two recorded arms/shirt negative controls are not in the allowlist.
- Applied draw-order priority 1–4 (clamped at 4) with `glPolygonOffset(0.0f, priority * 0.50f)`.
- Reset the priority counter at the presented-frame boundary.
- Enabled polygon offset only around the matched draw and disabled it immediately afterward, including the alpha-test multipass path.
- Added only the required `glPolygonOffset` OpenGL function entry to the renderer's loader.

### Explicitly Excluded

- No material inspector, candidate collection, wireframe highlighting, marking controls, or diagnostic logging.
- No new keyboard bindings, INI settings, runtime controls, or environment variables.
- No changes to depth formats, blend/alpha tests, geometry, UVs, texture memory, or unrelated renderer behavior.

### Validation

- **Source review:** Confirmed 25 allowlist entries, exact common-field/TEX0/ALPHA matching, 1–4 priority cap, half-scale units, frame reset, and per-draw cleanup.
- **Build:** Pending GitHub Actions.
- **Runtime:** Pending. Retest the previously marked decal surfaces and the arms/shirt negative controls in the same scene.



## 2026-11-02 — Step 8: Restore GS ZBUF Precision for Decal Depth Priority

**Category:** GS renderer / depth attachment format  
**Status:** Implemented in source; Windows build and in-game validation pending  
**Branch:** `experimental/gs-rebuild`  
**Reference:** PSM-aware `get_db()` implementation from `experimental/decals-wip`

### Problem

The initial decal-priority port preserved the allowlist and polygon-offset behavior but did not port the depth-buffer allocation logic. `GS-Rebuild` allocated every OpenGL depth attachment as `GL_DEPTH_COMPONENT24`, regardless of the active `ZBUF.PSM`. This was a concrete mismatch with the reference renderer: the marked decal signatures use `ZBUF=0x0A00012C`, whose PSM field is Z16S (10), but were therefore rendered against a D24 attachment.

The depth attachment's format also affects the effective resolution of depth values and polygon-offset units. Matching only ZBP and dimensions could incorrectly reuse an attachment created for another GS depth format.

### Changes

- Added `ZBUF.PSM` to the `DepthBuffer` cache identity.
- Restored the reference format selection:
  - PSM 0 / Z32: `GL_DEPTH_COMPONENT32`, `GL_UNSIGNED_INT`.
  - PSM 1 / Z24: `GL_DEPTH_COMPONENT24`, `GL_UNSIGNED_INT`.
  - Other supported depth formats, including Z16S (PSM 10): `GL_DEPTH_COMPONENT16`, `GL_UNSIGNED_SHORT`.
- Updated `get_db()` to require both ZBP and PSM, as well as width and height, when reusing a depth attachment.
- Updated `gsRendererDraw()` to pass `GSF(st->zbuf, 24, 4)` into `get_db()`.
- Added guarded definitions for the D16 and D32 OpenGL depth-format constants.

### Explicitly Excluded

- No changes to the 25-entry decal allowlist or its common GS-state checks.
- No changes to the 1–4 draw-order cap, `glPolygonOffset(0.0f, priority * 0.50f)`, frame reset, or per-draw enable/disable scope.
- No changes to `z_scale()`, depth comparison direction, geometry, UVs, texture memory, or unrelated renderer behavior.
- The existing bounded fallback when all `MAX_DB` slots are occupied remains unchanged.

### Files Changed

- `pc/src/gs/gs_gl.c`
  - Added `DepthBuffer.psm`, PSM-specific GL attachment allocation, PSM-aware lookup, and the PSM argument at the draw callsite.
- `pc/src/gs/gl_funcs.h`
  - Added guarded D16 and D32 depth-format constants.
- `DEVELOPMENT_LOG.md`
  - Recorded the format mismatch, correction, scope and validation status.

### Validation

- **Source comparison:** Ported the PSM-to-format selection and PSM-aware cache key from `experimental/decals-wip`.
- **Code review:** Confirmed the decal signature's ZBUF PSM (10) now selects D16, while Z32 and Z24 select D32 and D24 respectively.
- **Build:** Pending GitHub Actions.
- **Runtime:** Pending. Retest the same coplanar decal scene and negative controls. This source correction is not yet considered a verified visual fix.


## 2026-11-02 — Step 9: Correct Font Glyph UV Sampling

**Category:** Graphics correctness / 2D font texture sampling  
**Status:** Implemented in source; Windows build and visual validation pending  
**Branch:** `experimental/gs-rebuild`  
**Reference:** `alpha` implementation; originally from Aikatore's `recvx-decomp` commit `b4212c43faf4d88c3e7d73730517971289713f53`, recorded in `recvx-enhanced-port` commit `d01d2801133d0ee63272e2c686de2d206182e7cb` (fmil95)

### Problem

At higher internal resolutions, font glyph quads could sample into the neighbouring glyph cell. The far-side UV coordinates sat on the far texel's edge, producing visible seams/bleed around letters in messages, item names and loading text.

### Change

In `src/ps2/veronica/prog/ps2_NaDraw2D.c`, `njDrawPolygon2DM()` now uses the same PC-only UV correction already present in Alpha and the decompilation source:

- For the four glyph-quad corners, move each U/V coordinate half a texel (8 units in the GS 4-bit fractional UV representation) inward from the corresponding cell edge.
- Near edges use `+8`; far edges use `-8`.
- Keep the original non-PC/PS2 coordinate path unchanged under `#else`.
- Keep the existing color conversion, geometry, primitive setup, and `Ps2AddPrim2D()` submission unchanged.

This is a direct port of the reference implementation, not a texture-filtering or renderer-wide UV adjustment.

### Files Changed

- `src/ps2/veronica/prog/ps2_NaDraw2D.c`
  - Updated only the four-corner UV selection inside `njDrawPolygon2DM()`.
  - The correction is guarded by `TARGET_PC`; the original PS2 UV values remain intact.
- `DEVELOPMENT_LOG.md`
  - Recorded the source lineage, exact scope, and validation status.

### Explicitly Excluded

- No changes to `njDrawPolygon2D()`, `njDrawLine2D()`, texture filtering, GS texture memory, font assets, scaling, or OpenGL sampling.
- No changes to 3D/model UVs or general-purpose texture coordinates.
- No changes to the decals/depth implementation or other GS-Rebuild work.

### Validation

- **Reference comparison:** Confirmed the four-corner `TARGET_PC` branch matches Alpha and Aikatore's decompilation implementation.
- **Scope review:** Confirmed only the font-specific `njDrawPolygon2DM()` UV corner values change; PS2/non-PC values remain unchanged.
- **Build:** Pending GitHub Actions.
- **Runtime/visual:** Pending. Inspect messages, item names, and loading text at the same elevated internal resolution where glyph seams were visible.

## 2026-11-02 — Step 10: Add Configurable Font UV Edge Tuning

**Category:** Graphics correctness / font UV diagnostics  
**Status:** Implemented in source; Windows build and visual/runtime validation pending  
**Branch:** `FONTFix` (created from `experimental/gs-rebuild`)  
**Reference:** Configurable PC-only per-edge correction in `njDrawPolygon2DM()`

### Behavior

The font UV correction is now controlled by `fontUVfix` in `recvx.ini`:

- Missing or `fontUVfix=0`: the configurable PC correction is disabled. `njDrawPolygon2DM()` uses the original PS2 corner offsets (`+8/+4`) instead.
- `fontUVfix=1`: the PC-only per-edge correction is enabled. Its code defaults are left `4`, right `-4`, top `4`, bottom `-4`.
- The four settings are independently stored as `fontUVleft`, `fontUVright`, `fontUVtop`, and `fontUVbottom`.
- Each value is clamped to `-32…32`, in GS UV units of `1/16` texel.

Corner mapping is explicit: left affects top-left and bottom-left U; right affects top-right and bottom-right U; top affects the two upper V coordinates; bottom affects the two lower V coordinates.

### Live Tuning Controls

The F10 tuner is active only while `fontUVfix=1`:

- F10 enters/leaves font UV tuning.
- Tab selects left, right, top, then bottom.
- Up/Down changes the selected value by one unit; Shift+Up/Down changes it by four.
- Backspace restores the selected edge to its correction default; Shift+Backspace restores all four defaults.
- Escape leaves tuning mode.
- Adjusted values are saved to `recvx.ini` immediately and reported in the window title/log.

The font tuner has its own state and values. It does not call or modify the menu UV crop API from `alpha`; the F10 binding can be reconsidered when menu UV tuning is added to this branch.

### Files Changed

- `pc/include/pc_platform.h`
  - Declares the font UV configuration and live-adjustment API.
- `pc/src/pc_platform.c`
  - Loads the master switch and four edge values.
  - Applies the `-32…32` bounds and persists live edits.
  - Adds the F10/Tab/Up/Down/Backspace/Escape tuner and title-bar status.
- `src/ps2/veronica/prog/ps2_NaDraw2D.c`
  - Applies the four independent offsets only in the PC branch of `njDrawPolygon2DM()`, and only when `fontUVfix=1`.
  - Retains the original PS2 UV values as the disabled/default path.
- `DEVELOPMENT_LOG.md`
  - Records configuration, key behavior, scope and validation status.

### Explicitly Excluded

- No menu/UI UV crop implementation or `pcUiUvCrop*` controls were imported.
- No rotated UI UV profile, automatic UI crop, wireframe toggle, or unrelated renderer changes.
- No texture filtering, font assets, font loading, or GS memory changes.
- No changes to non-PC UV behavior.

### Validation

- **Source review:** Confirmed the four side-to-corner mappings, the `fontUVfix=0` fallback to original PS2 offsets, the `4/-4/4/-4` code defaults used when enabled, and the `-32…32` bounds.
- **Diff review:** Confirmed the live tuner is independent of the menu UV crop controls and only intercepts its keys while `fontUVfix=1`.
- **Build:** Not yet run.
- **Runtime/visual:** Pending. Compare `fontUVfix=0` and `fontUVfix=1`, verify each edge changes only its two intended corners, and check that the menu/UI UV behavior is unchanged.

## 2026-11-02 — Step 11: Add Runtime Font Filtering Toggle

**Category:** Graphics correctness / font texture sampling  
**Status:** Implemented in source; Windows build and runtime validation pending  
**Branch:** `FONTFix`

### Behavior

- `fontFilter=1` (default) respects the GS `TEX1` filtering state. The current font texture-loading path sets linear magnification filtering.
- `fontFilter=0` forces nearest sampling for font glyph primitives.
- Press `R` at runtime to toggle between the two modes. The setting is saved immediately to `recvx.ini` and restored on the next launch.
- The title bar reports the selected mode after each toggle. The R key is reserved by this diagnostic and is not forwarded to the game; it is not one of the default game bindings.

### Implementation and Scope

- In `src/ps2/veronica/prog/ps2_NaDraw2D.c`, mark textured submissions from `njDrawPolygon2DM()` around the `Ps2AddPrim2D()` call.
- Font quads use the deferred Ordering Table path (`attr=0x80000060`). The direct-draw path is synchronous, but the OT path is emitted later, so a stack-scoped marker alone would not survive until rendering.
- For OT entries, `Ps2AddOT()` stores the nearest-filter request in the existing `PS2_OT.pad` field. The first DMA tag in an OT entry is an OT link / optional texture-state REF; the second tag, at `p->p + 16`, carries the primitive's GIF draw payload. After texture-reference setup is complete, `Ps2DrawOTagSub()` sets bit 16 on that second tag only. This bit is reserved by the DMA tag format and is consumed only as PC-side metadata; QWC, ID and address fields are unchanged.
- `pc/src/gs/gs.c` scopes that metadata around the corresponding GIF segment and captures it in `GsDrawState`. The direct path uses the temporary font-draw marker instead.
- The marker is part of the GS batch state, so font and non-font primitives are separated when necessary rather than applying the override to a mixed batch.
- In `pc/src/gs/gs_gl.c`, derive the effective linear-filter flag from the original `TEX1` state, except for marked font draws while `fontFilter=0`. The existing shader then uses its nearest/texel-fetch path instead of its manual four-texel bilinear interpolation. Render-target sampling also receives the effective filter.
- The runtime filter toggle is independent of `fontUVfix`; it works whether the UV correction is enabled or disabled.

### Files Changed

- `pc/include/pc_platform.h`
  - Declares the runtime filter state and font-draw scope functions.
- `pc/src/pc_platform.c`
  - Loads `fontFilter` with a default of `1`, toggles and saves it on R, and reports the new state in the title bar.
  - Tracks the temporary font-draw scope for direct PC submissions and OT queue insertion.
- `src/ps2/veronica/prog/ps2_NaDraw2D.c`
  - Marks only textured `njDrawPolygon2DM()` submissions.
- `src/ps2/veronica/prog/ps2_dummy.c`
  - Preserves the marker in deferred OT entries and attaches it to each corresponding PC DMA segment.
- `pc/src/gs/gs_internal.h`
  - Adds the font-only nearest-sampling marker to `GsDrawState`.
- `pc/src/gs/gs.c`
  - Captures the marker in the draw state so the normal GS batch comparison keeps modes separate.
- `pc/src/gs/gs_gl.c`
  - Forces the effective `TEX1` linear flag off only for marked font draws when nearest mode is selected.
- `.github/workflows/build-windows.yml`
  - Adds `FONTFix` to the Windows build workflow's push branches so this debug branch can be compiled by CI.
- `DEVELOPMENT_LOG.md`
  - Records the configuration, runtime control, scope and pending validation.

### Explicitly Excluded

- No global change to GS `TEX1` state or texture filtering.
- No changes to 3D/model texture sampling, ordinary 2D UI/menu primitives, movie sampling, texture decoding, or filtering defaults for non-font draws.
- No changes to font UV coordinates or the `fontUVfix` values and tuner.
- No changes to the menu UV crop system.

### Validation

- **Source review:** Confirmed the actual font call uses `njDrawPolygon2DM(..., 0x80000060)`, which is deferred through the Ordering Table. The PC-only OT sideband is written to the second DMA tag (`p->p + 16)) carrying the primitive's GIF draw payload—not the first link / texture-state REF tag—so the override remains active for the correct segment. Direct submissions retain the scoped marker path.
- **Batch review:** Confirmed the marker participates in `GsDrawState` comparison, preventing font-nearest draws from merging with ordinary draws using the same GS registers.
- **Filter review:** Confirmed marked font draws select nearest sampling while all unmarked draws continue to use the original `TEX1` state.
- **Windows CI:** Run [41529410526](https://github.com/zedhqx4/recvx-pc/actions/runs/41529410526) passed for commit `428bc231`: `Build game`, `Verify executables`, and `Upload Windows build` all succeeded. Artifact: `recvx-pc_FONTFix_428bc23`.
- **Follow-up correction:** The OT sideband was moved from the entry's first/link tag to its second DMA tag (`p->p + 16`), the one that actually carries the primitive GIF payload. Run [41529912728](https://github.com/zedhqx4/recvx-pc/actions/runs/41529912728) is validating this latest placement on commit `861f58b3`; final result pending.
- **Runtime/visual:** Pending. Compare the default and R-toggled modes on the same font strings, then verify that menu text outside `njDrawPolygon2DM()`, UI artwork, 3D textures and movies retain their previous filtering.

## 2026-11-02 — Step 12: Set Conservative Font UV and Filter Code Defaults

**Category:** Graphics correctness / user-facing defaults  
**Status:** Code and documentation updated; Windows build and runtime validation pending  
**Branch:** `FONTFix`

### Code Defaults

These are fallback values in the PC code when the corresponding keys are absent from `recvx.ini`:

```ini
fontFilter=1
fontUVfix=0
fontUVleft=4
fontUVright=-4
fontUVtop=4
fontUVbottom=-4
```

- `fontFilter=1` preserves the original bilinear magnification-filter behavior. R still toggles bilinear/nearest at runtime and writes the selected value to the INI.
- `fontUVfix=0` keeps the configurable font UV correction disabled by default.
- When a user enables `fontUVfix=1`, the four edge defaults above are used. They are independently configurable through `recvx.ini` and through the existing F10 runtime tuner; tuner edits are saved immediately.
- F10 remains gated by `fontUVfix=1`; users can opt in through the INI and restart before tuning. The tuner does not silently enable the UV correction.
- Explicit INI values always override the code defaults. Existing user settings are therefore preserved.
- This changes only PC defaults. Non-PC PS2 UV behavior, the menu UV crop system, and non-font texture filtering remain unchanged.

### Files Changed

- `pc/src/pc_platform.c`
  - Sets the fallback array to `{ 4, -4, 4, -4 }`.
  - Defaults the `fontUVfix` master switch to `0`; keeps `fontFilter` default at `1`.
- `src/ps2/veronica/prog/ps2_NaDraw2D.c`
  - Aligns the local fallback edge values with `{ 4, -4, 4, -4 }`.
- `DEVELOPMENT_LOG.md`
  - Records the exact defaults and opt-in behavior.

### Validation

- **Source review:** Confirmed the INI lookup defaults and local fallback values match the six settings above.
- **Windows build:** Pending for this exact default change.
- **Runtime/visual:** Pending. Test with all six keys absent, then with custom INI values and F10/R runtime controls.
