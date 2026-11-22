#!/usr/bin/env python3
"""Borra definiciones de funciones C por nombre con emparejado de llaves (ignora comentarios/cadenas) y el comentario
inmediatamente anterior. Uso: remove_funcs.py archivo.c nombre [nombre...]. Imprime lo borrado. No toca prototipos."""
import re, sys

def skip_literals(s, i):
    c = s[i]
    if s.startswith('/*', i):
        j = s.find('*/', i + 2); return (len(s) if j < 0 else j + 2)
    if s.startswith('//', i):
        j = s.find('\n', i); return (len(s) if j < 0 else j)
    if c in '"\'':
        j = i + 1
        while j < len(s) and s[j] != c:
            j += 2 if s[j] == '\\' else 1
        return j + 1
    return None

def func_span(s, name):
    for m in re.finditer(r'^[A-Za-z_][\w \t\*]*?\b' + re.escape(name) + r'[ \t]*\(', s, re.M):
        # saltar parametros hasta ')' y ver si sigue '{' (definicion) o ';' (prototipo)
        i = m.end(); depth = 1
        while depth:
            k = skip_literals(s, i)
            if k is not None: i = k; continue
            if s[i] == '(': depth += 1
            elif s[i] == ')': depth -= 1
            i += 1
        j = i
        while j < len(s) and s[j] in ' \t\r\n': j += 1
        if s[j] != '{': continue
        depth = 0; i = j
        while True:
            k = skip_literals(s, i)
            if k is not None: i = k; continue
            if s[i] == '{': depth += 1
            elif s[i] == '}':
                depth -= 1
                if depth == 0: i += 1; break
            i += 1
        end = i
        while end < len(s) and s[end] in ' \t\r': end += 1
        if end < len(s) and s[end] == '\n': end += 1
        start = m.start()
        # comentario inmediatamente anterior (lineas // o bloque /* */ pegado)
        while True:
            prev = s.rfind('\n', 0, start - 1) + 1
            line = s[prev:start].strip()
            if line.startswith('//') or (line.startswith('/*') and line.endswith('*/')):
                start = prev
            elif line.endswith('*/'):
                b = s.rfind('/*', 0, start)
                if b >= 0 and s[s.rfind('\n', 0, b) + 1:b].strip() == '': start = s.rfind('\n', 0, b) + 1
                else: break
            else: break
        return start, end
    return None

if __name__ == '__main__':
    p = sys.argv[1]; s = open(p).read()
    for n in sys.argv[2:]:
        sp = func_span(s, n)
        if not sp: print('NO ENCONTRADA', n); sys.exit(1)
        a, b = sp; print('%s: %d lineas' % (n, s[a:b].count('\n'))); s = s[:a] + s[b:].lstrip('\n')
    open(p, 'w').write(s)
