#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Turns a host address of a PC crash into the guest address of a recompiled function.

Why it exists
-------------
A crash of the PC build is reported as `cod2.exe+0x144D415` (by the exception handler, by the debugger
in tools/pc/vigilante_fallo.cpp, or by a dump). The translated code, app/huecos.toml and the codegen log
all speak in *guest* addresses (`sub_8229AF10`), and the link decides where each of the 21,000 functions
lands without writing it anywhere. app/src/cod2_pc_simbolos.cpp writes logs/simbolos.csv for this tool.

How it resolves an address
--------------------------
`logs/simbolos.csv` has one line per registered function, sorted by host address:

    host;guest,siguiente

The address belongs to the last line whose host is at or below it, and the distance to that function's
start is the offset inside it. `siguiente` is the host of the next function, so the offset can be checked
against how much room there was: past the end means the address is not in a recompiled function (a runtime
helper, a library, or the SDK), and the tool says so instead of reporting a wrong name.

Usage:
    python tools/pc/direccion.py 0x144D415
    python tools/pc/direccion.py 7FF760420000+0x144D415 0x5F0A12
    python tools/pc/direccion.py 0x144D415 --csv app/out/pc/logs/simbolos.csv
"""

import argparse
import bisect
import os
import sys


def leer_csv(ruta):
    """[(host, guest, siguiente)] sorted by host."""
    filas = []
    with open(ruta, encoding="utf-8", errors="ignore") as fh:
        for linea in fh:
            if linea.startswith("#"):
                continue
            partes = linea.strip().split(";")
            if len(partes) != 3:
                continue
            try:
                filas.append((int(partes[0], 16), int(partes[1], 16), int(partes[2], 16)))
            except ValueError:
                continue
    filas.sort()
    return filas


def resolver(filas, direccion):
    """(texto, exacto). `exacto` is False when the address did not land in a function."""
    if not filas:
        return "sin simbolos: no hay logs/simbolos.csv que leer", False
    hosts = [f[0] for f in filas]
    i = bisect.bisect_right(hosts, direccion) - 1
    if i < 0:
        return ("0x%X queda por debajo de la primera funcion recompilada (0x%X)"
                % (direccion, hosts[0]), False)
    host, guest, siguiente = filas[i]
    desplazamiento = direccion - host
    texto = "sub_%08X+0x%X" % (guest, desplazamiento)
    if siguiente and direccion >= siguiente:
        return ("%s  (pero hay 0x%X bytes hasta la siguiente funcion en 0x%X: la direccion cae mas "
                "alla del final de sub_%08X, no es codigo recompilado)"
                % (texto, siguiente - host, siguiente, guest), False)
    return texto, True


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("direcciones", nargs="+",
                   help="direcciones de host: 0x144D415, o base+0x144D415 (lo que imprime el log)")
    p.add_argument("--csv", default=None,
                   help="logs/simbolos.csv (por defecto, el de app/out/pc si existe)")
    args = p.parse_args()

    ruta = args.csv
    if ruta is None:
        for candidato in ("app/out/pc/logs/simbolos.csv", "logs/simbolos.csv"):
            if os.path.exists(candidato):
                ruta = candidato
                break
    if ruta is None or not os.path.exists(ruta):
        print("no encuentro logs/simbolos.csv: ejecuta el juego una vez con --cod2_pc_simbolos=true "
              "(por defecto) y vuelve a pasar la direccion con --csv")
        return 1

    filas = leer_csv(ruta)
    print("%s: %d funciones" % (ruta, len(filas)))
    for texto in args.direcciones:
        direccion = None
        for parte in texto.replace("+", " ").split():
            valor = int(parte, 16)
            direccion = valor if direccion is None else direccion + valor
        if direccion is None:
            continue
        resuelto, exacto = resolver(filas, direccion)
        print("  0x%X  %s" % (direccion, resuelto))
    return 0


if __name__ == "__main__":
    sys.exit(main())