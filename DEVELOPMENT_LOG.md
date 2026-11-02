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



## 2026-11-01 — Normal Movies: Reuse One OpenGL Texture for the GS VRAM Surface

**Category:** GS renderer / movie memory usage  
**Status:** Implemented; Windows build and runtime validation pending  
**Branch:** `experimental/movie-texture-reuse` (based on `experimental/gs-rebuild`)

### Goal

Test whether the per-frame growth of the generic OpenGL texture cache is responsible for the memory pressure and black normal-movie playback observed during long FMVs.

The experiment deliberately keeps the existing full-frame host-to-local GS transfer. It changes only how the renderer represents and samples the movie's GS VRAM region.

### Change

For normal (non-`MOVIE_HD`) movies:

- Keep the existing MPEG decoder and full-frame IMAGE transfer into GS VRAM at TBP 14208 / TBW 5 / PSMCT32.
- Flush queued renderer primitives before overwriting the movie surface, so earlier draws finish using the previous contents.
- After the GS transfer completes, decode the active rectangle back out of emulated GS VRAM using `gsDecodeTexture()`.
- Upload that rectangle with `glTexSubImage2D()` into one persistent 512x512 OpenGL texture (1 MiB).
- While the normal movie is active, sample this persistent texture directly for TBP 14208 instead of calling the content-addressed `get_texture()` cache.
- Keep the last valid texture contents if an invalid frame size is supplied; disable only the special sampling path when the movie ends.
- Log the persistent texture's active/idle state and the number of frame updates per GS statistics interval. Its memory is intentionally not counted as a generic cached texture.

The special movie surface is selected before render-target and generic texture-cache sampling while active. The existing `MOVIE_HD` texture path is left unchanged.

### Files Changed

- `pc/src/pc_movie.c`
  - Flush pending draws before overwriting the normal movie's GS surface.
  - Refresh the persistent renderer texture after the full-frame GS transfer.
  - Disable the special movie VRAM sampling path at normal movie termination.
- `pc/src/gs/gs_gl.c`
  - Allocate one persistent 512x512 GL texture for normal movie VRAM.
  - Read the current PSMCT32 movie rectangle back from GS VRAM and update the same GL object.
  - Bypass the generic content-addressed texture cache for the active movie TBP.
  - Add periodic logging of movie texture updates and include its decode work in GS texture-decode statistics.
- `.github/workflows/build-windows.yml`
  - Include this experiment branch in the Windows build workflow's push trigger.

### Explicitly Excluded

- No 16-pixel strip transfers yet.
- No changes to `pl_mpeg`, MPEG timing, or the audio path.
- No changes to the `MOVIE_HD` path.
- No changes to generic texture-cache policy for non-movie textures.
- No changes to GS VRAM allocation or the 4 MiB GS memory model.

### Validation

- **Source review:** Confirmed that the persistent texture is refreshed from emulated GS VRAM, and active normal-movie draws bypass `get_texture()`.
- **Memory accounting:** The persistent 512x512 texture is 1 MiB and is allocated once; it is separate from `tex_cached_bytes`.
- **Build:** Windows build workflow trigger added for this experiment branch; result pending.
- **Runtime:** Pending. Compare long normal-movie playback with the previous log: verify stable generic texture-cache counts/bytes, continuous frame updates, no black video, and no regression after `termAll()`.
- **Causality:** The experiment is intended to test whether the previous per-frame texture-cache accumulation caused the memory pressure and black-video failure. That conclusion remains pending runtime results.

### Implementation Commits

- `e5c1a9f` — Reuse one GL texture for normal movie VRAM.
- `f18dd6d` — Refresh persistent movie texture from GS VRAM.
- `10ba160` — Track movie VRAM texture decode work.
- `0323792` — Prioritize active movie VRAM surface in sampling.
- `cdf08ae` — Bind the persistent movie texture on texture unit zero.
- `c8300b4` — Run the Windows build workflow for this experiment branch.

