# cod2 - traduce un volcado de pilas (logs/pilas_N.txt) a nombres de funcion del juego.
#
# Por que existe
#   El vigilante de paradas (app/src/cod2_perfil_pc.cpp) escribe, por cada hilo, la lista de direcciones
#   de retorno del host. Leidas en crudo no dicen nada: hay que cruzarlas con logs/simbolos.csv, que es
#   el mapa direccion de host -> sub_XXXXXXXX del codigo recompilado que escribe cod2_pc_simbolos.cpp.
#
# Uso
#   python tools/pc/pilas.py                       # el volcado mas reciente de app/out/pc/logs
#   python tools/pc/pilas.py logs/pilas_2.txt
#   python tools/pc/pilas.py --hilo 45856          # solo ese hilo del sistema
#   python tools/pc/pilas.py --base 0x7FF7A3DC0000 # si el volcado no trae la base del modulo
#
# El volcado empieza con las lineas '#modulo;...' que dan la base de cada modulo, asi que --base solo
# hace falta para volcados viejos o recortados.

import argparse
import bisect
import os
import sys


def leer_simbolos(ruta):
    direcciones, nombres = [], []
    with open(ruta, encoding="utf-8", errors="ignore") as f:
        for linea in f:
            if linea.startswith("#"):
                continue
            campos = linea.rstrip("\n").split(";")
            if len(campos) < 2:
                continue
            direcciones.append(int(campos[0], 16))
            nombres.append(campos[1])
    orden = sorted(range(len(direcciones)), key=lambda i: direcciones[i])
    return [direcciones[i] for i in orden], [nombres[i] for i in orden]


def resolver(direcciones, nombres, valor):
    i = bisect.bisect_right(direcciones, valor) - 1
    if i < 0:
        return None
    return "%s+0x%X" % (nombres[i], valor - direcciones[i])


def main():
    raiz = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    ap = argparse.ArgumentParser(description="Traduce un volcado de pilas a funciones del juego.")
    ap.add_argument("volcado", nargs="?", default=None, help="logs/pilas_N.txt (por defecto, el ultimo)")
    ap.add_argument("--csv", default=os.path.join(raiz, "app", "out", "pc", "logs", "simbolos.csv"))
    ap.add_argument("--hilo", default=None, help="id del hilo del sistema a mostrar")
    ap.add_argument("--base", default=None, help="base de cod2.exe si el volcado no la trae")
    ap.add_argument("--todos", action="store_true", help="muestra tambien los marcos de fuera del juego")
    args = ap.parse_args()

    if args.volcado is None:
        carpeta = os.path.join(raiz, "app", "out", "pc", "logs")
        candidatos = sorted(
            (f for f in os.listdir(carpeta) if f.startswith("pilas_") and f.endswith(".txt")),
            key=lambda f: os.path.getmtime(os.path.join(carpeta, f)),
        )
        if not candidatos:
            sys.exit("no hay volcados en %s" % carpeta)
        args.volcado = os.path.join(carpeta, candidatos[-1])

    if not os.path.exists(args.csv):
        sys.exit("falta %s: ejecuta el juego con cod2_pc_simbolos activado" % args.csv)
    direcciones, nombres = leer_simbolos(args.csv)

    base = int(args.base, 16) if args.base else None
    cabecera = None
    hilo = None
    marcos = []

    def volcar():
        if hilo is None:
            return
        if args.hilo and hilo[0] != args.hilo:
            return
        print("hilo %s%s" % (hilo[0], " (%s)" % hilo[1] if hilo[1] else ""))
        for profundidad, valor in marcos:
            nombre = resolver(direcciones, nombres, valor)
            if nombre is None and not args.todos:
                continue
            print("  %2d  0x%08X  %s" % (profundidad, valor - (base or 0), nombre or "fuera del juego"))

    with open(args.volcado, encoding="utf-8", errors="ignore") as f:
        for linea in f:
            linea = linea.rstrip("\n")
            if linea.startswith("#modulo;"):
                campos = linea.split(";")
                if len(campos) >= 4 and campos[2].lower() == "cod2.exe":
                    base = int(campos[3], 16)
                continue
            if linea.startswith("#volcado;"):
                cabecera = linea
                continue
            if linea.startswith("#hilo;"):
                volcar()
                campos = linea.split(";")
                hilo = (campos[1], campos[2] if len(campos) > 2 else "")
                marcos = []
                continue
            campos = linea.split(";")
            if len(campos) == 4 and hilo is not None:
                marcos.append((int(campos[1]), base + int(campos[3], 16) if base else int(campos[3], 16)))
    volcar()

    if cabecera:
        print("\n%s" % cabecera)


if __name__ == "__main__":
    main()
