# recvx UI UV capture and Blender 2.79 roundtrip (experimental)

This branch adds a source-geometry capture path independent of the interactive UV selector.

## Capture

1. Build and run this branch.
2. Set debug=1 in recvx.ini.
3. Open the game screen to inspect.
4. Press F10 to open the UV diagnostic controls.
5. Press F9 to start capture. Move through the target screen states while capture is active.
6. Press F9 again to close the file. Leaving the tuner with F10 or ESC also closes it.

The output is menu_uv_capture.jsonl beside recvx.exe. It is append-only. Each line is one unique textured 2D source draw. The capture:
- runs before UV snapping/cropping and before widescreen vertex changes;
- includes both GS FST 12.4 UV streams and ST float UV streams;
- does not inherit the selector's 64-vertex limit;
- stores source positions, raw UV words, decoded texel UVs, color words, texture identity, primitive flags and topology;
- deduplicates exact source streams across frames, while a changed position/UV/color/priority produces a separate record.

The capture still depends on recvx recognizing the current texture as a valid TIM2 source (Ps2_now_tex). It captures textured 2D draws, not untextured overlays or 3D geometry.

## Blender 2.79

Open tools/menu_uv/blender279_roundtrip.py in Blender's Text Editor.

For import, set MODE = "IMPORT", INPUT_JSONL to the captured JSONL file, and TEXTURE_DIR to the folder containing recvx_tex_*_g<Gindex>_f<format>.* dumps.

Run the script. It creates one object per editable surface primitive and Blender Groups by texture. UVs are normalized for Blender; positions are scaled from PS2 screen coordinates for convenient viewing. Textures are matched by Gindex and PictFormat.

Edit UVs only. Do not change mesh topology or object identity. Then set MODE = "EXPORT" and run the script again. It writes menu_uv_patch.json next to the capture.

### Deliberate limits

- Triangle lists, triangle strips, triangle fans, and 2-vertex GS sprites are imported as surfaces.
- Points and line primitives remain present in the raw JSONL but are skipped by the UV mesh importer.
- A GS sprite stores only two diagonal UV endpoints. The exporter rejects non-axis-aligned/sheared UV quads rather than silently losing data.
- If a source vertex gets different UVs on different face corners, the exporter rejects that object. The original packed draw stream has one UV per source vertex, so such a seam cannot be represented safely.
- The patch carries source IDs, signatures, texture identity and original-stream vertex counts. This commit generates the edit artifact; it does not yet apply menu_uv_patch.json inside recvx. Do not expect the game to load the patch automatically.

## What this test is intended to prove

It tests whether recvx can expose the source geometry and UV stream without reverse-engineering the packed game archives, and whether Blender 2.79 can edit those UVs without touching the original positions/topology. It also reveals which screens use textured 2D TIM2 draws and which do not.

If another screen is absent from the JSONL, inspect its draw path / texture source next; this capture intentionally does not pretend to see render-target composites or non-TIM2 resources.
