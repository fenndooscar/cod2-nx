# cod2 - check that the fastfile unpacking and the container naming still hold.
#
# Why this exists
#     Two things in this path fail silently, and both did:
#
#       * The fastfile layout. A `.ff` is a 20-byte big-endian header followed by a single zlib
#         stream, and the header's second dword is the inflated size. Read the stream from the wrong
#         offset and zlib either errors out or, worse, hands back a shorter buffer that still looks
#         like game data. The size is what makes that impossible, so it is checked and not assumed.
#       * The stage. The low bit of a container's signature is 0 for a pixel shader and 1 for a vertex
#         one, and `tools/extraer_shaders.py` names the file after it (`p_` / `v_`). That mapping is
#         easy to write backwards and it was: every file came out with the other stage's letter, and
#         the only reason it was caught is that `dxc -T ps_6_6` refuses an HLSL body that declares
#         `[shader("vertex")]`. Nothing in the repository checks the letter against the body.
#
# How to use it
#     python tools/comprobar_shaders_ff.py
#     python tools/comprobar_shaders_ff.py assets/shaders_ff
#
#     The first half builds its own fastfile and containers and needs no game data. The second half
#     runs only if the extraction folder exists, and then checks it against the real corpus: the
#     index against the files, and each `.hlsl`'s stage against the container it came from. It exits
#     non-zero on the first thing that does not hold.
#
#     `<carpeta>` is what `tools/extraer_shaders.py` was given, and it holds `contenedores/` (the
#     `.bin` files and `procedencia.tsv`) and `hlsl/` once `shaders/cod2_regenerar_biblioteca.ps1`
#     has run.

import pathlib
import struct
import sys
import zlib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from extraer_shaders import FIRMA, TAMANO_MAXIMO, contenedores, inflar  # noqa: E402


def exigir(condicion, mensaje):
    if not condicion:
        raise AssertionError(mensaje)


def cabecera(firma=0x102A0E00, virtual=48, fisico=12, ct=24, sombra=24):
    return struct.pack(">6I", firma, virtual, fisico, 0, ct, sombra)


def fastfile(texto):
    """A fastfile around an inflated body: five big-endian dwords, then zlib."""
    return struct.pack(">5I", 0x68, len(texto), 0, 0, 0) + zlib.compress(texto)


def probar_formato(tmp):
    # A well-formed fastfile: one pixel container at a known offset.
    relleno = b"\x00" * 100
    contenedor = cabecera() + b"\x00" * 36
    texto = relleno + contenedor
    ruta = tmp / "sintetico.ff"
    ruta.write_bytes(fastfile(texto))
    exigir(inflar(ruta) == texto, "the fastfile did not inflate to the size its header claims")

    hallados = list(contenedores(texto))
    exigir(hallados == [(100, 48, 12)], "expected one container at 100, got %r" % (hallados,))
    exigir(not (contenedor[3] & 1), "the synthetic container is a pixel one")

    # The header's size is what catches a wrong stream offset. Corrupt it and the file must be
    # rejected rather than silently believed.
    malo = fastfile(texto)
    ruta.write_bytes(malo[:4] + struct.pack(">I", len(texto) + 1) + malo[8:])
    exigir(inflar(ruta) is None, "a fastfile whose size disagrees with its header was accepted")
    # Trailing bytes after the stream: the header sizes the stream, so this is a different file.
    ruta.write_bytes(fastfile(texto) + b"\x00")
    exigir(inflar(ruta) is None, "a fastfile with data after the zlib stream was accepted")
    # Not a fastfile at all.
    ruta.write_bytes(b"\x00" * 64)
    exigir(inflar(ruta) is None, "a file that is not a fastfile was accepted")
    ruta.write_bytes(b"")
    exigir(inflar(ruta) is None, "an empty file was accepted")

    # The filter, one broken field at a time. Each of these is a real way to pick up bytes that are
    # not a shader container.
    casos = {
        "truncated virtual part": cabecera(virtual=12),
        "unaligned physical part": cabecera(fisico=13),
        "zero physical part": cabecera(fisico=0),
        "constant table before the header": cabecera(ct=12),
        "constant table past the virtual part": cabecera(ct=60),
        "shadow table past the virtual part": cabecera(sombra=40),
        "physical part past the file": cabecera(virtual=48, fisico=12000),
        "virtual part over the size limit": cabecera(virtual=TAMANO_MAXIMO, fisico=12),
    }
    for nombre, c in casos.items():
        exigir(list(contenedores(c + b"\x00" * 256)) == [], "accepted: %s" % nombre)

    # Both signatures, and the stage letter the file name gets from each. The letter comes from the
    # low bit of the signature's last byte: 0 is a pixel shader, 1 a vertex one.
    for firma, esperada in ((0x102A0E00, "p"), (0x102A0E01, "v")):
        c = cabecera(firma=firma) + b"\x00" * 36
        exigir(list(contenedores(c)) == [(0, 48, 12)], "0x%08X was not found" % firma)
        exigir("pv"[c[3] & 1] == esperada, "0x%08X got the wrong stage letter" % firma)
    exigir(FIRMA.search(b"\x10\x2a\x0e\x02") is None, "a signature outside the pair was accepted")