### Next Checkpoint

Build this branch and test both a short normal movie and a long normal movie. Compare periodic `gs: ... textures cached (... MB)` and `gs: movie VRAM texture: active, ... frame updates` logs against the earlier run. Confirm that the movie stays visible and that cache memory no longer grows with each decoded movie frame before considering the 16-pixel strip-transfer stage.

## 2026-11-01 — Generic GS Texture Cache: Reuse Updated Textures and Enforce a Byte Budget

**Category:** GS renderer / OpenGL texture-cache memory management  
**Status:** Implemented; Windows build and runtime validation pending  
**Branch:** `experimental/movie-texture-reuse`  
**Parent:** `905b2ac17e27c72234d9caefc62e58a1cfc01b8a`

### Problem

The previous cache retained a distinct GL texture for each observed content generation. When the game rewrote the same GS texture address and descriptor, a new cache entry was created while the old decoded RGBA8 copy remained resident. The fixed `MAX_TEX = 2048` table only triggered LRU replacement after all metadata slots had been consumed; it did not impose a practical memory limit.

The supplied runtime log showed the generic cache reaching approximately 2048 entries and 500 MB after ordinary gameplay. The earlier movie experiment stopped normal movie frames from adding entries, but did not address this general growth.

### Change

In `pc/src/gs/gs_gl.c`:

- Cache identity continues to include the texture descriptor, TEXA-relevant state, palette-content hash, dimensions, and decoded extent.
- The content-generation hash is now treated as a version of that identity rather than part of the identity itself.
- When an existing non-HD entry is requested with a new VRAM generation, the renderer decodes the new pixels and updates that entry's existing GL texture with `glTexSubImage2D()`. It no longer allocates and retains another GL object for every generation of the same descriptor/palette.
- HD replacement entries retain the previous content-generation validation. Their GL textures are owned by `gs_replace.c`, so the generic cache does not delete them.
- Added a 64 MiB nominal RGBA8 allocation budget for the generic decoded-texture cache. On allocation, least-recently-used non-HD entries are evicted until the new texture fits. `MAX_TEX` remains a separate metadata-entry ceiling.
- Added per-interval counts for in-place refreshes and cache evictions to the GS statistics log.

The 64 MiB limit applies only to the generic decoded GS texture cache. It is not a claim about total OpenGL/driver memory and does not include render targets, depth buffers, movie textures, HD replacements, or driver-side allocation overhead.

### Explicitly Excluded

- No changes to GS VRAM size, page-generation tracking, texture decoding formats, or render-target coherency.
- No changes to the normal-movie persistent texture path or `MOVIE_HD`.
- No attempt to collapse different texture descriptors, PSMs, dimensions, extents, or palette contents into one GL object.
- No change to the Windows build workflow.

### Files Changed

- `pc/src/gs/gs_gl.c`
  - Refresh matching non-HD texture objects when their GS content generation changes.
  - Add LRU eviction by byte budget, independent of the fixed entry-count limit.
  - Report refresh and eviction activity in the periodic GS log.
- `DEVELOPMENT_LOG.md`
  - Record this cache-management step and its validation status.

### Validation

- **Source review:** Confirmed that generic texture identity remains descriptor/palette-specific, while changed page content refreshes the existing GL object.
- **Ownership review:** HD replacement textures remain owned by `gs_replace.c`; generic cache eviction calls `glDeleteTextures()` only for non-HD entries.
- **Accounting review:** The byte budget counts the full `w × h × 4` RGBA8 allocation created by `make_texture()`, not merely the uploaded `cols × rows` rectangle.
- **Build:** Pending Windows Actions result.
- **Runtime:** Pending. Verify that the cache remains at or below 64 MiB, refresh/eviction counts are plausible, frequently reused textures do not corrupt, and scenes/FMVs remain visually correct with acceptable decode cost and frame pacing.

### Next Checkpoint

