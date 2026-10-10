#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""cod2 - find a string inside the fastfiles, without unpacking them to disk.

Why
    The UI cfg scripts (updategameprofile, loadgame_continue, Updatedvarsfromprofile) and the
    campaign-progress dvars are rawfile assets inside the .ff fastfiles. The assets are stored
    uncompressed, but the container is, so `grep` on the .ff files finds nothing. This walks every
    fastfile, inflates the zlib stream at +0x14 and reports where the needle appears, with context.

Usage
    python tools/pc/buscar_ff.py mis_01
    python tools/pc/buscar_ff.py updategameprofile --contexto 400
    python tools/pc/buscar_ff.py mis_01 --solo moscow
"""

import argparse
import glob
import os
import re
import struct
import sys
import zlib

RAIZ = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "assets", "game_root")


def inflar(datos):
    """The zlib stream of a fastfile: 20-byte big-endian header, stream at +0x14."""
    for inicio in (0x14, 0x10, 0x0):
        try:
            d = zlib.decompressobj()
            salida = d.decompress(datos[inicio:])
        except zlib.error:
            continue
        if len(salida) > 1024:
            return salida
    return None


def main():
    p = argparse.ArgumentParser()
    p.add_argument("aguja")
    p.add_argument("--contexto", type=int, default=160)
    p.add_argument("--solo", default="", help="substring of the fastfile name; empty = all")
    p.add_argument("--max", type=int, default=4, help="matches printed per fastfile")
    args = p.parse_args()

    aguja = args.aguja.encode()
    ficheros = sorted(glob.glob(os.path.join(RAIZ, "*.ff")))
    if args.solo:
        ficheros = [f for f in ficheros if args.solo in os.path.basename(f)]
    total = 0
    for ruta in ficheros:
        nombre = os.path.basename(ruta)
        try:
            with open(ruta, "rb") as f:
                datos = f.read()
        except OSError:
            continue
        crudo = inflar(datos)
        if crudo is None:
            print("%-28s (no se pudo inflar, %d byte(s))" % (nombre, len(datos)))
            continue
        posiciones = [m.start() for m in re.finditer(re.escape(aguja), crudo)]
        if not posiciones:
            continue
        total += len(posiciones)
        print("=" * 100)
        print("### %s: %d aparicion(es), %d byte(s) inflados" % (nombre, len(posiciones), len(crudo)))
        for pos in posiciones[: args.max]:
            ini = max(0, pos - args.contexto)
            fin = min(len(crudo), pos + args.contexto)
            texto = crudo[ini:fin].decode("latin-1").replace("\x00", "\n")
            print("  --0x%X--" % pos)
            print(texto)
    print("-- %d aparicion(es) en total" % total)
    return 0


if __name__ == "__main__":
    sys.exit(main())
