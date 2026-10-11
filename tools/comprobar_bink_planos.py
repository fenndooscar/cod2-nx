#!/usr/bin/env python3
"""cod2 - compares the port's native Bink output against the game's own Bink decoder, surface by surface

The failure this exists for
    Replacing the guest's Bink decoder with FFmpeg only works if the native decode lands in the same surface, with the
    same strides, as the one the game's own decoder wrote. A wrong offset there does not crash: it gives a picture
    shifted by a row, or a black frame, or - if the two chroma planes cross over - a mirrored hue, which on a game
    this warm reads as "the colours have gone blue". All three look like "the video plays".

What it compares
    Two dumps of the same frames of the same movie, both taken from the game itself by the same code
    (`app/src/cod2_video_bink_gancho.cpp`, cvar `cod2_video_bink_planos`, written to `logs/bink/`):

        the game with --no-cod2_video_bink_nativo   <- the reference: the guest's own decoder
        the game with the default (native on)       <- what this port writes

    Comparing two dumps of the game is what makes this a real check: a reference decoded from the file by
    `cod2_video_bink_prueba --volcar` would only restate whatever plane order the port already assumes.

    Each side is indexed by dump order, not by the movie's own frame number, so the two runs have to be started with
    the same `--cod2_video_bink_volcar_desde`; the log line for each dump names the movie frame, which is how the two
    runs are lined up. The default of 0 is the wrong choice for a chroma check: the first eight frames of every one of
    these nine movies are a colourless fade from black, and a comparison taken there passes whatever order the planes
    are written in - which is how the crossed chroma survived the first version of this check. `atvi.bik` has real
    colour from frame 300 on, so the run that found it was:

        --cod2_video_bink_planos=true --cod2_video_bink_volcar_desde=300

    and then, for the picture itself, `sonda_bink`-style FFmpeg planes can be cross-checked against the game's own:
    FFmpeg's `data[1]` (Cb) comes out equal to the game's *second* surface and its `data[2]` (Cr) to the game's
    *first*, because Bink's bitstream is Y, V, U and FFmpeg's `AVFrame` has to be standard YUV420P.

Usage
    python tools/comprobar_bink_planos.py <carpeta_del_nativo> <carpeta_del_invitado> [fotogramas] [ancho] [alto]
"""
import os
import sys

PLANOS = 3


def leer(ruta):
    with open(ruta, "rb") as f:
        return f.read()


def main(argv):
    if len(argv) not in (3, 4, 5, 6):
        print(__doc__)
        return 2
    nativo, invitado = argv[1], argv[2]
    fotogramas = int(argv[3]) if len(argv) > 3 else 8
    ancho = int(argv[4]) if len(argv) > 4 else 1280
    alto = int(argv[5]) if len(argv) > 5 else 720

    fallos = 0
    for n in range(fotogramas):
        for p in range(PLANOS):
            pw = ancho if p == 0 else (ancho + 1) // 2
            ph = alto if p == 0 else (alto + 1) // 2
            ruta_nat = os.path.join(nativo, "fotograma_%02d_plano%d.bin" % (n, p))
            ruta_inv = os.path.join(invitado, "fotograma_%02d_plano%d.bin" % (n, p))
            if not os.path.exists(ruta_nat) or not os.path.exists(ruta_inv):
                print("faltan ficheros para el fotograma %d plano %d" % (n, p))
                return 2
            datos = leer(ruta_nat)
            ref = leer(ruta_inv)
            if len(datos) != len(ref):
                print("fotograma %d plano %d: el nativo mide %d y el invitado %d"
                      % (n, p, len(datos), len(ref)))
                fallos += 1
                continue
            if len(datos) % ph:
                print("fotograma %d plano %d: %d bytes no es multiplo de %d filas" % (n, p, len(datos), ph))
                fallos += 1
                continue
            paso = len(datos) // ph
            distintas = 0
            peor = 0
            for y in range(ph):
                a = datos[y * paso:y * paso + pw]
                b = ref[y * paso:y * paso + pw]
                if a != b:
                    distintas += 1
                    peor = max(peor, max(abs(x - z) for x, z in zip(a, b)))
            if distintas:
                print("fotograma %d plano %d: %d de %d filas distintas, diferencia maxima %d (paso %d)"
                      % (n, p, distintas, ph, peor, paso))
                fallos += 1
            else:
                print("fotograma %d plano %d: %d/%d filas iguales (paso %d)" % (n, p, ph, ph, paso))

    if fallos:
        print("FALLA: %d plano(s) no coinciden" % fallos)
        return 1
    print("OK: los %d fotogramas y sus %d planos coinciden con el decodificador del propio juego" % (fotogramas, PLANOS))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