Run the Windows build, then repeat the long gameplay path that previously grew the generic cache toward 500 MB. Compare cache bytes, refreshes, evictions, texture correctness, and frame pacing. A stable byte count alone is not sufficient: inspect whether the budget causes excessive cache churn or decode spikes.

## 2026-11-01 — Replace the Generic Texture Cache with a Three-Slot Upload Pool

**Category:** GS renderer / bounded transient texture storage  
**Status:** Implemented; Windows build and runtime validation pending  
**Branch:** `experimental/movie-texture-reuse`  
**Parent:** `6264fefe871d89d264052f9f85cbc368d000aa60`

### Design Correction

The preceding byte-budget cache was still the wrong lifetime model. A 64 MiB LRU is smaller than 500 MB, but it continues to preserve historical texture versions and asset identities. The goal is not to retain a smaller history; it is to retain no history of decoded GS texture snapshots.

### Change

In `pc/src/gs/gs_gl.c`:

- Removed the generic content-addressed `CachedTex` table, its LRU policy, page-generation hash lookup, and the 64 MiB budget.
- Added exactly three reusable OpenGL texture objects in a round-robin upload pool.
- Each ordinary GS texture request decodes the requested current VRAM contents into the shared CPU decode buffer and uploads them into the next pool slot.
- Pool slots retain only their allocated dimensions and GL object. A slot can grow its storage when a larger texture arrives, but it does not allocate another object for a new frame, generation, address, or asset.
- Pool slots never shrink during a run. Since GS TEX0 dimensions are limited to 1024x1024, the nominal pool allocation has a hard upper bound of 3 × 1024 × 1024 × 4 = 12 MiB.
- The slot advances per textured draw/upload request, not once per presented game frame. A frame may contain many draws and therefore many texture requests.
- HD replacement textures remain on their existing file-backed path and continue to be owned by `gs_replace.c`; they are not transient GS VRAM snapshots and are not overwritten by this pool.
- The periodic log now reports the number of pool uploads and the pool's nominal allocated bytes instead of cache entries, refreshes, and evictions.

### Files Changed

- `pc/src/gs/gs_gl.c`
  - Replace historical per-texture cache entries with a fixed three-object upload pool.
  - Remove generic cache LRU and content-generation hashing from the renderer's texture lookup path.
  - Report pooled allocation size and upload count.
- `DEVELOPMENT_LOG.md`
  - Record that the preceding 64 MiB cache implementation is superseded by this pool design.

### Memory Model

The generic GS texture upload path owns at most three GL texture objects. Each object grows only to the largest dimensions it has needed, up to 1024x1024 RGBA8. The maximum nominal storage for the pool is therefore 12 MiB, not 64 MiB and not a history of per-frame snapshots.

This accounting does not include render targets, depth buffers, HD replacements, movie textures, or driver-side overhead.

### Validation

- **Source review:** Confirmed that the generic `CachedTex` table, `MAX_TEX`, LRU functions, content-generation cache lookup, and 64 MiB budget are removed from `gs_gl.c`.
- **Pool bound:** Three slots maximum; each slot is limited to 1024x1024 RGBA8 (4 MiB), for at most 12 MiB nominal allocation.
- **OpenGL ordering:** Each upload and the draw that samples it are issued in sequence on the renderer's GL context. Slot reuse is ordered after earlier draws that referenced that slot; the driver must preserve GL command semantics when processing the update.
- **Build:** Pending Windows Actions result.
- **Runtime:** Pending. Test for texture corruption, upload/decode cost, frame pacing, and bounded pool allocation through the previous long gameplay path and FMVs.

### Important Performance Tradeoff

Unlike the former cache, this pool does not skip decoding when an earlier texture happens to have the same identity or content. It deliberately favors bounded transient storage over retaining an unbounded set of decoded copies. Runtime profiling must determine whether repeated decoding is acceptable or whether a separate source-side optimization is needed that does not retain decoded GL snapshots.
