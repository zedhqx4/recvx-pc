#!/usr/bin/env python3
"""Lista las definiciones de funciones C que existian en la revision REV y ya no existen en el arbol de trabajo (src + pc/src),
y avisa de las que todavia tienen referencias. Uso: check_defs.py REV"""
import re, subprocess, sys
rev = sys.argv[1]
D = lambda s: set(re.findall(r'^(?:static\s+)?(?:unsigned\s+|const\s+)?[\w\*]+\s+\**(\w+)\s*\([^;{]*\)\s*\n?\{', s, re.M))
files = [f for f in subprocess.run(['git', 'ls-files', 'src', 'pc/src'], capture_output=True, text=True).stdout.split() if f.endswith(('.c', '.cpp'))]
old = set(); new = set(); txt = {}
for f in files:
    r = subprocess.run(['git', 'show', '%s:%s' % (rev, f)], capture_output=True)
    if r.returncode == 0: old |= D(r.stdout.decode('latin1'))
    t = open(f, errors='ignore').read(); new |= D(t); txt[f] = t
gone = sorted(old - new); print(len(gone), 'definiciones desaparecidas:', gone)
for f in subprocess.run(['git', 'ls-files', 'include', 'pc/include'], capture_output=True, text=True).stdout.split():
    if f.endswith('.h'): txt[f] = open(f, errors='ignore').read()
for n in gone:
    for f, t in txt.items():
        for m in re.finditer(r'\b' + n + r'\b', t):
            line = t[t.rfind('\n', 0, m.start()) + 1:t.find('\n', m.end())]
            if not line.lstrip().startswith(('/*', '*', '//')) and '"' not in line:
                print('REF', n, f, line.strip()[:90])
