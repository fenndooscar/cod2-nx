#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""cod2 - dump the strings of the mapped guest image out of the running game.

Why
    default.xex is compressed, so the game's data tables and string literals are not readable
    from the file on disk and the recompiled C++ carries them as data references, not as text.
    Once the runtime has mapped the image they are readable from the process. This is how the
    game's own vocabulary for the save system is found, and with it the addresses to xref.

Usage
    python tools/pc/volcar_cadenas.py [--filtrar save] [--min 6]
"""

import argparse
import ctypes
import re
import sys

sys.path.insert(0, __file__.rsplit("\\", 1)[0])

import leer_guest as lg  # noqa: E402

# The guest image, from the runtime's own line:
#   Function table initialized for module: code=82090000-824A8DEC, image=82000000-84290000
IMAGEN = (0x82000000, 0x84290000)
TROZO = 1 << 20


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--filtrar", default="", help="substring, case-insensitive; empty = everything")
    p.add_argument("--min", type=int, default=6)
    p.add_argument("--desde", type=lambda v: int(v, 0), default=0)
    p.add_argument("--hasta", type=lambda v: int(v, 0), default=0)
    args = p.parse_args()

    pids = lg.procesos()
    if not pids:
        print("cod2.exe no esta en marcha")
        return 1
    proceso = lg.abrir(pids[0])
    ventana = lg.ventana_invitado(proceso)
    if ventana is None:
        print("no se encontro la ventana de memoria de invitado")
        return 1

    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    patron = re.compile(rb"[\x20-\x7e]{%d,}" % args.min)
    filtro = args.filtrar.lower().encode()
    total = 0
    inicio, fin = IMAGEN
    minimo = 0
    if args.desde:
        minimo = args.desde
        inicio = args.desde
    if args.hasta:
        fin = args.hasta
    # The read is 1 MB-aligned, but --desde is exact: without this the block is rounded down and
    # the strings below the requested address are printed too, which makes a narrow window useless.
    inicio -= inicio % TROZO
    for base in range(inicio, fin, TROZO):
        buf = ctypes.create_string_buffer(TROZO)
        leidos = ctypes.c_size_t(0)
        if not k32.ReadProcessMemory(proceso, ctypes.c_void_p(ventana + base), buf,
                                     ctypes.c_size_t(TROZO), ctypes.byref(leidos)):
            continue
        crudo = buf.raw[: leidos.value]
        for m in patron.finditer(crudo):
            s = m.group()
            if base + m.start() < minimo:
                continue
            if filtro and filtro not in s.lower():
                continue
            print("0x%08X  %s" % (base + m.start(), s.decode("ascii", "replace")))
            total += 1
    print("-- %d cadena(s)" % total)
    return 0


if __name__ == "__main__":
    sys.exit(main())
