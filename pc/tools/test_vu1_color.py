#!/usr/bin/env python3
"""Extrae las 11 funciones vu1GetVertexColor* transpiladas (rama TARGET_PC) de ps2_Vu1Strip.c y ejecuta test_vu1_color.c."""
import os, re, subprocess, sys, tempfile
root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
src = open(os.path.join(root, 'src/ps2/veronica/prog/ps2_Vu1Strip.c')).read().split('\n')
out = []
i = 0
while i < len(src):
    m = re.match(r'void (vu1GetVertexColor\w*)\(VU1_STRIP_BUF\* pStrip, VU1_PRIM_BUF\* pPrim\)', src[i])
    if m:
        out.append('static void ref_%s(VU1_STRIP_BUF* pStrip, VU1_PRIM_BUF* pPrim)' % m.group(1))
        i += 1
        while src[i].strip() != '#else':
            if src[i].strip() != '#ifdef TARGET_PC':
                out.append(src[i])
            i += 1
        out.append('}')
    i += 1
d = tempfile.mkdtemp()
open(os.path.join(d, 'vu1_color_bodies.h'), 'w').write('\n'.join(out) + '\n')
exe = os.path.join(d, 'test_vu1_color')
subprocess.check_call(['gcc', '-O1', '-no-pie', '-w', '-DTARGET_PC', '-I' + d, '-I' + os.path.join(root, 'pc/include'),
                       os.path.join(root, 'pc/tools/test_vu1_color.c'), '-o', exe, '-lm'])
sys.exit(subprocess.call([exe]))
