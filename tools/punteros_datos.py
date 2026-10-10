#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Finds code pointers stored in the game's data, which are function entry points.

Why this is needed
------------------
An indirect call reaches an address the analysis never marked as a function
start, and the game dies:

    [FATAL] Call to invalid or unregistered function at guest address 0x...

The call itself is `bctrl` through a register, so the codegen's Validate phase
cannot see it: it only checks direct branches. What tells us the address is a
function is that the game's own data holds a pointer to it - a vtable slot, a
static initializer table, a callback array. Those pointers are exactly the
values of 4 bytes, big-endian, that land inside .text.

What it does
------------
Reads default.xex, walks it 4 bytes at a time, and collects every big-endian
dword inside the code range that is not already a known function start. The
result is the list of entry points that only a pointer can reach.

  * With --comprobar it also reports whether a given address is one of them,
    which answers "where did 0x82494B78 come from?" without a game run.
  * With --crashes it appends what it finds to a file that tools/huecos.py
    reads, so the declarations come from the binary instead of from one crash
    at a time.

This finds pointers that are aligned and stored as a whole dword, which is how
these tables are built. A pointer built by arithmetic at run time is not
visible here; that one has to come from a crash.

Usage:
    python tools\\punteros_datos.py
    python tools\\punteros_datos.py --comprobar 0x82494B78
    python tools\\punteros_datos.py --crashes tools/huecos_crashes.txt
"""

import argparse
import json
import os
import re
import struct
import sys

# .text of Call of Duty 2, from the XEX information (docs/building.md step 2).
# Overridable so this keeps working if the executable is rebuilt.
TEXTO_INICIO = 0x82090000
TEXTO_FIN = 0x82495E14

RE_FUNC = re.compile(r'^DEFINE_REX_FUNC\((?:sub_)?([0-9A-Fa-f]{8})\)')


def inicios_de_funcion(gen_dir):
    """Addresses the codegen emitted a DEFINE_REX_FUNC for."""
    inicios = set()
    for nombre in sorted(os.listdir(gen_dir)):
        if not nombre.endswith('.cpp'):
            continue
        with open(os.path.join(gen_dir, nombre), encoding='utf-8', errors='replace') as fh:
            for linea in fh:
                m = RE_FUNC.match(linea)
                if m:
                    inicios.add(int(m.group(1), 16))
    return inicios


def punteros_del_binario(ruta, inicio, fin):
    """{address: [file offsets that point at it]} for pointers inside .text.

    Every 4-byte-aligned dword of the file is read big-endian, which is how the
    Xbox 360 stores them, and kept when it lands inside [inicio, fin).
    """
    datos = open(ruta, 'rb').read()
    encontrados = {}
    for off in range(0, len(datos) - 4, 4):
        valor = struct.unpack_from('>I', datos, off)[0]
        if inicio <= valor < fin:
            encontrados.setdefault(valor, []).append(off)
    return encontrados


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    p.add_argument("--xex", default="assets/game_root/default.xex")
    p.add_argument("--gen", default="app/generated/default")
    p.add_argument("--inicio", default="0x%08X" % TEXTO_INICIO)
    p.add_argument("--fin", default="0x%08X" % TEXTO_FIN)
    p.add_argument("--comprobar", nargs="*", default=[],
                   help="addresses to look up, e.g. 0x82494B78")
    p.add_argument("--crashes", default="",
                   help="append the entry points found to this file "
                        "(the one tools/huecos.py reads)")
    p.add_argument("--salida", default="docs/punteros.txt")
    args = p.parse_args()

    inicio, fin = int(args.inicio, 16), int(args.fin, 16)
    if not os.path.exists(args.xex):
        raise SystemExit("No existe %s" % args.xex)

    print("Leyendo %s..." % args.xex)
    punteros = punteros_del_binario(args.xex, inicio, fin)
    print("  %d direcciones distintas de .text citadas por datos" % len(punteros))

    conocidos = inicios_de_funcion(args.gen)
    print("  %d funciones conocidas por el codegen" % len(conocidos))

    faltan = sorted(a for a in punteros if a not in conocidos)
    print("  %d de esas direcciones NO son funciones conocidas" % len(faltan))

    if args.comprobar:
        print()
        for s in args.comprobar:
            a = int(s, 16)
            if a in conocidos:
                print("  0x%08X  ya es una funcion" % a)
            elif a in punteros:
                print("  0x%08X  SI: %d puntero(s) en el fichero, en %s"
                      % (a, len(punteros[a]),
                         " ".join("0x%X" % o for o in punteros[a][:6])))
            else:
                print("  0x%08X  no aparece como puntero: solo una ejecucion lo encuentra" % a)

    os.makedirs(os.path.dirname(args.salida) or ".", exist_ok=True)
    with open(args.salida, "w", encoding="utf-8") as fh:
        fh.write("Direcciones de .text citadas por datos que el codegen no conocia.\n")
        fh.write("Generado por tools/punteros_datos.py.\n\n")
        for a in faltan:
            fh.write("  0x%08X  %d puntero(s)\n" % (a, len(punteros[a])))
    print("Informe en %s" % args.salida)

    if args.crashes:
        with open(args.crashes, "w", encoding="utf-8") as fh:
            fh.write("# Entry points that only a pointer can reach, found by tools/punteros_datos.py\n")
            fh.write("# in the game's own data. tools/huecos.py declares them.\n")
            for a in faltan:
                fh.write("0x%08X\n" % a)
        print("Escritas %d direcciones en %s" % (len(faltan), args.crashes))

    return 0


if __name__ == "__main__":
    sys.exit(main())
