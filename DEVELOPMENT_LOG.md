# Alpha Development Log

**Repository:** `zedhqx4/recvx-pc`  
**Branch audited:** `alpha`  
**Audited HEAD:** `011f54510a022d78963e9e837265fc814d2f2e6e`  
**HEAD date:** 2026-11-01  
**Audit date:** 2026-11-04

This file describes the implementation that is present in the audited `alpha` tree. It is a development map, not a claim that every commit or experiment in the branch history remains active. The milestones below are representative rather than an exhaustive reproduction of the commit list.

The branch is 157 commits ahead of the comparison point `ebf4a017295d582b3f776d8c0dbf49ca30de5f4e` (2026-10-25). That comparison point is useful for tracing this recent development line; it is not the beginning of the entire repository or of the original PC port.

## 1. Project and platform architecture

`recvx-pc` is a native PC port of Resident Evil Code: Veronica X based on the decompiled game source. It is not a console emulator. The game logic remains in the original source tree while the PS2-specific platform services are replaced by PC implementations.

The current port targets **32-bit Windows/x86** and is built with MSVC. The 32-bit target is intentional: parts of the original asset-loading code patch file offsets into pointer fields in place. PC-specific code is generally guarded by `TARGET_PC`, leaving the original PS2 implementation in the non-PC branch wherever that structure is retained.

The main layers are:

| Area | Responsibility |
|---|---|
| `src/`, `include/`, `config/` | Decompiled game and Ninja/CNK logic, with PC-specific sections guarded by `TARGET_PC` |
| `pc/src/gs/` | PS2 Graphics Synthesizer model: GIF/register handling, primitive assembly, GS VRAM, render targets, OpenGL renderer, texture cache and HD replacement |
| `pc/src/vu/` | PC-side C implementation of VU0 operations used by the port |
| `pc/src/pc_*.c` | PC operating-system, input, audio, movie, CD/DVD, kernel and platform services |
| `pc/config/recvx_config.c` | Settings application |
| `pc/tools/` | Build, dependency, asset-conversion and source-porting tools |
| `rando/` | Biorand++ / CV:X randomizer integration |

The central graphics files have distinct jobs:

- `pc/src/gs/gs.c`: consumes GIF packets, updates GS registers, assembles kicked primitives and submits batches to the renderer.
- `pc/src/gs/gs_mem.c`: implements GS local memory addressing, pixel reads/writes, transfer state and VRAM page-generation invalidation.
- `pc/src/gs/gs_gl.c`: OpenGL 3.3 renderer, texture and palette caches, render targets, depth buffers, presentation and PC-side renderer state tracking.
- `pc/src/gs/gs_replace.c`: PCSX2-format HD texture replacement.
- `src/ps2/veronica/prog/ps2_dummy.c`: PC adaptations around PS2 texture loading and TIM2 uploads, including the native asset-cache integration.
- `src/ps2/veronica/prog/ps2_NinjaCnk.c`: CNK model/chunk processing and the PC-side vertex/strip fast paths.

## 2. Platform, startup and display

The Windows platform layer was moved to **SDL3**. The current build configuration uses SDL 3.4.18 and enables HIDAPI/libusb support for the Nintendo Wii U GameCube adapter. At startup, the platform explicitly enables the GameCube HIDAPI and libusb hints before initializing SDL.

The PS2 scratchpad address range is reserved after SDL initialization but **before** the OpenGL window/context is created. This ordering exists because loading a graphics driver can occupy the fixed address range used by the original game.

The renderer requires an OpenGL 3.3 context. The Windows workflow gained a startup smoke test that checks entry into `main()`, SDL/window initialization and creation of the OpenGL context. This documents the automated startup gate; it does not certify any particular local build or gameplay session.

Display configuration is read from `recvx.ini`:

- `scale`: integer render scale, clamped to 1–8; the current default is 3.
- `render=WxH`: optional backing render dimensions at scale 1 (width 320–640, height 240–480). Game coordinates remain 640×480.
- `window=WxH`, `window_pos`, `window_max`: window geometry/state.
- `fullscreen=0|1|desktop`: windowed, exclusive fullscreen or desktop fullscreen.
- `aspect=W:H`: display aspect; the 2D layout retains its 4:3 design.
- `vsync=0|1`: controls the OpenGL swap interval; default is enabled.
- `pause_unfocused`: controls focus-loss pause behavior.

