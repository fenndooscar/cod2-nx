#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""cod2 - PC tool: watch the save file and say what the game put in it, each time it changes.

Why it exists
-------------
"After the second mission the save goes back to the first" is a claim about a *sequence* of writes,
and the port's log does not record one: it says a save was opened and closed, never what map the save
is for. The savegame is a big-endian header, a zlib stream, and inside it the game's own saved dvars --
`mapname`, `ui_campaign`, `ui_nextMission`, `sv_saveOnStartMap`. Those dvars are the campaign
position, and they are readable without a debugger.

So this prints, for every write to the save file, the map it names and the dvars that changed since
the previous write. If the last write before "Continue" names the first mission, the reset is in the
game or in the port's write path; if it names the second, the reset is in the read path or in the
menu. Either way the question stops being a guess.

Usage
    python tools/pc/vigilar_save.py                 # until stopped, polls twice a second
    python tools/pc/vigilar_save.py --segundos 3600
    python tools/pc/vigilar_save.py --ahora         # just print what is there and exit

The game does not have to be running: this reads the files, not the process.
"""

import argparse
import glob
import os
import re
import sys
import time
import zlib

# Where the two copies are: the live package the game mounts, and the readable backup that
# ContentManager::CopiaParaElUsuario keeps next to the runtime.
PATRONES = [
    os.path.join(os.environ.get("LOCALAPPDATA", ""), "cod2-nx", "pc", "datos", "*", "*",
                 "00000001", "savegame.svg", "savegame.svg"),
    os.path.join(os.environ.get("LOCALAPPDATA", ""), "cod2-nx", "pc", "saves", "*", "actual", "*"),
]

# The dvars that say where the campaign is. Everything else in the stream is level geometry names.
INTERESAN = ("mapname", "ui_campaign", "ui_nextMission", "sv_saveOnStartMap", "sv_lastSaveGame",
             "com_lastSaveGame", "campaign", "checkpoint", "level", "mission")


def descomprimir(datos):
    """The zlib stream inside the savegame, or None.

    The savegame is a 0x4D8-byte header followed by one zlib stream; the header's fields are not
    documented anywhere this port has, so the stream is found by trying every zlib signature rather
    than by trusting an offset that a future save might move.
    """
    for m in re.finditer(rb"\x78[\x01\x9c\xda]", datos):
        try:
            salida = zlib.decompressobj().decompress(datos[m.start():])
        except zlib.error:
            continue
        if len(salida) > 4096:
            return salida
    return None


def dvars(crudo):
    """The `name\\0value\\0` pairs the save carries, as a dict.

    They are not length-prefixed in a way this can rely on, so the split is on NUL and a pair is
    taken only when the name looks like a dvar name. That is enough to answer "which map".
    """
    fuera = {}
    partes = crudo.split(b"\x00")
    for i in range(len(partes) - 1):
        nombre = partes[i]
        if not nombre or len(nombre) > 40:
            continue
        if not re.fullmatch(rb"[A-Za-z_][A-Za-z0-9_.]*", nombre):
            continue
        valor = partes[i + 1]
        if len(valor) > 64:
            continue
        try:
            fuera[nombre.decode("ascii")] = valor.decode("ascii")
        except UnicodeDecodeError:
            continue
    return fuera


def leer(ruta):
    try:
        with open(ruta, "rb") as f:
            datos = f.read()
    except OSError:
        return None
    crudo = descomprimir(datos)
    if crudo is None:
        return {"bytes": len(datos), "dvars": {}, "mapa": None}
    d = dvars(crudo)
    # The map is a NUL-padded 64-byte field near the start of the stream, before the dvar block, so it
    # is not a dvar pair and has to be taken from there. It is the first thing in the stream that
    # looks like a level name.
    mapa = None
    for nombre in sorted(glob.glob(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                "..", "..", "assets", "game_root", "*.ff"))):
        corto = os.path.basename(nombre)[:-3]
        if corto.endswith("_load") or corto.startswith("mp_") or corto.startswith("movie_"):
            continue
        if crudo[:512].find(corto.encode()) >= 0:
            mapa = corto
            break
    return {"bytes": len(datos), "dvars": d, "mapa": mapa}


def imprime(etiqueta, estado):
    if estado is None:
        print("%s  (no se puede leer)" % etiqueta)
        return
    interesantes = {k: v for k, v in estado["dvars"].items()
                    if any(t in k.lower() for t in INTERESAN)}
    print("%s  %d byte(s) comprimidos, mapa=%s, %d dvar(s), de ellos %d interesantes"
          % (etiqueta, estado["bytes"], estado["mapa"], len(estado["dvars"]), len(interesantes)))
    for k in sorted(interesantes):
        print("      %s = %s" % (k, interesantes[k]))


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--segundos", type=float, default=0, help="0 = hasta que lo pares")
    p.add_argument("--cada-ms", type=int, default=500)
    p.add_argument("--ahora", action="store_true", help="imprime el estado y sale")
    args = p.parse_args()

    vistos = {}
    fin = time.time() + args.segundos if args.segundos else None
    primera = True
    while primera or fin is None or time.time() < fin:
        primera = False
        algo = False
        for patron in PATRONES:
            for ruta in glob.glob(patron):
                try:
                    firma = (os.path.getmtime(ruta), os.path.getsize(ruta))
                except OSError:
                    continue
                if vistos.get(ruta) == firma:
                    continue
                anterior = vistos.get(ruta)
                vistos[ruta] = firma
                algo = True
                estado = leer(ruta)
                etiqueta = "[%s] %s" % (time.strftime("%H:%M:%S"), ruta)
                if anterior is None:
                    imprime(etiqueta + " (primera lectura)", estado)
                else:
                    imprime(etiqueta + " CAMBIO", estado)
                if estado and anterior is not None:
                    previo = leer_cache.get(ruta, {})
                    nuevos = {k: v for k, v in estado["dvars"].items() if previo.get(k) != v}
                    if nuevos:
                        print("      cambios: " + ", ".join(
                            "%s=%s" % (k, v) for k, v in sorted(nuevos.items())))
                if estado:
                    leer_cache[ruta] = estado["dvars"]
        if args.ahora:
            break
        if not algo and not vistos:
            print("no hay ningun savegame en %s" % PATRONES)
            return 1
        time.sleep(args.cada_ms / 1000.0)
    return 0


leer_cache = {}

if __name__ == "__main__":
    sys.exit(main())
