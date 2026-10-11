#!/usr/bin/env python3
"""cod2 - compares the console's own Bink dump against the native decoder's

The failure this exists for
    Replacing the guest's Bink decoder with FFmpeg only works if the native decode lands in the same surface, with the
    same strides, as the one the game's own decoder wrote. A wrong offset there does not crash: it gives a plausible
    picture shifted by a row, or a black frame, and both look like "the video plays".

What it compares
    The console writes `fotograma_NN_planoP.bin` (cvar `cod2_video_bink_planos`), which is the surface as the decoder
    left it, stride and padding included. The reference is `nuestro_NN_planoP.bin`, written by the same decoder run
    off the file by `cod2_video_bink_prueba --volcar` and tightly packed.

    So this does not re-test the decoder - tools/comprobar_bink.ps1 does that. It tests the join: the frame
    descriptor's offsets, the stride, and which of the two double-buffered surfaces a frame went into.

Usage
    python tools/comprobar_bink_planos.py <carpeta_del_invitado> <carpeta_de_referencia> [ancho] [alto]
"""
import os
import sys

PLANOS = 3


def leer(ruta):
    with open(ruta, "rb") as f:
        return f.read()


def main(argv):
    if len(argv) not in (3, 4, 5):
        print(__doc__)
        return 2
    invitado, referencia = argv[1], argv[2]
    ancho = int(argv[3]) if len(argv) > 3 else 1280
    alto = int(argv[4]) if len(argv) > 4 else 720

    fallos = 0
    for n in range(8):
        for p in range(PLANOS):
            pw = ancho if p == 0 else (ancho + 1) // 2
            ph = alto if p == 0 else (alto + 1) // 2
            ruta_inv = os.path.join(invitado, "fotograma_%02d_plano%d.bin" % (n, p))
            ruta_ref = os.path.join(referencia, "nuestro_%02d_plano%d.bin" % (n, p))
            if not os.path.exists(ruta_inv) or not os.path.exists(ruta_ref):
                print("faltan ficheros para el fotograma %d plano %d" % (n, p))
                return 2
            datos = leer(ruta_inv)
            ref = leer(ruta_ref)
            if len(ref) != pw * ph:
                print("la referencia del plano %d del fotograma %d mide %d, se esperaban %d"
                      % (p, n, len(ref), pw * ph))
                return 2
            if len(datos) % ph:
                print("el volcado del invitado mide %d, que no es multiplo de %d filas" % (len(datos), ph))
                return 2
            paso = len(datos) // ph
            distintas = 0
            for y in range(ph):
                fila = datos[y * paso:y * paso + pw]
                if fila != ref[y * pw:(y + 1) * pw]:
                    distintas += 1
            if distintas:
                print("fotograma %d plano %d: %d de %d filas distintas (paso del invitado %d, ancho %d)"
                      % (n, p, distintas, ph, paso, pw))
                fallos += 1
            else:
                print("fotograma %d plano %d: %d/%d filas iguales (paso del invitado %d)"
                      % (n, p, ph, ph, paso))

    if fallos:
        print("FALLA: %d plano(s) no coinciden" % fallos)
        return 1
    print("OK: los 8 fotogramas y sus 3 planos coinciden con la referencia")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
