# cod2 - extract the Xbox 360 shader containers out of the `.ff` fastfiles.
#
# Why this exists
#     Call of Duty 2's shaders are not loose files. They are assets inside the `.ff` fastfiles, and a
#     fastfile is a 20-byte big-endian header followed by a single zlib stream, so nothing finds a
#     shader container until the stream is inflated. `shaders/cod2_buscar_contenedores.cpp` can only
#     walk loose files; this tool inflates first and hands it a folder of `.bin` containers.
#
#     This is the piece `docs/building.md` step 7 describes as missing for this game: the container
#     format itself (`shaders/cod2_contenedor.h`) already handles the 2005 layout this game uses.
#
# Why Python and not C++
#     zlib is in the standard library here and is not vendored anywhere the SDK's tools could reach
#     (AGENTS.md rule 4: no new dependency). Inflating 224 MB per fastfile in C++ would mean adding
#     zlib to `shaders/` for ten lines of work.
#
# How to use it
#     python tools/extraer_shaders.py assets/shaders_ff
#     python tools/extraer_shaders.py assets/shaders_ff --solo common
#
# The output folder must not exist. It gets one `.bin` per *distinct* container, named the way
# `cod2_buscar_contenedores.cpp` names them (`v_` vertex, `p_` pixel), plus `procedencia.tsv`: which
# fastfile and which inflated offset each container came from. That index is the only record of where
# a container was found, and it is what a later `--solo` run is compared against.
#
# Nothing here is committed: the containers are game data (AGENTS.md rule 1). `*.bin` is ignored.

import argparse
import hashlib
import pathlib
import re
import struct
import sys
import zlib

# The signature pair of the 2005 container this game uses. The low byte is the tool that wrote it:
# 0 = pixel, 1 = vertex (`cod2::Convertir2005` reads it the same way).
FIRMA = re.compile(rb"\x10\x2a\x0e[\x00\x01]")

# The physical part is CF microcode, three words per instruction. A length that is not a multiple of
# twelve cannot be one, and `cod2_shader_library.cpp` rejects it too.
TAMANO_MAXIMO = 64 * 1024


def inflar(ruta):
    """The inflated contents of a fastfile, or None if it is not one.

    The header is five big-endian dwords and the second one is the inflated size, so a wrong stream
    offset cannot pass silently: it would not inflate to that size.
    """
    datos = ruta.read_bytes()
    if len(datos) < 0x18 or datos[0x14] != 0x78:
        return None
    tamano = struct.unpack(">I", datos[4:8])[0]
    try:
        flujo = zlib.decompressobj()
        texto = flujo.decompress(datos[0x14:]) + flujo.flush()
    except zlib.error:
        return None
    if len(texto) != tamano or not flujo.eof or flujo.unused_data:
        return None
    return texto


def contenedores(texto):
    """Every container candidate in an inflated fastfile, in offset order.

    The test is the one `shaders/cod2_buscar_contenedores.cpp` applies, plus the twelve-byte
    alignment of the physical part. It is a candidate and not a validation: the microcode is checked
    later, by `cod2_hlsl` (which calls `cod2::Convertir2005`) and again by the packer.
    """
    n = len(texto)
    for m in FIRMA.finditer(texto):
        i = m.start()
        if i + 24 > n:
            continue
        cabecera = texto[i : i + 24]
        virtual, fisico, _, tabla, sombra = struct.unpack(">5I", cabecera[4:24])
        if (
            virtual < 24
            or not fisico
            or fisico % 12
            or virtual + fisico > TAMANO_MAXIMO
            or virtual + fisico > n - i
            or tabla < 24
            or tabla >= virtual
            or sombra < 24
            or sombra + 24 > virtual
        ):
            continue
        yield i, virtual, fisico


def main():
    ap = argparse.ArgumentParser(description="Extract the shader containers of the .ff fastfiles")
    ap.add_argument("salida", help="new folder for the containers")
    ap.add_argument(
        "--game-root",
        default=str(pathlib.Path(__file__).resolve().parents[1] / "assets" / "game_root"),
        help="folder with the .ff fastfiles",
    )
    ap.add_argument("--solo", default="", help="only fastfiles whose name contains this")
    args = ap.parse_args()

    salida = pathlib.Path(args.salida)
    if salida.exists():
        print("the output folder already exists: %s" % salida, file=sys.stderr)
        return 1
    raiz = pathlib.Path(args.game_root)
    ficheros = sorted(raiz.glob("*.ff"))
    if args.solo:
        ficheros = [f for f in ficheros if args.solo in f.name]
    if not ficheros:
        print("no .ff fastfile under %s" % raiz, file=sys.stderr)
        return 1
    salida.mkdir(parents=True)

    # Keyed by the container's own bytes: the same shader sits in several fastfiles (code.ff and
    # mp_code.ff are the same file), and translating it twice would only make the packer dedupe it.
    unicos = {}
    indice = ["archivo\tdesplazamiento\tnombre\tfirma\tvirtual\tfisico\n"]
    ilegibles = []
    for fichero in ficheros:
        texto = inflar(fichero)
        if texto is None:
            ilegibles.append(fichero.name)
            print("%-28s NOT a fastfile (header or zlib stream rejected)" % fichero.name)
            continue
        encontrados = 0
        for i, virtual, fisico in contenedores(texto):
            datos = texto[i : i + virtual + fisico]
            huella = hashlib.sha256(datos).digest()
            if huella not in unicos:
                unicos[huella] = (datos, len(unicos))
            nombre = "%c_%06d.bin" % (b"pv"[texto[i + 3] & 1], unicos[huella][1])
            indice.append(
                "%s\t%d\t%s\t0x%08X\t%d\t%d\n"
                % (fichero.name, i, nombre, struct.unpack(">I", texto[i : i + 4])[0], virtual, fisico)
            )
            encontrados += 1
        print("%-28s %3d containers (%d MB inflated)" % (fichero.name, encontrados, len(texto) >> 20))

    for datos, n in unicos.values():
        (salida / ("%c_%06d.bin" % (b"pv"[datos[3] & 1], n))).write_bytes(datos)
    (salida / "procedencia.tsv").write_text("".join(indice), encoding="utf-8", newline="\n")

    pixeles = sum(1 for d, _ in unicos.values() if not (d[3] & 1))
    vertices = len(unicos) - pixeles
    print(
        "\n%d fastfiles, %d unreadable; %d containers, %d distinct "
        "(%d pixel, %d vertex) -> %s"
        % (len(ficheros), len(ilegibles), len(indice) - 1, len(unicos), pixeles, vertices, salida)
    )
    if ilegibles:
        print("unreadable: %s" % " ".join(ilegibles), file=sys.stderr)
    return 0 if unicos else 2


if __name__ == "__main__":
    sys.exit(main())
