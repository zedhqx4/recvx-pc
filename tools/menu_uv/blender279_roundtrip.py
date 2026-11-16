# Blender 2.79 script: import recvx menu_uv_capture.jsonl and export UV-only patches.
# Edit INPUT_JSONL / TEXTURE_DIR / MODE below, then Run Script (Alt-P).
# MODE="IMPORT" builds editable objects; MODE="EXPORT" writes menu_uv_patch.json.
import bpy
import json
import os
import glob
import math

MODE = "IMPORT"
INPUT_JSONL = os.path.join(os.path.dirname(bpy.data.filepath) or os.getcwd(), "menu_uv_capture.jsonl")
TEXTURE_DIR = os.path.dirname(INPUT_JSONL)
PATCH_JSON = os.path.join(os.path.dirname(INPUT_JSONL), "menu_uv_patch.json")
GROUP_PREFIX = "RECVX_UI_"

def read_records(path):
    records = {}
    with open(path, "r") as stream:
        for line_no, line in enumerate(stream, 1):
            line = line.strip()
            if not line:
                continue
            try:
                item = json.loads(line)
            except Exception as exc:
                print("RECVX: bad JSON line %d: %s" % (line_no, exc))
                continue
            if item.get("schema") != 1 or not item.get("id"):
                continue
            # Keep the latest occurrence if the same capture was appended twice.
            records[item["id"]] = item
    return records.values()

def find_texture(record):
    gindex = record["gindex"]
    pict_format = record["pict_format"]
    patterns = [
        os.path.join(TEXTURE_DIR, "*g%d*f%d*" % (gindex, pict_format)),
        os.path.join(TEXTURE_DIR, "*g%d_f%d*" % (gindex, pict_format))
    ]
    for pattern in patterns:
        matches = sorted(glob.glob(pattern))
        for path in matches:
            if os.path.splitext(path)[1].lower() in (".png", ".bmp", ".tga", ".jpg", ".jpeg", ".tif", ".tiff"):
                return path
    return None

def build_faces(topology, count):
    faces = []
    if topology == 3 or topology == 7:
        for i in range(0, count - 2, 3):
            faces.append((i, i + 1, i + 2))
    elif topology == 4:
        for i in range(count - 2):
            if i & 1:
                faces.append((i + 1, i, i + 2))
            else:
                faces.append((i, i + 1, i + 2))
    elif topology == 5:
        for i in range(1, count - 1):
            faces.append((0, i, i + 1))
    return faces

def make_material(name, image):
    mat = bpy.data.materials.get(name)
    if mat is None:
        mat = bpy.data.materials.new(name)
    mat.diffuse_color = (1.0, 1.0, 1.0)
    mat.use_shadeless = True
    if image is not None:
        tex = bpy.data.textures.get(name + "_IMAGE")
        if tex is None:
            tex = bpy.data.textures.new(name + "_IMAGE", type="IMAGE")
        tex.image = image
        slot = None
        for old in mat.texture_slots:
            if old and old.texture == tex:
                slot = old
                break
        if slot is None:
            slot = mat.texture_slots.add()
            slot.texture = tex
        slot.texture_coords = "UV"
        slot.use_map_color_diffuse = True
    return mat

def import_record(record):
    topology = int(record["topology"])
    raw_vertices = record["vertices"]
    source_count = len(raw_vertices)
    sprite = topology == 6 and source_count >= 2

    if sprite:
        a = raw_vertices[0]
        b = raw_vertices[1]
        raw_vertices = [
            dict(x=a["x"], y=a["y"], z=a["z"], u=a["u"], v=a["v"]),
            dict(x=b["x"], y=a["y"], z=a["z"], u=b["u"], v=a["v"]),
            dict(x=b["x"], y=b["y"], z=b["z"], u=b["u"], v=b["v"]),
            dict(x=a["x"], y=b["y"], z=b["z"], u=a["u"], v=b["v"])
        ]
        faces = [(0, 1, 2, 3)]
    else:
        faces = build_faces(topology, source_count)

    if not faces:
        print("RECVX: skipped non-surface primitive %s (topology %d)" % (record["id"], topology))
        return None

    width = float(record["texture_width"])
    height = float(record["texture_height"])
    verts = [(v["x"] / 16.0, -v["y"] / 16.0, -v["z"] / 65536.0) for v in raw_vertices]
    mesh = bpy.data.meshes.new("M_" + record["id"])
    mesh.from_pydata(verts, [], faces)
    mesh.update()
    obj = bpy.data.objects.new("UI_G%d_F%d_%s" % (record["gindex"], record["pict_format"], record["id"]), mesh)
    bpy.context.scene.objects.link(obj)

    uv_layer = mesh.uv_layers.new("UVMap")
    for poly in mesh.polygons:
        for loop_index in poly.loop_indices:
            vertex_index = mesh.loops[loop_index].vertex_index
            v = raw_vertices[vertex_index]
            uv_layer.data[loop_index].uv = (v["u"] / width, 1.0 - (v["v"] / height))

    image_path = find_texture(record)
    image = None
    if image_path:
        try:
            image = bpy.data.images.load(image_path, check_existing=True)
            image.pack()
        except Exception as exc:
            print("RECVX: could not load texture %s: %s" % (image_path, exc))
    mat = make_material("MAT_G%d_F%d" % (record["gindex"], record["pict_format"]), image)
    mesh.materials.append(mat)
    for poly in mesh.polygons:
        poly.material_index = 0

    obj["recvx_schema"] = 1
    obj["recvx_id"] = record["id"]
    obj["recvx_signature"] = record["signature"]
    obj["recvx_gindex"] = int(record["gindex"])
    obj["recvx_bank"] = int(record["bank"])
    obj["recvx_pict_format"] = int(record["pict_format"])
    obj["recvx_texture_width"] = int(record["texture_width"])
    obj["recvx_texture_height"] = int(record["texture_height"])
    obj["recvx_topology"] = topology
    obj["recvx_sprite"] = bool(sprite)
    obj["recvx_original_uv"] = json.dumps([[v["u"], v["v"]] for v in raw_vertices])
    obj["recvx_original_vertex_count"] = source_count
    obj["recvx_texture_path"] = image_path or ""
    group_name = GROUP_PREFIX + "G%d_F%d" % (record["gindex"], record["pict_format"])
    group = bpy.data.groups.get(group_name)
    if group is None:
        group = bpy.data.groups.new(group_name)
    group.objects.link(obj)
    return obj

