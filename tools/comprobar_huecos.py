#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Checks that every address declared in app/huecos.toml actually got a body.

The problem
-----------
The codegen registers every declared address in its function table
(`cod2_register.cpp`) but only emits a body for the ones it could analyse. A
declaration without a body is not a codegen error: it fails much later, at link
time, as

    lld-link: error: undefined symbol: sub_82494C54
    >>> referenced by .../cod2_init.cpp.obj:(struct PPCFuncMapping *PPCFuncMappings)

with no mention of huecos.toml. The two known causes are in
tools/huecos_excluir.txt: data blocks embedded in .text ("not in any code
region" in the codegen log) and import thunks.

Usage:
    python tools\\comprobar_huecos.py
    python tools\\comprobar_huecos.py --log app/codegen.log

Exit code 1 when something is declared without a body, so it can be used as the
step between the codegen and the build.
"""

import argparse
import glob
import os
import re
import sys

RE_DECL = re.compile(r'^"0x([0-9A-Fa-f]{8})"')
RE_CUERPO = re.compile(r'DEFINE_REX_FUNC\((?:sub_)?([0-9A-Fa-f]{8})\)')
RE_FUERA = re.compile(r'function 0x([0-9A-Fa-f]{8}) not in any code region')


def leer_declaradas(ruta):
    """{address: the line that declared it}."""
    declaradas = {}
    if not os.path.exists(ruta):
        raise SystemExit("No existe %s" % ruta)
    for linea in open(ruta, encoding='utf-8'):
        m = RE_DECL.match(linea)
        if m:
            declaradas[int(m.group(1), 16)] = linea.strip()
    return declaradas


def leer_cuerpos(gen_dir):
    """Addresses the codegen emitted a DEFINE_REX_FUNC for."""
    cuerpos = set()
    for ruta in glob.glob(os.path.join(gen_dir, '*.cpp')):
        for linea in open(ruta, encoding='utf-8', errors='replace'):
            m = RE_CUERPO.match(linea)
            if m:
                cuerpos.add(int(m.group(1), 16))
    return cuerpos


def leer_fuera_de_region(ruta):
    """Addresses the codegen reported as 'not in any code region'."""
    fuera = set()
    if ruta and os.path.exists(ruta):
        for linea in open(ruta, encoding='utf-8', errors='ignore'):
            m = RE_FUERA.search(linea)
            if m:
                fuera.add(int(m.group(1), 16))
    return fuera


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    p.add_argument("--gen", default="app/generated/default")
    p.add_argument("--huecos", default="app/huecos.toml")
    p.add_argument("--log", default="app/codegen.log")
    args = p.parse_args()

    declaradas = leer_declaradas(args.huecos)
    cuerpos = leer_cuerpos(args.gen)
    fuera = leer_fuera_de_region(args.log)

    sin_cuerpo = sorted(a for a in declaradas if a not in cuerpos)
    print("Declaradas: %d   con cuerpo: %d   sin cuerpo: %d"
          % (len(declaradas), len(declaradas) - len(sin_cuerpo), len(sin_cuerpo)))

    if sin_cuerpo:
        print()
        print("Estas declaraciones rompen el enlace con 'undefined symbol: sub_XXXXXXXX'.")
        print("Pasar a tools/huecos_excluir.txt y regenerar con tools/huecos.py.")
        for a in sin_cuerpo:
            motivo = "fuera de toda region de codigo" if a in fuera else "sin cuerpo, causa desconocida"
            print("  0x%08X  %s" % (a, motivo))
        return 1

    print("Todas las declaraciones tienen cuerpo: el enlace no se rompera por huecos.toml.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