def probar_corpus(extraidos):
    if not extraidos.is_dir():
        print("no %s: the corpus half is skipped" % extraidos)
        return
    exigir((extraidos / "contenedores").is_dir(), "%s has no contenedores/" % extraidos)
    indice = (extraidos / "contenedores" / "procedencia.tsv").read_text(encoding="utf-8").splitlines()[1:]
    exigir(indice, "procedencia.tsv has no rows")
    vistos = set()
    for linea in indice:
        archivo, desplazamiento, nombre, firma, virtual, fisico = linea.split("\t")
        vistos.add(nombre)
        datos = (extraidos / "contenedores" / nombre).read_bytes()
        exigir(
            len(datos) == int(virtual) + int(fisico),
            "%s is not virtualSize + physicalSize" % nombre,
        )
        exigir(
            struct.unpack(">I", datos[:4])[0] == int(firma, 16),
            "%s does not carry the signature the index records" % nombre,
        )
        exigir(
            "pv"[datos[3] & 1] == nombre[0],
            "%s is named for the wrong stage" % nombre,
        )
    exigir(
        vistos == {p.name for p in (extraidos / "contenedores").glob("[pv]_*.bin")},
        "the index and the folder do not name the same containers",
    )

    # The check that would have caught the inverted stage: the translator writes the stage into the
    # HLSL it produces, out of the same signature bit. If the two disagree, every file is misnamed.
    hlsl = extraidos / "hlsl"
    if not hlsl.is_dir():
        print("no hlsl/: the stage half is skipped")
        print("%d containers (%d distinct) indexed and named consistently" % (len(indice), len(vistos)))
        return
    cuenta = 0
    for f in sorted(hlsl.glob("[pv]_*.hlsl")):
        cuerpo = f.read_text(encoding="utf-8", errors="replace")
        exigir(
            ('[shader("pixel")]' in cuerpo) != ('[shader("vertex")]' in cuerpo),
            "%s declares both stages or neither" % f.name,
        )
        esperado = "pixel" if f.name[0] == "p" else "vertex"
        exigir('[shader("%s")]' % esperado in cuerpo, "%s is the wrong stage" % f.name)
        exigir((extraidos / "contenedores" / (f.stem + ".bin")).is_file(), "%s has no container" % f.name)
        cuenta += 1
    exigir(cuenta == len(vistos), "the HLSL folder does not cover the distinct containers")
    print(
        "%d containers (%d distinct): index, name and translated stage all agree"
        % (len(indice), cuenta)
    )


def main():
    extraidos = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else (
        pathlib.Path(__file__).resolve().parents[1] / "assets" / "shaders_ff")
    tmp = pathlib.Path(__file__).resolve().parent / "salida_comprobar_shaders_ff"
    tmp.mkdir(exist_ok=True)
    try:
        probar_formato(tmp)
        print("fastfile header, zlib stream and container filter: ok")
        probar_corpus(extraidos)
    finally:
        for f in tmp.glob("*"):
            f.unlink()
        tmp.rmdir()
    print("ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
