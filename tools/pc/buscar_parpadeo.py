"""Finds the frames of a captured run that differ sharply from their neighbours.

For an artefact that only shows for a few frames while the player or an NPC moves, and
for a report of the kind "it flickered when I walked past the burnt-out truck", the run
has to be recorded whole and then searched.

    powershell -File tools\\pc\\jugar.ps1 -Partida -Extra '--cod2_captura_cada_ms=250,--cod2_captura_max=4000,--cod2_captura_escala=4'

leaves a few thousand 320x180 PNGs in %LOCALAPPDATA%\\cod2-nx\\pc\\capturas. This reads
them in name order (which is time order, the name carries the milliseconds) and, for each
one, measures how far it is from the one before it and from the one after it:

  * moving the camera or an NPC makes EVERY frame differ from its neighbours, by an amount
    that grows with the speed of the movement;
  * a flicker is a frame that differs from both of its neighbours far more than they differ
    from each other - the picture jumps away and comes back.

So the number that matters is not "how different is this frame" but the ratio between this
frame's difference and its neighbours' difference: `salto`. A frame whose salto is several
times the median of the run is a candidate, and the tool writes the worst ones (and their
neighbours) as a strip so they can be looked at.

    python tools/pc/buscar_parpadeo.py [carpeta] [--tope 20]

Writes into the same folder: `parpadeo_NNNN.png` (the suspect frame and its two
neighbours, side by side) and prints the ranked list with the millisecond of each one.
"""

import argparse
import os
import re
import sys

import numpy as np
from PIL import Image


def por_orden_de_tiempo(carpeta):
    """The captures, in time order. The name carries the milliseconds since start."""
    nombres = [n for n in os.listdir(carpeta) if n.startswith('captura_') and n.endswith('.png')]
    def clave(n):
        m = re.search(r'_(\d+)ms\.png$', n)
        return int(m.group(1)) if m else 0
    return sorted(nombres, key=clave)


def gris(ruta):
    return np.asarray(Image.open(ruta).convert('L')).astype(np.int16)


