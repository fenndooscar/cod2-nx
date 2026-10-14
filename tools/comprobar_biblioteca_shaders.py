# cod2 - read the packed shader library (cod2_shaders.nfsp) and name the shaders an index refers to.
#
# WHY IT EXISTS
# The renderer's logs speak of shaders by their index into the packed library ("VS n353", "PS n620"), and the
# library is sorted by fingerprint, so the index cannot be mapped to a file by sorting the directory of
# containers. That made two log lines impossible to investigate: every attempt to find out what the shaders of
# a suspicious draw actually do ended in guesswork.
#
# WHAT IT DOES
# Reads the library with the same layout app/src/cod2_shader_library.cpp writes and validates:
#   'NFSSPV\0\0' | u32 version | u32 count | u64 body hash, then per entry
#   u32 original bytes | u32 SPIR-V words | u64 fingerprint | original container | SPIR-V.
# It then matches entry N's container bytes against the containers extracted to assets/shaders_ff/contenedores
# and prints the matching .bin and the .hlsl translated from it, so the shader can be read.
#
# It also checks the invariant the library relies on: the entries must be sorted by (fingerprint, container),
# so index N is exactly the (N+1)th entry of the file. If that ever stops holding, every index in every log
# line means something else and the file has to be looked at, not the log.
#
# Usage:
#   python tools/comprobar_biblioteca_shaders.py                     # verify the library and its sort order
#   python tools/comprobar_biblioteca_shaders.py 353 620             # name those two indices
#   python tools/comprobar_biblioteca_shaders.py 353 --biblioteca X --contenedores Y
import argparse
import hashlib
import pathlib
import struct
import sys

FIRMA = b"NFSSPV\x00\x00"
CABECERA = 8 + 4 + 4 + 8
MAX_ENTRADAS = 4096


def leer_biblioteca(datos: bytes):
    """Returns ([(original, spirv, huella)], body_hash) in file order."""
    if len(datos) < CABECERA or datos[:8] != FIRMA:
        raise ValueError("firma de paquete desconocida")
    version, cantidad = struct.unpack_from("<II", datos, 8)
    if version != 1:
        raise ValueError(f"version de paquete no admitida: {version}")
    if not 0 < cantidad <= MAX_ENTRADAS:
        raise ValueError(f"cantidad de shaders fuera de limites: {cantidad}")
    (huella_cuerpo,) = struct.unpack_from("<Q", datos, 16)
    entradas = []
    posicion = CABECERA
    for _ in range(cantidad):
        original_n, palabras_n, huella = struct.unpack_from("<IIQ", datos, posicion)
        posicion += 16
        original = datos[posicion:posicion + original_n]
        posicion += original_n
        spirv = datos[posicion:posicion + palabras_n * 4]
        posicion += palabras_n * 4
        if len(original) != original_n or len(spirv) != palabras_n * 4:
            raise ValueError("paquete de shaders truncado")
        entradas.append((original, spirv, huella))
    if posicion != len(datos):
        raise ValueError(f"datos sobrantes: {len(datos) - posicion} bytes")
    return entradas, huella_cuerpo


def comprobar_orden(entradas):
    """The library is searched with lower_bound on the fingerprint, so the file must be sorted by it."""
    problemas = []
    for i in range(1, len(entradas)):
        anterior, actual = entradas[i - 1], entradas[i]
        if (anterior[2], anterior[0]) >= (actual[2], actual[0]):
            problemas.append(f"entradas {i - 1} y {i} desordenadas o repetidas")
    return problemas


def indice_de_contenedor(contenedores: pathlib.Path):
    """Container bytes -> file name, for naming an entry of the library."""
    indice = {}
    for f in sorted(contenedores.glob("*.bin")):
        indice[hashlib.sha256(f.read_bytes()).digest()] = f
    return indice


def main():
    raiz = pathlib.Path(__file__).resolve().parent.parent
    p = argparse.ArgumentParser(description="Nombra los shaders de la biblioteca empaquetada")
    p.add_argument("indices", nargs="*", type=int, help="indices tal y como salen en el registro (VS nNNN)")
    p.add_argument("--biblioteca",
                   default=str(raiz / "assets" / "shaders_ff" / "cod2_shaders.nfsp"))
    p.add_argument("--contenedores", default=str(raiz / "assets" / "shaders_ff" / "contenedores"))
    p.add_argument("--hlsl", default=str(raiz / "assets" / "shaders_ff" / "hlsl"))
    a = p.parse_args()

    biblioteca = pathlib.Path(a.biblioteca)
    if not biblioteca.is_file():
        print(f"no hay biblioteca en {biblioteca}")
        return 2
    entradas, huella_cuerpo = leer_biblioteca(biblioteca.read_bytes())
    print(f"{biblioteca.name}: {len(entradas)} shaders, huella del cuerpo {huella_cuerpo:016X}")
    problemas = comprobar_orden(entradas)
    if problemas:
        print("ORDEN INCORRECTO: los indices del registro no valen")
        for x in problemas[:5]:
            print("  " + x)
        return 1
    vertices = sum(1 for o, _, _ in entradas if struct.unpack_from(">I", o, 0)[0] & 1)
    print(f"orden correcto (por huella y contenedor); {vertices} de vertices y {len(entradas) - vertices} "
          f"de pixeles")

    indice = indice_de_contenedor(pathlib.Path(a.contenedores))
    print(f"contenedores conocidos: {len(indice)}")
    faltan = 0
    for n in a.indices:
        if not 0 <= n < len(entradas):
            print(f"n{n}: fuera de la biblioteca (0..{len(entradas) - 1})")
            continue
        original, spirv, huella = entradas[n]
        es_vs = bool(struct.unpack_from(">I", original, 0)[0] & 1)
        f = indice.get(hashlib.sha256(original).digest())
        print(f"n{n}: {'VS' if es_vs else 'PS'} huella {huella:016X} contenedor {len(original)} B, "
              f"SPIR-V {len(spirv) // 4} palabras -> "
              + (f.name if f else "NINGUN .bin coincide (¿otra extraccion?)")
              + ("" if not f else f"  ({pathlib.Path(a.hlsl) / (f.stem + '.hlsl')})"))
        if not f:
            faltan += 1
    return 1 if faltan else 0


if __name__ == "__main__":
    sys.exit(main())
