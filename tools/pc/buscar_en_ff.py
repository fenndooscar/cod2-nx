# cod2 - search the inflated text of every fastfile for a byte pattern, with context.
#
# Why this exists
#     `buscar_ff.py` answers "is this string anywhere"; this answers "where, and next to what".
#     The campaign-progress question is about the *neighbourhood* of a name: `mis_01` next to
#     `setDvar` is progression, `mis_01` next to `setfocusbydvar` is a menu label. Without the
#     surrounding bytes the two are indistinguishable.
#
# How to use it
#     python tools/pc/buscar_en_ff.py mis_0 updategamerprofile sv_lastSaveGame
#     python tools/pc/buscar_en_ff.py --solo moscow.ff --contexto 64 mis_
#
# The fastfile layout is the one `tools/pc/buscar_ff.py` documents: a 20-byte big-endian header
# followed by a single zlib stream.

import argparse
import pathlib
import re
import zlib

RAIZ = pathlib.Path(__file__).resolve().parents[2] / "assets" / "game_root"


def inflar(ruta):
    datos = ruta.read_bytes()
    if len(datos) < 0x18:
        return None
    try:
        return zlib.decompressobj().decompress(datos[0x14:], 512 * 1024 * 1024)
    except zlib.error:
        return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("patrones", nargs="+")
    ap.add_argument("--solo", default="", help="only this fastfile name")
    ap.add_argument("--contexto", type=int, default=48)
    ap.add_argument("--max", type=int, default=6, help="hits per pattern per file")
    args = ap.parse_args()

    patron = re.compile(b"|".join(p.encode() for p in args.patrones))
    ficheros = sorted(RAIZ.glob("*.ff"))
    if args.solo:
        ficheros = [f for f in ficheros if f.name == args.solo]

    for fichero in ficheros:
        texto = inflar(fichero)
        if texto is None:
            continue
        salidas = []
        for m in patron.finditer(texto):
            inicio = max(0, m.start() - args.contexto)
            trozo = texto[inicio : m.end() + args.contexto]
            salidas.append("  0x%08X  %s" % (m.start(), trozo.decode("latin-1").replace("\n", "|")))
            if len(salidas) >= args.max:
                break
        if salidas:
            print("### %s (%d B inflados)" % (fichero.name, len(texto)))
            print("\n".join(salidas))
            print()


if __name__ == "__main__":
    main()
