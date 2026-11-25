#!/usr/bin/env python3
"""Convierte el volcado de piezas 2D (recvx_uiuv.tsv) en un OBJ editable en Blender.

Cada pieza 2D es un objeto `p<N>_<clave>` con su quad (posicion = pixeles de pantalla del juego, y hacia abajo -> -y) y las UV que el juego
usaria hoy (en el espacio normalizado del atlas, v hacia arriba como en Blender). Se editan las UV en el editor UV de Blender y se exporta
el OBJ con UV; despues uiuv_obj_to_table.py genera la tabla ui_uv_table.txt.

Uso: uiuv_dump_to_obj.py recvx_uiuv.tsv salida.obj [--atlas-dir DIR] [--gindex N ...]
  --atlas-dir  carpeta con los volcados de texturas (recvx_tex_NN_g<Gindex>_f<fmt>.jpg/png/bmp) para verlos de fondo en Blender
  --gindex     solo las piezas de esas texturas
En Blender: File > Import > Wavefront (.obj); al exportar, marcar "Objects as OBJ Objects" y "UV Coordinates", sin triangular las caras."""
import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import uiuv_common as uc


def find_atlas(atlas_dir, gindex):
    if not atlas_dir:
        return None
    rx = re.compile(r'_g%d_f\d+\.(jpg|jpeg|png|bmp)$' % gindex, re.I)
    for name in sorted(os.listdir(atlas_dir)):
        if rx.search(name):
            return os.path.join(atlas_dir, name)
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dump')
    ap.add_argument('obj')
    ap.add_argument('--atlas-dir')
    ap.add_argument('--gindex', type=int, nargs='*')
    a = ap.parse_args()

    pieces = uc.read_dump(a.dump)
    if a.gindex:
        pieces = [p for p in pieces if p.gindex in a.gindex]
    mtl_name = os.path.splitext(os.path.basename(a.obj))[0] + '.mtl'
    vbase = tbase = 0
    out = ['# recvx ui uv: una pieza 2D = un objeto p<N>_<clave>. Editar solo las UV.', 'mtllib ' + mtl_name]
    gset = []
    skipped = 0
    for idx, p in enumerate(pieces):
        corners, faces, _ = uc.corners_and_faces(p)
        if not faces:
            skipped += 1
            continue
        if p.gindex not in gset:
            gset.append(p.gindex)
        out.append('o p%d_%016x' % (idx, p.hash))
        out.append('usemtl atlas_g%d' % p.gindex)
        for x, y, u, v in corners:
            out.append('v %.4f %.4f 0.0' % (x, -y))
        for x, y, u, v in corners:
            out.append('vt %.8f %.8f' % (u / p.tex_w, 1.0 - v / p.tex_h))
        for f in faces:
            out.append('f ' + ' '.join('%d/%d' % (vbase + i + 1, tbase + i + 1) for i in f))
        vbase += len(corners)
        tbase += len(corners)
    open(a.obj, 'w').write('\n'.join(out) + '\n')

    mtl = []
    for g in gset:
        mtl += ['newmtl atlas_g%d' % g, 'Kd 1.0 1.0 1.0']
        img = find_atlas(a.atlas_dir, g)
        if img:
            mtl.append('map_Kd ' + os.path.relpath(img, os.path.dirname(os.path.abspath(a.obj))))
        else:
            mtl.append('# sin imagen de atlas para Gindex %d (poner --atlas-dir con los recvx_tex_*_g%d_*.jpg)' % (g, g))
    open(os.path.join(os.path.dirname(os.path.abspath(a.obj)), mtl_name), 'w').write('\n'.join(mtl) + '\n')
    print('%d piezas en %s (%d sin caras omitidas), %d texturas: %s' % (len(pieces) - skipped, a.obj, skipped, len(gset), gset))


if __name__ == '__main__':
    main()
