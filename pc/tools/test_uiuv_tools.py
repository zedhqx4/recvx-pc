#!/usr/bin/env python3
"""Prueba de extremo a extremo de la ruta volcado -> OBJ -> (edicion) -> tabla -> juego, sin Blender ni el juego.
Compila pc/tools/test_uiuv.c con pc/src/pc_uvtable.c, vuelca 6 piezas sinteticas (quad, sprite, ST, triangulos, abanico, tira de 6),
comprueba que la clave de C = la de Python, que el OBJ sin editar da una tabla vacia, y que editar una UV de un quad y de un sprite llega
a la busqueda del juego con los valores nuevos y solo en esas piezas.   Sin gcc (p.ej. Windows con solo MSVC) la parte de C se omite y se prueban igualmente el OBJ y la tabla con un volcado hecho en Python.
Uso: python pc/tools/test_uiuv_tools.py   (solo necesita Python 3; sin dependencias externas)"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import uiuv_common as uc


def run(cmd, cwd, **kw):
    r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, **kw)
    if r.returncode:
        raise SystemExit('FALLO: %s\n%s%s' % (' '.join(cmd), r.stdout, r.stderr))
    return r.stdout


SYNTH = [  # (gindex, topologia, fst, n, [x16, y16, u16, v16] * n)
    (2222001, 4, 1, 4, [640, 832, 72, 200, 1664, 832, 1096, 200, 640, 1344, 72, 712, 1664, 1344, 1096, 712]),
    (2222002, 6, 1, 2, [-160, 32, 8, 8, 480, 288, 1032, 520]),
    (2222001, 4, 0, 4, [0, 0, 8, 8, 1024, 0, 8200, 8, 0, 512, 8, 4104, 1024, 512, 8200, 4104]),
    (2222002, 3, 1, 6, [0, 0, 8, 8, 160, 0, 168, 8, 0, 160, 8, 168, 160, 0, 168, 8, 160, 160, 168, 168, 0, 160, 8, 168]),
    (2222001, 5, 1, 5, [80, 80, 40, 40, 160, 80, 120, 40, 160, 160, 120, 120, 80, 160, 40, 120, 0, 80, 0, 40]),
    (2222002, 4, 1, 6, [0, 0, 8, 8, 0, 160, 8, 168, 160, 0, 168, 8, 160, 160, 168, 168, 320, 0, 328, 8, 320, 160, 328, 168]),
]


def write_synth_dump(path):
    with open(path, 'w') as f:
        f.write('# dump sintetico (Python)\n')
        for g, topo, fst, n, flat in SYNTH:
            verts = [tuple(flat[4 * i:4 * i + 4]) for i in range(n)]
            h = uc.piece_hash(g, 512, 256, topo, fst, n, verts)
            row = ['P', '%016x' % h, str(g), '512', '256', str(topo), str(fst), str(n)] + [str(v) for vv in verts for v in vv]
            for x, y, u, v in verts:
                row += ['%.4f' % (u / 16.0 - 0.5), '%.4f' % (v / 16.0 - 0.5)]
            f.write('\t'.join(row) + '\n')


def main():
    bad = 0

    def check(c, msg):
        nonlocal bad
        print(('ok   ' if c else 'FALLO ') + msg)
        bad += 0 if c else 1

    with tempfile.TemporaryDirectory() as d:
        exe = None
        if shutil.which('gcc'):
            exe = os.path.join(d, 'tu')
            run(['gcc', '-O1', '-Wall', '-I', os.path.join(ROOT, 'include'), '-o', exe, os.path.join(ROOT, 'tools', 'test_uiuv.c'),
                 os.path.join(ROOT, 'src', 'pc_uvtable.c')], d)
            out = run([exe, 'dump'], d)
            chash = [int(m.group(1), 16) for m in re.finditer(r'hash \d (\w+)', out)]
            pieces = uc.read_dump(os.path.join(d, 'recvx_uiuv.tsv'))
            check([p.hash for p in pieces] == chash, 'la clave de C coincide con la de Python en las 6 piezas')
        else:
            print('aviso: no hay gcc; se omite la parte de C (clave C = Python y busqueda del juego)')
            write_synth_dump(os.path.join(d, 'recvx_uiuv.tsv'))
            pieces = uc.read_dump(os.path.join(d, 'recvx_uiuv.tsv'))
        check(len(pieces) == 6, 'el volcado tiene 6 piezas sin duplicados (%d)' % len(pieces))

        obj = os.path.join(d, 'm.obj')
        tab = os.path.join(d, 'ui_uv_table.txt')
        tools = os.path.join(ROOT, 'tools')
        out = run([sys.executable, '-I', os.path.join(tools, 'uiuv_dump_to_obj.py'), os.path.join(d, 'recvx_uiuv.tsv'), obj], d)
        print('     ' + out.strip())
        run([sys.executable, '-I', os.path.join(tools, 'uiuv_obj_to_table.py'), obj, os.path.join(d, 'recvx_uiuv.tsv'), tab], d)
        rows = [l for l in open(tab) if l.startswith('P ')]
        check(len(rows) == 0, 'OBJ sin editar -> tabla vacia (%d filas)' % len(rows))

        # editar: quad 0 (todas las esquinas +1 texel en u) y sprite 1 (esquina 0 y 2 desplazadas)
        lines = open(obj).read().split('\n')
        objs = [i for i, l in enumerate(lines) if l.startswith('o p')]
        def bump(obj_idx, vt_indices, du, dv):
            start = objs[obj_idx]
            end = objs[obj_idx + 1] if obj_idx + 1 < len(objs) else len(lines)
            vts = [i for i in range(start, end) if lines[i].startswith('vt ')]
            for k in vt_indices:
                t = lines[vts[k]].split()
                lines[vts[k]] = 'vt %.8f %.8f' % (float(t[1]) + du / 512.0, float(t[2]) - dv / 256.0)
        bump(0, [0, 1, 2, 3], 1.0, 0.0)
        bump(1, [0, 2], 0.5, 0.25)
        open(obj, 'w').write('\n'.join(lines))
        out = run([sys.executable, '-I', os.path.join(tools, 'uiuv_obj_to_table.py'), obj, os.path.join(d, 'recvx_uiuv.tsv'), tab], d)
        print('     ' + out.strip())
        rows = [l for l in open(tab) if l.startswith('P ')]
        check(len(rows) == 2, 'tras editar 2 piezas la tabla tiene 2 filas (%d)' % len(rows))

        if exe:
            out = run([exe, 'lookup'], d)
            res = {int(m.group(1)): m.group(0) for m in re.finditer(r'lookup (\d) (\d)[^\n]*', out)}
            hits = [i for i in range(6) if res[i].split()[2] == '1']
            check(hits == [0, 1], 'el juego encuentra solo las piezas editadas (%s)' % hits)
            q = pieces[0]
            exp = [(u + 1.0, v) for u, v in q.default_uv]
            got = list(map(float, res[0].split()[3:]))
            check(all(abs(got[2 * i] - exp[i][0]) < 0.01 and abs(got[2 * i + 1] - exp[i][1]) < 0.01 for i in range(4)), 'UV del quad = defecto + 1 texel en u')
            s = pieces[1]
            got = list(map(float, res[1].split()[3:]))
            exp = [(s.default_uv[0][0] + 0.5, s.default_uv[0][1] + 0.25), (s.default_uv[1][0] + 0.5, s.default_uv[1][1] + 0.25)]
            check(all(abs(got[2 * i] - exp[i][0]) < 0.01 and abs(got[2 * i + 1] - exp[i][1]) < 0.01 for i in range(2)), 'UV del sprite (esquinas 0 y 2) desplazadas')
    print('RESULTADO: %s' % ('TODO BIEN' if bad == 0 else '%d fallos' % bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
