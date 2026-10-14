"""Reads the pass-by-pass depth dump out of a cod2 log and prints it as a sequence.

The depth instrumentation prints two kinds of line and they only make sense together: the identity of every
pass with a depth attachment (which colour target, whether it cleared, how many of its draws write depth) and
the dump of the image itself, before and after a pass. Reading an order out of one dump per frame is
impossible; this prints the whole window in order, which is what says whether the colour pass received the
depth the pre-pass wrote - the render area's fingerprint is the same number in both dumps when it did.

Usage: python tools/pc/leer_secuencia_pases.py cod2_197.log [desde_fotograma]
"""

import os
import re
import sys

ruta = os.path.join(os.path.expandvars(r'%LOCALAPPDATA%\cod2-nx\pc\logs'), sys.argv[1])
desde = int(sys.argv[2]) if len(sys.argv) > 2 else 0

RE_PASE = re.compile(
    r'C6 pase del fotograma (\d+) numero (\d+): (\d+)x(\d+), (\d+) dibujos dentro, borra prof (\d), '
    r'(\d+) escritores de Z, primer dibujo VS n(-?\d+) PS n(-?\d+) control Z (\w{8})')
RE_COLOR = re.compile(r'color0=f(\d+)/base(\w+)')
RE_PROF = re.compile(r'prof=base(\w+)')
RE_VOLCADO = re.compile(
    r'C6 volcado: (?:fotograma (\d+) )?(ANTES del pase|DESPUES del pase) (\d+)(?: con (\d+) dibujos dentro)?')
RE_AREA = re.compile(
    r'area \d+x\d+: (\d+) texeles, (\d+) distintos, (\d+) lejos \(([\d.]+) %\), (\d+) a cero \(([\d.]+) %\), '
    r'(\d+) con geometria \(([\d.]+) %\), huella (\w{16})')
RE_SIN = re.compile(r'C6 pase del fotograma (\d+) numero (\d+): .*\*\*\* SIN PROFUNDIDAD \*\*\*')


def main():
    vistos = 0
    for linea in open(ruta, encoding='utf-8', errors='replace'):
        t = re.sub(r'^.*\[nativo\] ', '', linea).rstrip()
        m = RE_PASE.search(t)
        if m:
            fot, num, ancho, alto, dentro, borra, esc, vs, ps, cz = (
                int(m.group(1)), int(m.group(2)), int(m.group(3)), int(m.group(4)), int(m.group(5)),
                int(m.group(6)), int(m.group(7)), int(m.group(8)), int(m.group(9)), m.group(10))
            if fot < desde:
                continue
            color = 'f%s/base%s' % (RE_COLOR.search(t).group(1), RE_COLOR.search(t).group(2)) \
                if RE_COLOR.search(t) else 'sin color'
            prof = RE_PROF.search(t).group(1) if RE_PROF.search(t) else '-'
            sin = ' *** SIN PROFUNDIDAD ***' if 'SIN PROFUNDIDAD' in t else ''
            print('f%-6d pase %-3d %sx%-5s dentro=%-3d borra=%d escritoresZ=%-4d VS n%-5d PS n%-6s ctrlZ %s '
                  '%-16s prof=base%s%s' % (fot, num, ancho, alto, dentro, borra, esc, vs, ps, cz, color,
                                           prof, sin))
            vistos += 1
            continue
        m = RE_VOLCADO.search(t)
        if m:
            print('        volcado f%s %s %s%s' % (m.group(1) or '-', m.group(2), m.group(3),
                                                   (' con %s dibujos' % m.group(4)) if m.group(4) else ''))
            continue
        m = RE_AREA.search(t)
        if m:
            print('            %s distintos, %s lejos (%s %%), %s a cero, %s geom (%s %%), huella %s' % (
                m.group(2), m.group(3), m.group(4), m.group(5), m.group(7), m.group(8), m.group(9)))
    print('%d pases con profundidad impresos' % vistos)


if __name__ == '__main__':
    main()
