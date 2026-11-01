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
**Status:** Implemented; runtime validation pending  
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
- **Build:** Not yet verified.
- **Runtime startup:** Not yet verified.
- **Game/scene regression:** Not yet verified.

### Next Checkpoint

Build this branch and launch the executable. Confirm that scratchpad reservation succeeds before
OpenGL initialization and that `pcGsInit(spr)` completes. If reservation fails, capture the
startup log and inspect the address-space diagnostic before changing the allocation strategy.
