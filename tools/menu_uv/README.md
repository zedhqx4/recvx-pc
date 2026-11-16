# recvx UI UV capture and Blender 2.79 roundtrip (experimental)

This branch captures the original 2D source vertex stream, lets Blender edit its UVs, then applies a strict, runtime-only UV patch. It does not rewrite the game's packed TIM2/archive files.

## Capture

1. Build and run this branch.
2. Set `debug=1` in `recvx.ini`.
3. Open the game screen to inspect.
4. Press F10 to open the UV diagnostic controls.
5. Press F9 to start capture. Visit the target screen and each state that needs its own UV edit.
6. Press F9 again to close the file. Leaving the tuner with F10 or ESC also closes it.

The output is `menu_uv_capture.jsonl` beside `recvx.exe`. It is append-only. Each line is one unique textured 2D source draw. The capture:
- runs before tuner adjustments, crop, widescreen changes and imported patches;
- includes both GS FST 12.4 UV streams and ST float UV streams;
- does not inherit the selector's 64-vertex limit;
- stores source positions, raw UV words, decoded texel UVs, color words, texture identity, primitive flags and topology;
- fingerprints the complete source stream so a patch is applied only to the exact primitive that was captured.

The capture depends on recvx recognizing the current texture as a valid TIM2 source. It captures textured 2D draws, not untextured overlays or 3D geometry. Two byte-identical source draws are indistinguishable and are deduplicated.

## Blender 2.79

Open `tools/menu_uv/blender279_roundtrip.py` in Blender's Text Editor.

For import, set `MODE = "IMPORT"`, `INPUT_JSONL` to the capture file, and `TEXTURE_DIR` to the folder containing `recvx_tex_*_g<Gindex>_f<format>.*` dumps.

Run the script. It creates one object per editable surface primitive and Blender Groups by texture. UVs are normalized for Blender; positions are scaled from PS2 screen coordinates for convenient viewing. Texture dumps are matched by Gindex and PictFormat, with an exact dimensions check.

Edit UVs only. Do not change mesh topology, vertex positions, object transforms or object identity. Then set `MODE = "EXPORT"` and run the script again. It writes `menu_uv_patch.jsonl` beside the capture.

The exporter rejects FST UV coordinates outside the GS 14-bit range, non-finite ST coordinates, UV seams that cannot be represented by the original per-vertex stream, and sheared sprite UV quads.

## Apply the patch in recvx

Copy `menu_uv_patch.jsonl` next to `recvx.exe` (the folder containing `recvx.ini`) and start the game. recvx checks the file on the first eligible textured 2D draw and applies matching entries automatically.

A patch is accepted only when the runtime draw has the same:
- source-stream signature (all original vertex words and primitive flags);
- Gindex, bank, PictFormat and texture dimensions;
- topology, UV encoding and vertex count.

The patch changes only the U/V words in a private copy of the source vertex stream. It does not mutate the game's original data or TIM2 archive. Existing live UV tuner adjustments run after the imported patch, so they can still be used for small final corrections. Remove or rename `menu_uv_patch.jsonl` to disable it.

A nonmatching entry is ignored rather than applied to a nearby or similarly named draw. The runtime log reports how many entries loaded and the IDs of primitives actually patched.

## Deliberate limits

- Triangle lists, triangle strips, triangle fans, and 2-vertex GS sprites are imported as editable surfaces.
- Points and line primitives remain available in the raw capture but are skipped by the UV mesh importer.
- A GS sprite stores only two diagonal UV endpoints. The exporter rejects non-axis-aligned/sheared UV quads rather than silently losing data.
- If one source vertex gets different UVs on different face corners, the exporter rejects that object. The original packed draw stream has one UV per source vertex, so such a seam cannot be represented safely.
- Exact source fingerprints intentionally favor safety over fuzzy matching. Draws whose positions, colors, primitive flags or other source words change at runtime may need separate captures for their different states.

This workflow tests whether recvx can expose the source geometry/UV stream without reverse-engineering the packed game archives, and whether Blender 2.79 can edit those UVs while preserving original positions and topology. If a screen is absent from the JSONL, inspect its draw path or texture source; it may use a render-target composite or a non-TIM2 resource rather than a captured 2D TIM2 draw.
