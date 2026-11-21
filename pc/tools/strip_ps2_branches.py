#!/usr/bin/env python3
"""Quita de un archivo fuente las ramas que solo existen en la PS2 (TARGET_PC no definido):
   #ifdef TARGET_PC  A  #else  B  #endif   ->  A
   #ifndef TARGET_PC  B  #endif            ->  (nada)
   #ifndef TARGET_PC  B  #else  A #endif   ->  A
Los demas condicionales (#if/#ifdef de otros simbolos) se conservan tal cual dentro de las ramas vivas.
Uso: strip_ps2_branches.py archivo [archivo...]   (reescribe en sitio)"""
import re, sys

def strip(text):
    out = []
    st = []   # (kind, live)   kind: pc | notpc | other
    def live():
        return all(l for k, l in st)
    for line in text.split('\n'):
        t = line.strip()
        m_if = re.match(r'#\s*(ifdef|ifndef)\s+TARGET_PC\b\s*(/\*.*\*/|//.*)?$', t)
        if m_if:
            st.append(('pc', True) if m_if.group(1) == 'ifdef' else ('notpc', False))
            continue
        if re.match(r'#\s*(if|ifdef|ifndef)\b', t):
            st.append(('other', True))
            if live(): out.append(line)
            continue
        if re.match(r'#\s*(else|elif)\b', t) and st:
            k, l = st[-1]
            if k == 'pc':
                st[-1] = ('pc', False)
                continue
            if k == 'notpc':
                st[-1] = ('notpc', True)
                continue
            if live(): out.append(line)
            continue
        if re.match(r'#\s*endif\b', t) and st:
            k, l = st.pop()
            if k == 'other' and live(): out.append(line)
            continue
        if live(): out.append(line)
    assert not st, 'condicionales sin cerrar'
    return '\n'.join(out)

if __name__ == '__main__':
    for p in sys.argv[1:]:
        s = open(p, encoding='utf-8', errors='surrogateescape', newline='').read()
        crlf = '\r\n' in s
        t = strip(s.replace('\r\n', '\n'))
        if crlf: t = t.replace('\n', '\r\n')
        open(p, 'w', encoding='utf-8', errors='surrogateescape', newline='').write(t)
