"""Utilidades comunes de las herramientas de UV 2D: lectura del volcado recvx_uiuv.tsv y la clave de pieza (igual que pc/src/pc_uvtable.c)."""
import struct

FNV_OFFSET = 14695981039346656037
FNV_PRIME = 1099511628211
MASK64 = (1 << 64) - 1
TOPO_SPRITE = 6


def piece_hash(gindex, tex_w, tex_h, topology, fst, n, verts):
    """verts: lista de (x16, y16, u16, v16) enteros. Mismo orden y empaquetado que pcUiPieceHash."""
    data = struct.pack('<IIIIII', gindex & 0xFFFFFFFF, tex_w & 0xFFFFFFFF, tex_h & 0xFFFFFFFF,
                       topology & 0xFFFFFFFF, fst & 0xFFFFFFFF, n & 0xFFFFFFFF)
    for x, y, u, v in verts:
        data += struct.pack('<iiii', x, y, u, v)
    h = FNV_OFFSET
    for b in data:
        h ^= b
        h = (h * FNV_PRIME) & MASK64
    return h


class Piece:
    def __init__(self, hash_, gindex, tex_w, tex_h, topology, fst, n, verts, default_uv):
        self.hash, self.gindex, self.tex_w, self.tex_h = hash_, gindex, tex_w, tex_h
        self.topology, self.fst, self.n = topology, fst, n
        self.verts = verts                  # [(x16, y16, u16, v16)]
        self.default_uv = default_uv        # [(u, v)] en texels, lo que usaria el juego sin tabla

    def xy(self, i):
        return self.verts[i][0] / 16.0, self.verts[i][1] / 16.0


def read_dump(path):
    pieces = []
    with open(path, 'r', errors='replace') as f:
        for line in f:
            if not line.startswith('P\t'):
                continue
            c = line.rstrip('\n').split('\t')
            h = int(c[1], 16)
            gindex, tw, th, topo, fst, n = (int(c[i]) for i in range(2, 8))
            ints = list(map(int, c[8:8 + 4 * n]))
            verts = [tuple(ints[4 * i:4 * i + 4]) for i in range(n)]
            fl = list(map(float, c[8 + 4 * n:8 + 6 * n]))
            default_uv = [(fl[2 * i], fl[2 * i + 1]) for i in range(n)]
            if piece_hash(gindex, tw, th, topo, fst, n, verts) != h:
                raise ValueError('hash del volcado no coincide con su contenido: ' + line[:80])
            pieces.append(Piece(h, gindex, tw, th, topo, fst, n, verts, default_uv))
    return pieces


def corners_and_faces(p):
    """Esquinas (x, y, u, v) que se exportan al OBJ, caras (indices de esquina) y, para cada esquina, el par de origen
    al que corresponde (None: solo informativa). Sprite: 4 esquinas; tira de 4: un quad; resto: triangulos."""
    n = p.n
    if p.topology == TOPO_SPRITE and n == 2:
        (x0, y0), (x1, y1) = p.xy(0), p.xy(1)
        (u0, v0), (u1, v1) = p.default_uv[0], p.default_uv[1]
        corners = [(x0, y0, u0, v0), (x1, y0, u1, v0), (x1, y1, u1, v1), (x0, y1, u0, v1)]
        return corners, [[0, 1, 2, 3]], [0, None, 1, None]
    corners = [p.xy(i) + p.default_uv[i] for i in range(n)]
    pair = list(range(n))
    if p.topology == 4 and n == 4:
        return corners, [[0, 1, 3, 2]], pair
    faces = []
    if p.topology == 3:
        faces = [[3 * k, 3 * k + 1, 3 * k + 2] for k in range(n // 3)]
    elif p.topology == 4:
        faces = [[i, i + 1, i + 2] if i % 2 == 0 else [i + 1, i, i + 2] for i in range(n - 2)]
    elif p.topology == 5:
        faces = [[0, i, i + 1] for i in range(1, n - 1)]
    return corners, faces, pair
