#!/usr/bin/env python3
"""Comprueba que quitar las ramas PS2 no cambia el preprocesado con -DTARGET_PC.
Uso: check_pp_equal.py antes.c despues.c  (los includes que faltan se sustituyen por archivos vacios)"""
import subprocess, sys, os, re, tempfile

def pp(path, stub, incs):
    for _ in range(200):
        r = subprocess.run(['gcc', '-E', '-P', '-DTARGET_PC', '-nostdinc', '-I', stub] + incs + [path],
                           capture_output=True, text=True, errors='replace')
        if r.returncode == 0:
            return re.sub(r'\s+', ' ', r.stdout).strip()
        m = re.search(r'fatal error: ([^:]+): No such file', r.stderr)
        if not m: raise SystemExit('gcc -E fallo: ' + r.stderr[:400])
        f = os.path.join(stub, m.group(1)); os.makedirs(os.path.dirname(f) or stub, exist_ok=True); open(f, 'w').close()
    raise SystemExit('demasiados includes')

if __name__ == '__main__':
    a, b = sys.argv[1], sys.argv[2]
    incs = sys.argv[3:]
    with tempfile.TemporaryDirectory() as stub:
        pa = pp(a, stub, incs); pb = pp(b, stub, incs)
    print('IGUAL' if pa == pb else 'DISTINTO', len(pa))
    sys.exit(0 if pa == pb else 1)