Custom backing resolution is not the same as changing the game's logical coordinate system. The renderer maps between the 640×480 game space, the selected backing size and the scaled presentation target.

**Representative milestones:** SDL3/GameCube port ([`d9497ce98f`](https://github.com/zedhqx4/recvx-pc/commit/d9497ce98f)); startup smoke test ([`3eee5d17a2`](https://github.com/zedhqx4/recvx-pc/commit/3eee5d17a2)); scratchpad reservation ordering ([`3baca495cd`](https://github.com/zedhqx4/recvx-pc/commit/3baca495cd)); custom render resolution ([`dc2bf1ae1d`](https://github.com/zedhqx4/recvx-pc/commit/dc2bf1ae1d)); VSync setting ([`c3e506bcac`](https://github.com/zedhqx4/recvx-pc/commit/c3e506bcac)).

## 3. UI texture sampling and UV correction

A significant graphics-fix line addresses thin seams and neighboring-cell sampling in upscaled 2D UI atlases. It is deliberately more selective than a global UV offset.

The PC path supports independent crop values for the left, right, top and bottom edges. Values are in **1/16 texel units**: 8 is half a texel and 16 is one texel. Signed values are accepted, with each edge clamped to -64…64.

The current defaults in `ps2_dummy.c` are:

| Profile | Left | Right | Top | Bottom |
|---|---:|---:|---:|---:|
| Normal | -8 | 16 | -16 | 16 |
| 90° clockwise | 0 | 8 | 16 | 1 |

The `ui_uv_crop_*` keys override the normal profile. The `ui_uv_crop_rot90_cw_*` keys override the clockwise-rotated profile. The older `ui_uv_crop` setting remains a fallback for normal-profile edges that do not have individual values.

The automatic correction is guarded by geometry/UV eligibility checks. It is intended for suitable 2D fixed-UV atlas primitives, not arbitrary 3D surfaces, scaled mappings or ambiguous/mirrored geometry. Rotated and mirrored cases have separate orientation handling rather than blindly applying the normal profile.

Developer controls documented in `pc/README.md`:

- `debug=1` enables diagnostics.
- F10 enters/leaves live UV crop tuning; Tab selects an edge, Up/Down adjust it, Backspace resets it, and changes are saved to `recvx.ini`.
- F12 toggles the cyan wireframe diagnostic.
- `ui_snap_uv=0` and `ui_uv_inset=0` are A/B switches for the older snapping and renderer inset paths, not normal setup requirements.

A separate PC font-glyph sampling repair pulls samples toward texel centers in `njDrawPolygon2DM()`; it should not be confused with replacing the game's font.

**Representative milestones:** guarded per-edge crop ([`a484e8b2dd`](https://github.com/zedhqx4/recvx-pc/commit/a484e8b2dd)); rotated/mirrored handling ([`4c8cf4f563`](https://github.com/zedhqx4/recvx-pc/commit/4c8cf4f563)); separate clockwise profile defaults ([`4ab2d5553e`](https://github.com/zedhqx4/recvx-pc/commit/4ab2d5553e)); font glyph UV repair ([`2b4c4f2306`](https://github.com/zedhqx4/recvx-pc/commit/2b4c4f2306)).

## 4. GS emulation and OpenGL renderer

The PC renderer models the PS2 GS pipeline rather than treating the game as a conventional PC scene renderer. GIF writes update GS register state; vertex kicks assemble points, lines, sprites and triangles; pending primitives are grouped into batches that share a draw state; the OpenGL renderer then applies GS texture, clamp, alpha, depth, fog and blending behavior.

### 4.1 Primitive and CLUT ordering

The primitive queue is flushed by `gsFlushPrims()`. CLUT replacement retains the ordering rule that queued primitives must be drawn with the palette that was active when they were submitted. In the current tree, `clut_load()` flushes queued primitives before overwriting `clut_buf` when a CLUT load is required.

This ordering is intentional. A test that skipped the flush when the newly read CLUT compared equal was reverted after the original flush ordering was tested for particle rendering. Do not treat that identical-CLUT flush skip as a current Alpha feature.

### 4.2 OpenGL state cache

The current `gs_gl.c` includes a renderer-local `PcGsGlState` cache. Its wrappers track state such as framebuffer bindings, viewport/scissor, depth and color masks, blend state, enabled capabilities, shader program, VAO/buffer bindings, active texture unit and 2D texture bindings. When a requested value is already known to be active, the wrapper can skip the redundant OpenGL call.

This is a **renderer/OpenGL state cache**. It is separate from the GS texture-content cache and separate from the TIM2 source/image-upload cache described below. Raw OpenGL work performed by the HD replacement loader invalidates tracked bindings before the renderer resumes using its wrappers.

### 4.3 Render targets and depth

Render targets represent GS framebuffer regions. The renderer synchronizes them with GS VRAM as required, tracks dirty state and can reload changed rows instead of always reloading a complete target. A constrained partial-readback path handles aligned PSMCT32 transfers when the framebuffer pitch and address mapping can be proven to match; other layouts keep the conservative fallback.

Depth-buffer allocation follows the GS depth format rather than forcing every depth surface into the same precision. The decal/overlay work adds a bounded draw-order depth priority for coplanar transparent 3D overlays. The priority is deliberately capped; it is not a universal fix for every Z-fighting case.

**Representative milestones:** partial render-target readback ([`6aac76b9fc`](https://github.com/zedhqx4/recvx-pc/commit/6aac76b9fc)); GS depth precision ([`0f07da96c1`](https://github.com/zedhqx4/recvx-pc/commit/0f07da96c1)); bounded decal priority ([`d91cb966bc`](https://github.com/zedhqx4/recvx-pc/commit/d91cb966bc), finalized by [`4553e48995`](https://github.com/zedhqx4/recvx-pc/commit/4553e48995)).

## 5. Texture caching: three distinct layers

“Texture cache” in Alpha refers to multiple mechanisms. They solve different costs and should not be treated as interchangeable.

### 5.1 General GS texture cache

The OpenGL renderer caches decoded GS textures using texture interpretation, palette/content information, decoded extent and GS page-generation state. The cache uses hash buckets for exact-key lookup; the LRU scan is reserved for allocation/eviction paths rather than being required for every exact hit.

The cache also handles indexed PS2 formats with a palette-independent representation:

1. `gsDecodeIndices()` reads the source index plane from GS memory.
2. Indexed atlas data is stored as a compact `GL_R8` texture.
3. A separate small palette texture stores the CLUT colors.
4. The fragment shader uses the index to fetch the corresponding palette entry.
5. Different CLUTs can therefore reuse one index atlas while binding different palette lookup textures.

The indexed formats covered by the current path are PSMT8, PSMT4, PSMT8H, PSMT4HL and PSMT4HH. The palette size is 256 entries for 8-bit formats and 16 entries for 4-bit formats. Atlas and palette caches have separate capacity/LRU management. The palette cache uses the CLUT content hash together with TEXA/CPSM semantics so it can avoid expanding the CLUT on every indexed-texture hit.

This path is different from caching a fully expanded RGBA copy for every palette variant.

**Representative milestones:** palette-independent index decode ([`3646a2029f`](https://github.com/zedhqx4/recvx-pc/commit/3646a2029f)); indexed-atlas/CLUT shader path ([`95ded46cc0`](https://github.com/zedhqx4/recvx-pc/commit/95ded46cc0)); separate atlas and palette caches ([`e43984b729`](https://github.com/zedhqx4/recvx-pc/commit/e43984b729)); reuse across CLUT variants ([`49bcd00f18`](https://github.com/zedhqx4/recvx-pc/commit/49bcd00f18)); avoid expanding CLUT on every hit ([`8fbd5a226c`](https://github.com/zedhqx4/recvx-pc/commit/8fbd5a226c)); indexed shader-state reset on every draw ([`2a1df56085`](https://github.com/zedhqx4/recvx-pc/commit/2a1df56085)); hash-indexed cache lookup ([`15d8e3cf22`](https://github.com/zedhqx4/recvx-pc/commit/15d8e3cf22)).

### 5.2 Static indexed TIM2 asset/image-upload cache

A second cache connects the game's TIM2 asset identity to a native indexed texture already resident in the renderer cache. The identity is based on the TIM2 `Gindex` plus source image content hash; the renderer then verifies the GS texture layout and dimensions before reporting a reusable entry.

The current eligibility test in `Ps2TexLoad()` is intentionally narrow. The asset must be a static indexed TIM2 image with a CLUT, no texture-substitution/render-subtexture case, an eligible picture format, a supported indexed image type (4 or 5), a valid computed source hash and dimensions that fit the cache's supported limits. It is not a cache for all TIM2 images.

When the image is already resident, the code can avoid sending the image plane again. It still handles the palette/CLUT path and re-applies the GS texture state. A second, stricter bind memo checks the asset ID, image and CLUT hashes, TEX0/layout fields, dimensions, picture/image/CLUT types, clamp settings, bank, render-subtexture mode and GS texture-binding generation. That fast path still calls `Ps2BindTextureState()`; it does not simply return while leaving GS state stale.

The cache is enabled by default. Set:

```text
RECVX_PS2_TEX_CACHE=0
```

to disable the static indexed TIM2 asset cache for A/B testing. This switch does not disable the separate general GS texture cache or the OpenGL state cache.

**Representative milestones:** initial source identity path ([`fe6066aeaa`](https://github.com/zedhqx4/recvx-pc/commit/fe6066aeaa)); multiple TIM2 aliases ([`e8b31692ab`](https://github.com/zedhqx4/recvx-pc/commit/e8b31692ab)); exact layout validation ([`a7ad0f359d`](https://github.com/zedhqx4/recvx-pc/commit/a7ad0f359d)); switch to Gindex identity ([`9db4f21655`](https://github.com/zedhqx4/recvx-pc/commit/9db4f21655)); separate image upload and bind state ([`7faf17cb01`](https://github.com/zedhqx4/recvx-pc/commit/7faf17cb01)); enable by default ([`cbc6f4604d`](https://github.com/zedhqx4/recvx-pc/commit/cbc6f4604d)); restore GS texture state on cached loads ([`1192d9f50b`](https://github.com/zedhqx4/recvx-pc/commit/1192d9f50b)); merged into Alpha at [`011f54510a`](https://github.com/zedhqx4/recvx-pc/commit/011f54510a).

### 5.3 OpenGL state cache is not texture-content caching

The `PcGsGlState` wrappers skip redundant API state changes. The GS texture cache reuses decoded GPU texture data. The TIM2 asset cache can avoid retransferring an eligible source image from game memory to GS memory. A hit in one layer does not imply a hit in the others.

## 6. GS VRAM writes and IMAGE transfer optimization

`gs_mem.c` contains several CPU-side optimizations for host-to-GS transfers:

- Pixel writes first compare the existing destination value. If the pixel is unchanged, the code returns without writing it or invalidating its GS page.
- During one IMAGE packet, a page-generation epoch makes each affected 8 KiB GS page's generation increment at most once. The generation is an invalidation token for texture/render-target caches, not a per-pixel write counter.
- PSMCT32 and PSMZ32 host-to-GS transfers use `transfer32Run()`. The common horizontal run computes the GS page/block once for a group of up to eight pixels, then uses the column swizzle table for addresses.
- When the rectangle is suitably aligned and contains complete 8×8 regions, the fast path assembles each 8×8 block in GS-swizzled order, compares/copies the complete 256-byte block, and advances by a full row block. Partial, misaligned or incomplete regions fall through to the per-pixel run path.
- Other pixel formats retain their format-specific transfer logic; the 8×8 block path is specifically a 32-bit transfer optimization.

The swizzled block is not a linear 8×8 image copied as-is. Its entries are placed using the GS column table before the 256-byte comparison/copy.

**Representative milestones:** skip identical writes ([`94264b0035`](https://github.com/zedhqx4/recvx-pc/commit/94264b0035)); coalesce page-generation updates ([`56c16ee455`](https://github.com/zedhqx4/recvx-pc/commit/56c16ee455)); 8-pixel 32-bit transfer runs ([`70189849c5`](https://github.com/zedhqx4/recvx-pc/commit/70189849c5)); aligned 8×8 block experiment and empty-input guard ([`1df798acf9`](https://github.com/zedhqx4/recvx-pc/commit/1df798acf9), [`63ac50aa02`](https://github.com/zedhqx4/recvx-pc/commit/63ac50aa02)).

## 7. CNK, VU0 and model-geometry CPU work

The current Alpha tree has several PC-side geometry fast paths. They target work performed inside CNK/VU processing, but they are not all the same optimization:

- **VU0 CALCPOINT batch:** `pcVu0CalcPointN()` evaluates groups of model vertices with an SSE2/SoA path and leaves boundary/tail work on the reference scalar microprogram path. Its A/B switch is `RECVX_VU0_BATCH=0`; the batch path is enabled by default when the variable is absent.
- **VU0 strip clipping:** `pcVu0StripClip()` implements the strip-clipping operation sequence in C for the PC path, retaining the VU state/clip side effects needed by subsequent processing. It is used by both `njCnkCsUvh()` and `njCnkCsUvn()`; the original PS2 assembly remains in the non-PC branches.
- **Directional CNK vertex batch:** `pcCnkCvVnDirectionalBatch()` processes four vertices at a time with SSE2 when the active lights meet the directional-only condition and the chunk is large enough. Remaining vertices use the scalar implementation. Set `RECVX_CV_VN_BATCH=0` to disable this path for comparison.

These are CPU geometry optimizations. They do not change the on-disk CNK format or turn the CNK parser into a single batched parser call.

The chunk dispatch in `njCnkDrawModelLocal()` still uses `pCnkFuncTbl[pCnk->ucType](pCnk)` to process V-list and P-list chunks. I did **not** find a general cache that suppresses repeated CNK parser-handler invocations in the audited current source. The profiling added around this path measures calls and time by chunk type; measurement should not be mistaken for a call-reduction feature.

**Representative milestones:** VU0 SSE2 batch ([`eca6a766ef`](https://github.com/zedhqx4/recvx-pc/commit/eca6a766ef)); C strip clipping ([`11e4bbbbe3`](https://github.com/zedhqx4/recvx-pc/commit/11e4bbbbe3)); use strip clipping for Vn chunks ([`40a7a67466`](https://github.com/zedhqx4/recvx-pc/commit/40a7a67466)); directional CNK vertex batch ([`c68b8eadee`](https://github.com/zedhqx4/recvx-pc/commit/c68b8eadee)).

## 8. Profiling and diagnostics

Game profiling is opt-in. The current `pcGameProfileEnabled()` requires both:

```ini
debug=1
profile_game=1
```

The profiler aggregates in 60-frame windows and records:

- `bhMainSequence` phases such as pre-work, animation, enemy/player updates, effects/objects, camera/light, model drawing, render-texture drawing, mirror, overlays and the uninstrumented remainder.
- `bhAllDrawModel` time divided into room geometry, enemy objects, effects, object/items, enemies, player, water and setup/other.
- CNK V-list and P-list processing, model calls, P-list chunk counts, and time/call counts by raw CNK chunk type.
- Texture-load time, `Send_Tim2_dataEx` call/time totals, skipped image uploads, CLUT-only time and GS IMAGE transfer time/QWC.

The renderer also has separate texture diagnostics controlled by:

```text
RECVX_GS_TEX_DIAG=1
```

That switch enables cache/miss and renderer timing diagnostics. Keep it distinct from `profile_game`: one is renderer-focused diagnostics, the other is game/CNK/texture-upload profiling.

**Representative milestones:** game-phase profiling ([`f7fad63d3e`](https://github.com/zedhqx4/recvx-pc/commit/f7fad63d3e)); CNK V/P-list timings ([`99fa1d0672`](https://github.com/zedhqx4/recvx-pc/commit/99fa1d0672)); profile by chunk type ([`bd9a4bf262`](https://github.com/zedhqx4/recvx-pc/commit/bd9a4bf262)); texture upload profiling ([`4f2eb2cbf6`](https://github.com/zedhqx4/recvx-pc/commit/4f2eb2cbf6), [`48ec3e6b46`](https://github.com/zedhqx4/recvx-pc/commit/48ec3e6b46)).

## 9. Movie/video path

The current movie path is independent of the static indexed TIM2 asset cache.

Movies are converted from the disc data to MPEG files by the PC tools and decoded by the PC MPEG player. The game still submits the movie sprite through the GS draw path, but decoded frame pixels are uploaded to a dedicated OpenGL movie texture. The renderer recognizes the movie texture's GS base pointer and samples that texture instead of reading a per-frame movie image from GS VRAM.

Original converted movies use scale 1. Optional upscaled movies in `MOVIE_HD` use scale 4 and are selected when present and enabled by `movies_hd` in `recvx.ini`. The HD texture therefore remains separate from the ordinary GS TIM2 texture cache and from the game's GS VRAM image-transfer path.

This is the current Alpha movie implementation; it should be treated as its own solution rather than as a required part of the TIM2 cache design.

**Representative milestone:** render original movies without per-frame GS VRAM frame uploads ([`279f678602`](https://github.com/zedhqx4/recvx-pc/commit/279f678602)).

## 10. Historical experiments and current-state cautions

A commit appearing in the branch's broader history is not sufficient evidence that its implementation is present in the current tree. The following distinctions were checked against the audited source:

1. **Identical-CLUT flush skip was reverted.** The attempt to avoid a primitive flush when a newly read CLUT matched the current buffer was reverted to test/restore the original flush ordering. Current `clut_load()` flushes before overwriting a required CLUT. See [`aedb39aaa8`](https://github.com/zedhqx4/recvx-pc/commit/aedb39aaa8) and its reversal/test [`e9c259501d`](https://github.com/zedhqx4/recvx-pc/commit/e9c259501d).

2. **GS draw-state reconstruction cache is not active in the audited `gs.c`.** The history contains [`3080176a95`](https://github.com/zedhqx4/recvx-pc/commit/3080176a95), which changed `current_state()` to cache a draw state across kicks. The current source has the local `GsDrawState` reconstruction and full-state comparison in `emit()`; do not list that optimization as a current feature.

3. **The renderer OpenGL state cache is active.** This is a different mechanism in `gs_gl.c` (`PcGsGlState` and its wrappers), and it is present in the audited HEAD.

4. **CNK profiling and CNK computation fast paths are not parser-call elimination.** The profiler counts/time-stamps chunk handlers. SSE2 and C paths batch or streamline parts of vertex/strip processing. The current chunk table dispatch remains in place.

5. **The movie texture path is independent.** It uses a dedicated GL texture for decoded movie frames; it is not the indexed TIM2 atlas/CLUT cache and is not a general requirement for the other GS renderer branches.

These notes are included to keep reverted tests and related-but-different caches from being described as one current feature.

## 11. Useful runtime switches

| Setting | Effect |
|---|---|
| `debug=1` | Enables developer diagnostics and UV tuning controls |
| `profile_game=1` | Enables game/CNK/texture-upload profiler when used with `debug=1` |
| `RECVX_GS_TEX_DIAG=1` | Enables GS renderer texture/cache diagnostics |
| `RECVX_PS2_TEX_CACHE=0` | Disables the static indexed TIM2 asset/image-upload cache; enabled by default |
| `RECVX_VU0_BATCH=0` | Disables the PC VU0 CALCPOINT batch path |
| `RECVX_CV_VN_BATCH=0` | Disables the directional CNK SSE2 vertex batch path |
| `ui_uv_crop_*` | Overrides normal-profile UI UV edge crop |
| `ui_uv_crop_rot90_cw_*` | Overrides clockwise-rotated UI UV edge crop |
| `ui_snap_uv=0` | Disables the older PC UV snapping workaround for A/B testing |
| `ui_uv_inset=0` | Disables the GS renderer half-texel inset for A/B testing |
| `render=WxH` | Selects the internal backing resolution, preserving 640×480 game coordinates |
| `scale=N` | Selects presentation scale, 1–8 |
| `vsync=0|1` | Disables/enables VSync |

Other operational variables for input replay, screenshots, logging, audio dumps, data paths and room warping are documented in `pc/README.md`.

## PSMT8 / PSMT4 Host-to-GS IMAGE block uploads

**Status:** Implemented on `experimental/opengl-4.3`; equivalence workflow added.

- Added `transfer8Blocks()`: complete aligned PSMT8 blocks are assembled in GS swizzled order as 256-byte copies. One block covers 16×16 texels.
- Added `transfer4Blocks()`: complete aligned PSMT4 blocks are assembled in GS swizzled order as 256-byte copies. One block covers 32×16 texels.
- Partial, unaligned, and packet-tail data continues through the existing per-pixel `mem_write()` path.
- Enabled the two block paths by default. `GS_BLOCK_FASTPATH=0` disables only these PSMT8/PSMT4 paths for a per-pixel reference comparison; the existing PSMCT32/PSMZ32 implementation is unchanged.
- PSMT8H, PSMT4HL/HH and all other PSM formats are unchanged. No Local-to-Local or GS-to-Host transfer path was modified.
- Added `pc/tests/gs_mem_blockpath_test.c` and `.github/workflows/test-gs-block-transfer.yml`. The test compares full GS VRAM, page-generation counters, and `gsXferDone()` notifications for 4,000 randomized transfers between block and reference builds.

**Validation:** The comparison is automated on pushes to this branch. A passing Actions result is required before describing the block path as verified. Windows build and in-game performance/visual regression remain separate checks.

## 12. Source map for future work

| Topic | Primary source files / functions |
|---|---|
| GS register and primitive path | `pc/src/gs/gs.c`: `gsWriteReg()`, `gsFlushPrims()`, `emit()`, `clut_load()` |
| GS memory and transfers | `pc/src/gs/gs_mem.c`: `mem_write()`, `touch()`, `transfer32Run()`, `transfer8Blocks()`, `transfer4Blocks()`, `gsMemTransferData()` |
| GPU texture/palette caches | `pc/src/gs/gs_gl.c`: `get_texture()`, `get_indexed_atlas_slot()`, `get_palette_texture()` |
| OpenGL state cache | `pc/src/gs/gs_gl.c`: `PcGsGlState`, `pcGsBindFramebuffer()`, `pcGsActiveTexture()`, `pcGsBindTexture()` |
| TIM2 asset reuse | `src/ps2/veronica/prog/ps2_dummy.c`: `Ps2TexLoad()`, `Ps2EnsureTextureImage()`, `Ps2BindTextureState()` |
| CNK and VU0 geometry | `src/ps2/veronica/prog/ps2_NinjaCnk.c`, `pc/src/vu/vu0_calcpoint.c` |
| Game/CNK profiler | `src/ps2/veronica/prog/game.c`, `pc/include/pc_game_profile.h` |
| Movie frames | `pc/src/pc_movie.c`, `pc/src/gs/gs_gl.c`: `pcGsMovieFrame()` |
| SDL3, input, display and VSync | `pc/src/pc_platform.c`, `pc/tools/build.py`, `.github/workflows/build-windows.yml` |

---

**Maintenance rule:** update this file when a change lands in `alpha`, and describe the behavior verified in the resulting tree. Mark experiments as experiments, and explicitly mark them reverted when that happens. Keep GPU texture-content caching, TIM2 image-upload avoidance, OpenGL state caching, GS VRAM transfer optimizations, CNK profiling and movie playback as separate topics unless the implementation actually couples them.


## OpenGL 4.3 branch: SDL3 gamepad mappings and decal targeting

**Status:** Implemented on `experimental/opengl-4.3`; runtime validation pending.

- Added optional `gamecontrollerdb.txt` loading in `pc/src/pc_platform.c`. The file is searched beside `recvx.exe` and loaded with `SDL_AddGamepadMappingsFromFile()` in `pcPadInit()`, after bindings load but before `SDL_GetJoysticks()` enumerates devices. If the file is absent or fails to load, SDL's built-in mappings remain available.
- Replaced the broad blended-textured-3D decal predicate in `pc/src/gs/gs_gl.c` with the exact GS material-signature matcher from `reconstruction/4-Decalfixing`. It checks the shared GS state fields and the captured TEX0/ALPHA signature list; unrelated blended 3D primitives no longer receive decal priority.
- Restored the earlier branch's smaller polygon offset: draw-order priorities 1–4 use `glPolygonOffset(0.0f, priority * 0.50f)` rather than 1–4 full units. The existing OpenGL state wrappers and per-frame draw-order reset are retained.
- This is an A/B experiment against the current broad predicate and larger offset. Confirm controller mapping logs and compare decal appearance in-game before treating the visual correction as validated.

**Commits:** SDL3 mapping load ([`9fa3e0caab`](https://github.com/zedhqx4/recvx-pc/commit/9fa3e0caab)); exact decal signatures and reduced offset ([`fcca847426`](https://github.com/zedhqx4/recvx-pc/commit/fcca847426)).
