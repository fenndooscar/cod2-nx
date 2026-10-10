# cod2 - traduce un desplazamiento dentro de una DLL del SDK a su funcion, usando .pdata y el mapa.
#
# Por que existe
#   rexruntime.dll se enlaza sin tabla de simbolos, asi que una direccion de host capturada en una
#   excepcion no dice nada. Dos fuentes de informacion, y hay que usar las dos:
#     * .pdata (RUNTIME_FUNCTION): los limites reales de cada funcion. Existe siempre en x86-64.
#     * el mapa del enlazador (-Wl,/MAP): el nombre. Solo cubre una parte de las funciones, pero es
#       la unica fuente de nombres que hay.
#   El mapa por si solo no sirve para buscar "el simbolo anterior": sin limites, un desplazamiento
#   grande cae dentro del simbolo equivocado y el resultado (+0x8BCA) es una mentira. Primero .pdata
#   para saber en que funcion esta, luego el mapa por la direccion exacta de su inicio.
#
# Uso
#   python tools/pc/dllmap.py 0x6A3DB 0x28123A
#   python tools/pc/dllmap.py --dll rexgpu-xenos 0x1234
#   python tools/pc/dllmap.py --dll rexruntime --map sdk/out/win-amd64/rexruntime.map 0x6A3DB
#
# El desplazamiento es el del modulo cargado, que es lo que imprime GetModuleHandleEx: una RVA, no una
# direccion de seccion.

import argparse
import bisect
import os
import re
import struct
import sys

MAPA = re.compile(r"^\s+[0-9A-F]{4}:[0-9A-F]{8}\s+(\S+)\s+([0-9A-F]{16})\s+(.*)$")


def leer_secciones(ruta):
    """Devuelve {nombre: (direccion_virtual, tamano, desplazamiento_en_fichero)} de un PE."""
    with open(ruta, "rb") as f:
        datos = f.read()
    if datos[:2] != b"MZ":
        sys.exit("%s no es un PE" % ruta)
    pe = struct.unpack_from("<I", datos, 0x3C)[0]
    if datos[pe : pe + 4] != b"PE\0\0":
        sys.exit("%s no es un PE valido" % ruta)
    numero_secciones = struct.unpack_from("<H", datos, pe + 6)[0]
    tamano_opcional = struct.unpack_from("<H", datos, pe + 20)[0]
    tabla = pe + 24 + tamano_opcional
    secciones = {}
    for i in range(numero_secciones):
        base = tabla + i * 40
        nombre = datos[base : base + 8].rstrip(b"\0").decode("ascii", "replace")
        tamano_virtual, direccion, tamano_crudo, desplazamiento = struct.unpack_from("<IIII", datos, base + 8)
        secciones[nombre] = (direccion, tamano_virtual or tamano_crudo, desplazamiento)
    return datos, secciones


def leer_pdata(ruta):
    """Devuelve la lista ordenada (inicio, fin) de cada funcion, segun .pdata."""
    datos, secciones = leer_secciones(ruta)
    if ".pdata" not in secciones:
        return []
    direccion, tamano, desplazamiento = secciones[".pdata"]
    funciones = []
    for pos in range(0, tamano, 12):
        inicio, fin, _ = struct.unpack_from("<III", datos, desplazamiento + pos)
        if inicio and fin > inicio:
            funciones.append((inicio, fin))
    funciones.sort()
    return funciones


def leer_mapa(ruta, base_preferida=0x180000000):
    if not os.path.exists(ruta):
        return [], []
    direcciones, nombres = [], []
    with open(ruta, encoding="utf-8", errors="ignore") as f:
        for linea in f:
            m = MAPA.match(linea.rstrip())
            if not m:
                continue
            valor = int(m.group(2), 16)
            if valor < base_preferida:
                continue
            direcciones.append(valor - base_preferida)
            nombres.append((m.group(1), m.group(3).strip()))
    orden = sorted(range(len(direcciones)), key=lambda i: direcciones[i])
    return [direcciones[i] for i in orden], [nombres[i] for i in orden]


def main():
    raiz = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    ap = argparse.ArgumentParser(description="Desplazamiento en una DLL del SDK -> funcion y nombre.")
    ap.add_argument("desplazamientos", nargs="+", help="0x6A3DB (relativo a la base del modulo)")
    ap.add_argument("--dll", default="rexruntime", help="nombre del modulo sin extension")
    ap.add_argument("--map", default=None, help="ruta del .map (por defecto junto a la DLL)")
    ap.add_argument("--dll-dir", default=None, help="carpeta donde esta la DLL (por defecto app/out/pc)")
    args = ap.parse_args()

    carpeta = args.dll_dir or os.path.join(raiz, "app", "out", "pc")
    ruta_dll = os.path.join(carpeta, args.dll + ".dll")
    ruta_mapa = args.map or os.path.join(raiz, "sdk", "out", "win-amd64", args.dll + ".map")
    if not os.path.exists(ruta_dll):
        sys.exit("no encuentro %s" % ruta_dll)

    funciones = leer_pdata(ruta_dll)
    if not funciones:
        sys.exit("%s no tiene .pdata: no puedo delimitar funciones" % ruta_dll)
    direcciones, nombres = leer_mapa(ruta_mapa)
    print("%s: %d funciones en .pdata, %d nombres en %s"
          % (os.path.basename(ruta_dll), len(funciones), len(direcciones),
             os.path.basename(ruta_mapa) if direcciones else "(sin mapa)"))

    inicios = [f[0] for f in funciones]
    for texto in args.desplazamientos:
        valor = int(texto, 16)
        i = bisect.bisect_right(inicios, valor) - 1
        if i < 0 or valor >= funciones[i][1]:
            print("  0x%-8X  fuera de toda funcion (¿no es codigo?)" % valor)
            continue
        inicio, fin = funciones[i]
        j = bisect.bisect_left(direcciones, inicio)
        etiqueta = "sin nombre en el mapa"
        if j < len(direcciones) and direcciones[j] == inicio:
            etiqueta = "%s  [%s]" % (nombres[j][0], nombres[j][1])
        elif j > 0:
            etiqueta = "sin nombre; anterior %s+0x%X" % (nombres[j - 1][0], inicio - direcciones[j - 1])
        print("  0x%-8X  funcion 0x%X-0x%X (tamano 0x%X) +0x%X  %s"
              % (valor, inicio, fin, fin - inicio, valor - inicio, etiqueta))


if __name__ == "__main__":
    main()
