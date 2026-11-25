#!/usr/bin/env python3
"""Convierte el OBJ editado en Blender en la tabla de correcciones ui_uv_table.txt del juego.

Solo se escriben las piezas cuyas UV difieren de las que el juego usa por defecto (las demas no se tocan).
Uso: uiuv_obj_to_table.py editado.obj recvx_uiuv.tsv ui_uv_table.txt [--eps 0.02]   (eps en texels)"""
import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import uiuv_common as uc


def parse_obj(path):
    vs, vts, objs = [], [], []
    cur = None
    with open(path, 'r', errors='replace') as f:
        for line in f:
            t = line.split()
            if not t:
                continue
            if t[0] == 'o':
                cur = {'name': t[1], 'vstart': len(vs), 'faces': []}
                objs.append(cur)
            elif t[0] == 'v':
                vs.append((float(t[1]), float(t[2])))
            elif t[0] == 'vt':
                vts.append((float(t[1]), float(t[2])))
            elif t[0] == 'f' and cur is not None:
                cur['faces'].append([tuple(int(q) if q else 0 for q in c.split('/')[:2]) for c in t[1:]])
    return vs, vts, objs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('obj')
    ap.add_argument('dump')
    ap.add_argument('table')
    ap.add_argument('--eps', type=float, default=0.02)
    a = ap.parse_args()

    by_hash = {p.hash: p for p in uc.read_dump(a.dump)}
    vs, vts, objs = parse_obj(a.obj)
    rows, edited, unknown = [], 0, 0
    for ob in objs:
        m = re.search(r'_([0-9a-f]{16})', ob['name'])
        if not m or int(m.group(1), 16) not in by_hash:
            unknown += 1
            continue
        p = by_hash[int(m.group(1), 16)]
        _, _, pair = uc.corners_and_faces(p)
        cand = {}
        for face in ob['faces']:
            for vi, ti in face:
                lv = (vi - 1 if vi > 0 else len(vs) + vi) - ob['vstart']
                u, v = vts[ti - 1 if ti > 0 else len(vts) + ti]
                if 0 <= lv < len(pair) and pair[lv] is not None:
                    cand.setdefault(pair[lv], []).append((u * p.tex_w, (1.0 - v) * p.tex_h))
        final = []
        for j in range(p.n):
            d = p.default_uv[j]
            c = cand.get(j, [d])
            # si las esquinas de un mismo vertice discrepan, vale la que se aparta del valor por defecto
            best = max(c, key=lambda q: abs(q[0] - d[0]) + abs(q[1] - d[1]))
            final.append(best)
        diff = max(max(abs(f[0] - d[0]), abs(f[1] - d[1])) for f, d in zip(final, p.default_uv))
        edited += 1
        if diff > a.eps:
            (x0, y0), (x1, y1) = p.xy(0), p.xy(p.n - 1)
            rows.append('P %016x %d %s   # g%d %dx%d topo%d fst%d (%.1f,%.1f)-(%.1f,%.1f) dif=%.3f' % (
                p.hash, p.n, ' '.join('%.4f %.4f' % f for f in final), p.gindex, p.tex_w, p.tex_h, p.topology, p.fst,
                x0, y0, x1, y1, diff))
    with open(a.table, 'w') as f:
        f.write('# ui_uv_table v1: P clave n u0 v0 u1 v1 ...  (UV finales en texels de cada vertice de origen). Generado por uiuv_obj_to_table.py\n')
        f.write('\n'.join(rows) + ('\n' if rows else ''))
    print('%d objetos leidos, %d con clave conocida (%d desconocidos), %d piezas corregidas -> %s' % (
        len(objs), edited, unknown, len(rows), a.table))


if __name__ == '__main__':
    main()
