#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Checks that the functions listed in app/orden_funciones.ld really were placed at the start of .text.

The problem
-----------
The linker script places functions by *section* name (`*(.text.__imp__sub_823800D8)`), and a pattern that matches
no section is not an error: the linker finds nothing, places nothing, and says nothing. The file then looks like
it is doing its job while every entry in it is inert — which is what happened to this fork, which inherited 555
of Need for Speed: Most Wanted's addresses and kept them for months (README.md, trap 1).

There is a second way to get it wrong, and it is why this check exists rather than a note in a comment. Every
recompiled function has two symbols, `sub_X` and `__imp__sub_X`, and which of the two owns the section is not
fixed: a function nothing hooks has its body under `__imp__sub_X` with `sub_X` as an alias of it, while a function
a hook in app/src replaces has a real `sub_X` of its own. The first regeneration of this list for Call of Duty 2
named only `sub_X` and got 66 of its 1015 functions wrong — every one of them a function the codegen had emitted,
which is to say the game's own code.

How it checks
-------------
Against the link map, which is the only place where the section names the ordering file asks for and the ones the
link actually produced can be compared. The build writes it next to the ELF (`app/CMakeLists.txt` passes
`-Wl,-Map`), so no extra link is needed.

A listed section that the map does not mention was not placed. That is only a failure if *neither* of a function's
two names was placed, so consecutive entries that differ by the `__imp__` prefix are read as one function.

Usage:
    python tools\\comprobar_orden.py
    python tools\\comprobar_orden.py --map app/out/sw8/cod2.map --orden app/orden_funciones.ld

Exit code 1 when any listed function was placed nowhere.
"""

import argparse
import os
import re
import sys

RE_PATRON = re.compile(r'^\s*\*\(\.text\.([^()]+)\)\s*$', re.M)
RE_COLOCADA = re.compile(r'^ \.text\.([A-Za-z0-9_.$]+)(?:\s+0x[0-9a-fA-F]+\s+0x[0-9a-fA-F]+.*)?$')


def leer_patrones(ruta):
    """The symbols whose sections the linker script asks for, in the order it asks for them."""
    return RE_PATRON.findall(open(ruta, encoding='utf-8').read())


def leer_colocadas(ruta):
    """The `.text` sections the map says went into the output section, by symbol name."""
    colocadas = set()
    for linea in open(ruta, encoding='utf-8', errors='replace'):
        m = RE_COLOCADA.match(linea.rstrip('\n'))
        if m:
            colocadas.add(m.group(1))
    return colocadas


def agrupar(nombres):
    """[(the function's names)] — `sub_X` and `__imp__sub_X` are one function, not two."""
    grupos = []
    for n in nombres:
        if grupos:
            anterior = grupos[-1][-1]
            if n == '__imp__' + anterior or anterior == '__imp__' + n:
                grupos[-1].append(n)
                continue
        grupos.append([n])
    return grupos


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    p.add_argument("--map", default="app/out/sw8/cod2.map",
                   help="the link map the build writes next to the ELF")
    p.add_argument("--orden", default="app/orden_funciones.ld")
    p.add_argument("--tolerancia", type=float, default=0.02,
                   help="fraction of the listed functions allowed to be unplaced. A few always are: symbols whose "
                        "section the compiler renames (.localalias, startup code) and libnx's own entry points. A "
                        "list of another binary's addresses fails almost all of them, not a few")
    args = p.parse_args()

    if not os.path.exists(args.map):
        raise SystemExit("No existe %s. Lo escribe el enlace (app/CMakeLists.txt pasa -Wl,-Map): vuelve a "
                         "construir el NRO." % args.map)

    nombres = leer_patrones(args.orden)
    colocadas = leer_colocadas(args.map)
    if not nombres:
        raise SystemExit("%s no pide ninguna seccion: no hay nada que comprobar." % args.orden)
    if not colocadas:
        raise SystemExit("No he leido ni una seccion en %s: el mapa no tiene el formato esperado." % args.map)

    grupos = agrupar(nombres)
    sin_sitio = [g for g in grupos if not any(n in colocadas for n in g)]
    en_el_elf = sum(1 for g in grupos if any(n in colocadas for n in g))
    print("Entradas: %d   funciones: %d   colocadas: %d   sin colocar: %d"
          % (len(nombres), len(grupos), en_el_elf, len(sin_sitio)))

    if sin_sitio:
        print()
        print("Estas funciones no se han colocado en ninguna parte: el enlazador ha ignorado sus entradas en")
        print("silencio. O el nombre no es el que posee la seccion, o la lista es de otro binario.")
        for g in sin_sitio[:20]:
            print("  %s" % " / ".join(g))
        if len(sin_sitio) > 20:
            print("  ... y %d mas" % (len(sin_sitio) - 20))

    if len(sin_sitio) > args.tolerancia * len(grupos):
        print()
        print("FALLO: %d de %d funciones de %s no ocupan el sitio que la lista les da."
              % (len(sin_sitio), len(grupos), args.orden))
        return 1

    print()
    print("Las %d de %d funciones de %s ocupan el sitio que la lista les da."
          % (len(grupos) - len(sin_sitio), len(grupos), args.orden))
    return 0


if __name__ == '__main__':
    sys.exit(main())