def autocomprobacion():
    """One synthetic sequence with a flicker at a known place.

    A detector for "which frame is the odd one" is the kind of thing that quietly stops working (a threshold
    nobody can justify, a sign flipped in the difference). This makes a sequence of 30 frames where frame 17 is
    the odd one and says whether it is found.
    """
    import tempfile
    # A gradient, not a flat colour: with a flat image the run's median change is 0 and the ratio has no
    # meaning (which is also true of a real run in which the camera never moves - with a static camera there is
    # nothing to flicker against).
    x = np.arange(48, dtype=np.int16)[None, :]
    y = np.arange(32, dtype=np.int16)[:, None]
    plana = (60 + x * 2 + y).astype(np.int16)
    with tempfile.TemporaryDirectory() as tmp:
        carpeta = os.path.join(tmp, 'capturas')
        os.makedirs(carpeta)
        for n in range(30):
            t = np.roll(plana, n, axis=1)  # movimiento continuo: todos los fotogramas cambian
            if n == 17:
                t = (255 - t).astype(np.int16)  # el parpadeo: el fotograma se invierte
            Image.fromarray(t.astype(np.uint8)).save(
                os.path.join(carpeta, 'captura_%05d_%06dms.png' % (n, n * 250)))
        nombres = por_orden_de_tiempo(carpeta)
        assert len(nombres) == 30, nombres
        imagenes = [gris(os.path.join(carpeta, n)) for n in nombres]
        dif = [float(np.abs(imagenes[i].astype(np.int32) - imagenes[i - 1].astype(np.int32)).mean())
               for i in range(1, len(imagenes))]
        orden = sorted(dif)
        base = max(1e-6, orden[len(orden) // 2])
        mejor, salto_mejor = -1, 0.0
        for i in range(1, len(imagenes) - 1):
            salto = min(dif[i - 1], dif[i]) / base
            if salto > salto_mejor:
                mejor, salto_mejor = i, salto
        assert mejor == 17, 'el parpadeo sintetico esta en 17 y se encontro en %d' % mejor
        print('autocomprobacion: el fotograma sintetico 17 se encuentra (salto %.1f)' % salto_mejor)
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('carpeta', nargs='?',
                    default=os.path.join(os.environ.get('LOCALAPPDATA', ''),
                                         'cod2-nx', 'pc', 'capturas'))
    ap.add_argument('--tope', type=int, default=20, help='cuantos candidatos escribir como tira')
    ap.add_argument('--prueba', action='store_true', help='autocomprobacion con fotogramas sinteticos')
    args = ap.parse_args()

    if args.prueba:
        return autocomprobacion()

    carpeta = args.carpeta
    if not os.path.isdir(carpeta):
        print('no existe la carpeta %s' % carpeta)
        return 1
    nombres = por_orden_de_tiempo(carpeta)
    if len(nombres) < 5:
        print('solo hay %d capturas en %s: nada que comparar' % (len(nombres), carpeta))
        return 1
    print('%d capturas en %s' % (len(nombres), carpeta))

    imagenes = [gris(os.path.join(carpeta, n)) for n in nombres]
    forma = imagenes[0].shape
    if any(i.shape != forma for i in imagenes):
        print('las capturas no son todas del mismo tamano; usa una carpeta limpia')
        return 1

    # Difference between consecutive frames: the raw "how much did the picture change". dif[k] is
    # |frame k+1 - frame k|, so the last index is len(imagenes) - 2.
    dif = [float(np.abs(imagenes[i].astype(np.int32) - imagenes[i - 1].astype(np.int32)).mean())
           for i in range(1, len(imagenes))]
    # A flicker is a frame the picture jumps INTO and OUT OF: both differences around it are large. The
    # baseline is the run's own median change, which absorbs how fast the camera was moving. Taking the
    # minimum of the two is what separates a flicker from ordinary movement: movement makes both large only
    # for the frame it happens in, and then a slow frame brings one of them back down.
    orden = sorted(dif)
    base = max(1e-6, orden[len(orden) // 2])
    candidatos = []
    for i in range(1, len(imagenes) - 1):
        entrada, salida = dif[i - 1], dif[i]
        candidatos.append((min(entrada, salida) / base, i, entrada, salida))
    if not candidatos:
        print('sin suficientes capturas')
        return 1

    saltos = sorted(c[0] for c in candidatos)
    print('cambio mediano entre fotogramas: %.2f niveles  (p90 %.2f, peor %.2f)' %
          (base, saltos[int(len(saltos) * 0.9)], saltos[-1]))

    candidatos.sort(key=lambda c: -c[0])
    print('\ncandidatos (salto = min(entrada, salida) / mediana):')
    escritos = 0
    for salto, i, entrada, salida in candidatos:
        if salto < 2.0:
            break
        if escritos >= args.tope:
            break
        m = re.search(r'_(\d+)ms\.png$', nombres[i])
        print('  salto %6.2f  #%-5d t=%8s ms  (entrada %.2f, salida %.2f)' %
              (salto, i, m.group(1) if m else '?', entrada, salida))
        tira = [imagenes[j] for j in (i - 1, i, i + 1)]
        alto, ancho = forma
        lienzo = np.zeros((alto, ancho * 3 + 8), dtype=np.uint8)
        for k, t in enumerate(tira):
            lienzo[:, k * (ancho + 4):k * (ancho + 4) + ancho] = np.clip(t, 0, 255).astype(np.uint8)
        Image.fromarray(lienzo).save(os.path.join(carpeta, 'parpadeo_%04d.png' % (escritos + 1)))
        escritos += 1
    if not escritos:
        print('  ninguno: ningun fotograma salta mas de 2 veces la mediana de la partida')
    else:
        print('\n%d tiras escritas como parpadeo_NNNN.png (anterior | el dudoso | siguiente)' % escritos)
    return 0


if __name__ == '__main__':
    sys.exit(main())
