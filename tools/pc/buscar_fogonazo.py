"""Busca el parpadeo LOCAL: una zona que salta y vuelve, no un cambio de todo el cuadro.

Uso: python tools/pc/buscar_fogonazo.py [carpeta] [--umbral 24] [--tope 12]

El primer intento (buscar_parpadeo.py) encuentra los fotogramas que mas se diferencian de sus vecinos, y lo
que encuentra asi son cambios de TODO el cuadro: cortes de camara, pantallas de carga, el menu. Eso no es el
artefacto: el artefacto es una zona concreta - los triangulos grandes que tapan la escena un instante - que
salta y vuelve mientras el resto del cuadro sigue igual.

Lo que mide este: para cada fotograma, la mascara de pixeles que se separan de los DOS vecinos a la vez
(entrada Y salida) y el tamano del mayor trozo CONEXO de esa mascara. Un pedazo compacto y grande es el
artefacto; la mascara entera es un corte de camara.
"""

import argparse
import os
import re
import sys

import numpy as np
from PIL import Image


def por_orden_de_tiempo(carpeta):
    nombres = [n for n in os.listdir(carpeta) if n.startswith('captura_') and n.endswith('.png')]
    return sorted(nombres, key=lambda n: int(re.search(r'_(\d+)ms\.png$', n).group(1)))


def mayor_trozo(mascara, bloque=4):
    """Area del mayor grupo conexo de la mascara, en pixeles, sobre una rejilla de `bloque`."""
    filas, columnas = mascara.shape[0] // bloque, mascara.shape[1] // bloque
    rejilla = np.zeros((filas, columnas), dtype=bool)
    for f in range(filas):
        for c in range(columnas):
            rejilla[f, c] = mascara[f * bloque:(f + 1) * bloque, c * bloque:(c + 1) * bloque].mean() > 0.25
    visto = np.zeros_like(rejilla)
    mejor = 0
    mejor_caja = None
    for f in range(filas):
        for c in range(columnas):
            if not rejilla[f, c] or visto[f, c]:
                continue
            pila = [(f, c)]
            visto[f, c] = True
            n = 0
            f0 = f1 = f
            c0 = c1 = c
            while pila:
                y, x = pila.pop()
                n += 1
                f0, f1 = min(f0, y), max(f1, y)
                c0, c1 = min(c0, x), max(c1, x)
                for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    yy, xx = y + dy, x + dx
                    if 0 <= yy < filas and 0 <= xx < columnas and rejilla[yy, xx] and not visto[yy, xx]:
                        visto[yy, xx] = True
                        pila.append((yy, xx))
            if n > mejor:
                mejor = n
                mejor_caja = (c0 * bloque, f0 * bloque, (c1 + 1) * bloque, (f1 + 1) * bloque)
    return mejor * bloque * bloque, mejor_caja


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('carpeta', nargs='?',
                    default=os.path.join(os.environ.get('LOCALAPPDATA', ''), 'cod2-nx', 'pc', 'capturas'))
    ap.add_argument('--umbral', type=int, default=24)
    ap.add_argument('--tope', type=int, default=12)
    # The user remembers roughly when it happened, and a session is 16,000 images: searching the whole thing
    # for every question is minutes of PNG decoding. The millisecond filter makes the common case fast.
    ap.add_argument('--desde_ms', type=int, default=0)
    ap.add_argument('--hasta_ms', type=int, default=1 << 40)
    args = ap.parse_args()

    carpeta = args.carpeta
    nombres = por_orden_de_tiempo(carpeta)
    nombres = [n for n in nombres
               if args.desde_ms <= int(re.search(r'_(\d+)ms\.png$', n).group(1)) <= args.hasta_ms]
    if len(nombres) < 5:
        print('solo hay %d capturas en ese tramo de %s' % (len(nombres), carpeta))
        return 1
    print('%d capturas en %s (de %d ms a %d ms)' %
          (len(nombres), carpeta, args.desde_ms, args.hasta_ms))

    imagenes = [np.asarray(Image.open(os.path.join(carpeta, n)).convert('L')).astype(np.int16)
                for n in nombres]
    forma = imagenes[0].shape
    total = forma[0] * forma[1]

    candidatos = []
    for i in range(1, len(imagenes) - 1):
        entrada = np.abs(imagenes[i] - imagenes[i - 1])
        salida = np.abs(imagenes[i + 1] - imagenes[i])
        mascara = np.minimum(entrada, salida) > args.umbral
        cuantos = int(mascara.sum())
        if cuantos * 100 < total:  # menos del 1 %: ruido de compresion o de sombra
            continue
        if cuantos * 100 > total * 60:  # mas del 60 %: cambio de camara o pantalla, no artefacto
            continue
        area, caja = mayor_trozo(mascara)
        if area == 0:
            continue
        candidatos.append((area, i, cuantos, caja))

    if not candidatos:
        print('ninguna zona local salta y vuelve; o no hay artefacto en estas capturas o el umbral no vale')
        return 0

    candidatos.sort(key=lambda c: -c[0])
    print('\ncandidatos por tamano del mayor trozo que salta y vuelve:')
    escritos = 0
    for area, i, cuantos, caja in candidatos[:args.tope]:
        m = re.search(r'_(\d+)ms\.png$', nombres[i])
        print('  trozo %7d px (%.1f %% del cuadro; %d px cambiados en total)  #%-5d t=%8s ms  caja x %d..%d y %d..%d'
              % (area, 100.0 * area / total, cuantos, i, m.group(1) if m else '?', caja[0], caja[2], caja[1], caja[3]))
        tira = [imagenes[j] for j in (i - 1, i, i + 1)]
        alto, ancho = forma
        lienzo = np.zeros((alto * 3 * 3, ancho * 3), dtype=np.uint8)
        for k, t in enumerate(tira):
            grande = np.kron(t, np.ones((3, 3)))
            lienzo[k * alto * 3:(k + 1) * alto * 3, :] = np.clip(grande, 0, 255).astype(np.uint8)
        # marco la caja del artefacto
        for k in range(3):
            y0, y1 = k * alto * 3 + caja[1] * 3, k * alto * 3 + caja[3] * 3
            x0, x1 = caja[0] * 3, caja[2] * 3
            lienzo[y0:y1, x0:x0 + 2] = 255
            lienzo[y0:y1, x1 - 2:x1] = 255
            lienzo[y0:y0 + 2, x0:x1] = 255
            lienzo[y1 - 2:y1, x0:x1] = 255
        Image.fromarray(lienzo).save(os.path.join(carpeta, 'fogonazo_%04d.png' % (escritos + 1)))
        escritos += 1
    print('\n%d tiras escritas como fogonazo_NNNN.png (anterior | el dudoso | siguiente, con la caja marcada)'
          % escritos)
    return 0


if __name__ == '__main__':
    sys.exit(main())