def import_all():
    if not os.path.isfile(INPUT_JSONL):
        raise RuntimeError("Capture not found: " + INPUT_JSONL)
    records = list(read_records(INPUT_JSONL))
    made = 0
    for record in records:
        if import_record(record) is not None:
            made += 1
    print("RECVX: imported %d editable primitives from %d unique records" % (made, len(records)))
    print("RECVX: texture images are matched by *g<Gindex>*f<format>* in %s" % TEXTURE_DIR)

def export_patch():
    patch = {"schema": 1, "coordinate_space": "texels_12_4", "records": []}
    for obj in bpy.context.scene.objects:
        if obj.type != "MESH" or int(obj.get("recvx_schema", 0)) != 1:
            continue
        mesh = obj.data
        if not mesh.uv_layers.active:
            print("RECVX: skip %s: no active UV layer" % obj.name)
            continue
        width = float(obj["recvx_texture_width"])
        height = float(obj["recvx_texture_height"])
        original = json.loads(obj["recvx_original_uv"])
        sprite = bool(obj.get("recvx_sprite", False))
        by_source = {}
        conflict = False

        if sprite:
            if len(mesh.polygons) != 1 or len(mesh.polygons[0].loop_indices) != 4:
                print("RECVX: skip %s: sprite must remain one quad" % obj.name)
                continue
            poly = mesh.polygons[0]
            uvs = [mesh.uv_layers.active.data[i].uv[:] for i in poly.loop_indices]
            # A GS sprite stores only diagonal UV endpoints; reject a sheared/non-rectangular UV quad.
            eps = 1.0e-5
            if abs(uvs[1][0] - uvs[2][0]) > eps or abs(uvs[1][1] - uvs[0][1]) > eps or \
               abs(uvs[3][0] - uvs[0][0]) > eps or abs(uvs[3][1] - uvs[2][1]) > eps:
                print("RECVX: skip %s: sprite UVs must remain axis-aligned" % obj.name)
                continue
            values = [(uvs[0][0] * width, (1.0 - uvs[0][1]) * height),
                      (uvs[2][0] * width, (1.0 - uvs[2][1]) * height)]
        else:
            for poly in mesh.polygons:
                for loop_index in poly.loop_indices:
                    vertex_index = mesh.loops[loop_index].vertex_index
                    uv = mesh.uv_layers.active.data[loop_index].uv
                    value = (uv[0] * width, (1.0 - uv[1]) * height)
                    previous = by_source.get(vertex_index)
                    if previous is not None and (abs(previous[0] - value[0]) > 1.0e-4 or abs(previous[1] - value[1]) > 1.0e-4):
                        conflict = True
                        break
                    by_source[vertex_index] = value
                if conflict:
                    break
            if conflict:
                print("RECVX: skip %s: one source vertex has conflicting per-face UVs" % obj.name)
                continue
            values = []
            for index in range(len(original)):
                values.append(by_source.get(index, (original[index][0], original[index][1])))

        if len(values) != len(original):
            print("RECVX: skip %s: UV/source vertex count mismatch" % obj.name)
            continue
        raw_uv = []
        for u, v in values:
            raw_uv.append([int(round(u * 16.0)), int(round(v * 16.0))])
        patch["records"].append({
            "id": obj["recvx_id"],
            "signature": obj["recvx_signature"],
            "gindex": int(obj["recvx_gindex"]),
            "bank": int(obj["recvx_bank"]),
            "pict_format": int(obj["recvx_pict_format"]),
            "texture_width": int(obj["recvx_texture_width"]),
            "texture_height": int(obj["recvx_texture_height"]),
            "topology": int(obj["recvx_topology"]),
            "sprite": sprite,
            "vertex_count": len(raw_uv),
            "uv_raw": raw_uv
        })
    with open(PATCH_JSON, "w") as stream:
        json.dump(patch, stream, indent=2, sort_keys=True)
    print("RECVX: wrote %d UV-only records to %s" % (len(patch["records"]), PATCH_JSON))

if MODE == "IMPORT":
    import_all()
elif MODE == "EXPORT":
    export_patch()
else:
    raise RuntimeError("MODE must be IMPORT or EXPORT")
