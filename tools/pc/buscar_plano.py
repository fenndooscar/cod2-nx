"""Encuentra el artefacto de triangulos planos: una zona PLANA que aparece en un fotograma y se va.

Uso:
    python tools/pc/buscar_plano.py [carpeta] [--desde_ms N] [--hasta_ms N] [--tope 12] [--prueba]

Por que existe (y en que se diferencia de buscar_parpadeo.py y buscar_fogonazo.py)
    El artefacto reportado es una poligono grande SIN TEXTURA que dura uno o dos fotogramas mientras el
    jugador o un PNJ se mueve. Los dos detectores anteriores fallan por razones distintas:

      * buscar_parpadeo.py: mide cuanto cambia el fotograma entero respecto a sus vecinos. Un corte de
        camara, una pantalla de carga o el menu cambian todo y ganan siempre; el artefacto, que tapa media
        pantalla y deja la otra media igual, pierde.
      * buscar_fogonazo.py: mira la mascara de pixeles que cambian respecto a AMBOS vecinos (entra y sale) y
        coge el mayor trozo conexo. Eso ya distingue un artefacto de un corte, pero sigue eligiendo lo mismo
        que un movimiento rapido de camara: al girar, media pantalla entra y sale y el trozo conexo es enorme.

    Lo que distingue de verdad al artefacto es el MATERIAL: es un poligono PLANO sin textura. Un giro de
    camara tambien cambia muchos pixeles, pero los pixeles que cambia conservan su textura (bordes, sombras,
    ruido de la pared). El triangulo del artefacto es una zona de color casi constante: su gradiente local es
    casi cero, y en el fotograma anterior y en el siguiente esa misma zona tenia textura.

    Asi que el estadistico es: pixeles que (a) se separan de AMBOS vecinos, y (b) en ESTE fotograma estan en
    una zona PLANO (gradiente local pequeno). El area de ese conjunto es lo que se ordena.

    Dos detalles que hacen el numero honesto:
      * La distancia se mide en RGB, no en gris: un poligono gris sobre una pared gris cambia poco en luma.
      * El gradiente se mide con el operador de Sobel sobre la luma, y el umbral se expresa en niveles por
        pixel. El interior de un triangulo plano da <1; una pared con nieve, ladrillos o ruido da 5-30.
"""
import argparse
import os
import re
import sys

import numpy as np
from PIL import Image

# Un pixel cuenta como "plano" si el modulo de su gradiente local, sumado en las cuatro direcciones, no
# pasa de esto. El interior de un poligono sin textura queda por debajo; una pared o el suelo, no.
PLANO = 6.0


def por_orden_de_tiempo(carpeta):
    nombres = [n for n in os.listdir(carpeta) if n.startswith('captura_') and n.endswith('.png')]
    return sorted(nombres, key=lambda n: int(re.search(r'_(\d+)ms\.png$', n).group(1)))


def gradiente(lum):
    """Modulo del gradiente (Sobel), en niveles por pixel."""
    gx = np.zeros_like(lum, dtype=np.float32)
    gy = np.zeros_like(lum, dtype=np.float32)
    gx[:, 1:-1] = lum[:, 2:] - lum[:, :-2]
    gy[1:-1, :] = lum[2:, :] - lum[:-2, :]
    return np.sqrt(gx * gx + gy * gy)


def cargar(ruta):
    return np.asarray(Image.open(ruta).convert('RGB')).astype(np.int16)


def analizar(imagenes, nombres, umbral, tope, carpeta):
    """Devuelve los candidatos ordenados por area de zona plana que entra y sale."""
    candidatos = []
    total = imagenes[0].shape[0] * imagenes[0].shape[1]
    for i in range(1, len(imagenes) - 1):
        a, b, c = imagenes[i - 1], imagenes[i], imagenes[i + 1]
        # La luminancia GLOBAL tiene que continuar la de los vecinos: un fundido a negro, un corte de camara o
        # una pantalla de carga cambian el cuadro entero y ademas lo dejan PLANO, asi que con esto solo ganaban
        # ellos. Medido en la sesion de 12.790 capturas: los diez primeros candidatos tenian media 15-20 contra
        # 61 de sus vecinos, es decir eran fotogramas negros, no el artefacto.
        mb, ma, mc = float(b.mean()), float(a.mean()), float(c.mean())
        if abs(mb - ma) > 12.0 or abs(mb - mc) > 12.0:
            continue
        # (a) se separa de AMBOS vecinos: la imagen salta hacia este fotograma y vuelve al siguiente.
        dif = np.minimum(np.abs(b - a).max(axis=2), np.abs(c - b).max(axis=2))
        cambio = dif > umbral
        if cambio.sum() * 100 < total:  # menos del 1 %: ruido de compresion
            continue
        # NO se limita por arriba el numero de pixeles cambiados: el artefacto tapa mas de la mitad del cuadro
        # (medido: 61 % en la sesion) y un limite del 45 % lo descartaba junto con los fotogramas negros. Lo
        # que separa un fotograma negro de un artefacto es la luminancia global, que se mira arriba.
        # (b) y en ESTE fotograma la zona es PLANA: sin textura. Es lo que separa el artefacto de un giro.
        lum = b.mean(axis=2)
        plano = gradiente(lum) < PLANO
        zona = cambio & plano
        area = int(zona.sum())
        # El limite inferior es lo que hace que la lista sea util, no el umbral de color: con textura fuerte y
        # una camara girando, el remuestreo de las esquinas deja cientos de pixeles sueltos que cumplen (a) y
        # (b) a la vez. Miden decenas de pixeles; el artefacto mide miles. Medido en la autocomprobacion: un
        # giro sin artefacto da como mucho 36 px de 14.400 (0,25 %).
        if area * 100 < total:  # menos del 1 % del cuadro: no es el artefacto
            continue
        if area * 100 > total * 70:  # mas del 70 %: un corte, o el artefacto tapando todo
            continue
        candidatos.append((area, i, int(cambio.sum()), float(dif[cambio].mean()) if cambio.any() else 0.0))
    candidatos.sort(key=lambda c: -c[0])
    print('\ncandidatos (area plana que entra y sale, indice, pixeles cambiados, salto medio):')
    escritos = 0
    for area, i, cuantos, salto in candidatos[:tope]:
        m = re.search(r'_(\d+)ms\.png$', nombres[i])
        print('  plano %7d px (%.1f %% del cuadro; %d px cambiados, salto medio %.1f)  #%-5d t=%8s ms'
              % (area, 100.0 * area / total, cuantos, salto, i, m.group(1) if m else '?'))
        tira = [imagenes[j] for j in (i - 1, i, i + 1)]
        alto, ancho = imagenes[0].shape[:2]
        lienzo = np.zeros((alto * 3 * 3, ancho * 3, 3), dtype=np.uint8)
        for k, t in enumerate(tira):
            grande = np.kron(t.astype(np.float32), np.ones((3, 3, 1), dtype=np.float32))
            lienzo[k * alto * 3:(k + 1) * alto * 3, :, :] = np.clip(grande, 0, 255).astype(np.uint8)
        # La mascara de zona plana, en rojo sobre el fotograma central.
        centro = lienzo[alto * 3:alto * 6, :, :]
        mascara = np.zeros((alto, ancho), dtype=bool)
        a, b, c = imagenes[i - 1], imagenes[i], imagenes[i + 1]
        dif = np.minimum(np.abs(b - a).max(axis=2), np.abs(c - b).max(axis=2))
        mascara = (dif > umbral) & (gradiente(b.mean(axis=2)) < PLANO)
        grande = np.kron(mascara, np.ones((3, 3), dtype=bool))
        centro[grande] = [255, 0, 0]
        Image.fromarray(lienzo).save(os.path.join(carpeta, 'plano_%04d.png' % (escritos + 1)))
        escritos += 1
    if not escritos:
        print('  ninguno: ningun fotograma tiene una zona plana que entre y salga')
    else:
        print('\n%d tiras como plano_NNNN.png (anterior | el dudoso | siguiente; la zona plana en rojo)' % escritos)
    return candidatos


def autocomprobacion():
    """Una secuencia sintetica con un triangulo plano en un fotograma conocido.

    Un detector de esto se rompe en silencio: un umbral que nadie puede justificar, una comparacion con el
    signo cambiado, una mascara que se queda vacia. Esta prueba construye una escena CON TEXTURA que se mueve
    (una camara girando) mas un fotograma con un poligono plano encima, y comprueba que se encuentra ese y no
    otro. Ademas comprueba que un giro de camara SIN artefacto no da candidatos.
    """
    import tempfile
    alto, ancho = 90, 160
    ruido = np.random.default_rng(7)
    base = np.zeros((alto, ancho), dtype=np.int16)
    # Escena con textura fuerte: bloques de damero + ruido. Asi "cambia mucho" NO basta para ser candidato.
    for y in range(alto):
        for x in range(ancho):
            base[y, x] = 40 + ((x // 4 + y // 4) % 2) * 120 + int(ruido.integers(0, 40))
    fallos = []

    def escena(desplazamiento):
        return np.roll(base, desplazamiento, axis=1)

    with tempfile.TemporaryDirectory() as tmp:
        carpeta = os.path.join(tmp, 'capturas')
        os.makedirs(carpeta)
        # El valor del parche plano se elige igual a la media del fondo (120): asi cambia la TEXTURA sin
        # cambiar el brillo del cuadro, que es lo que hace el artefacto de verdad y lo que lo distingue de un
        # fundido a negro o de un corte de camara.
        imagenes = []
        for n in range(40):
            t = escena(n)
            if n == 20:
                t = t.copy()
                t[10:80, 10:150] = 120
            rgb = np.stack([t, t, t], axis=2).astype(np.int16)
            imagenes.append(rgb)
            Image.fromarray(rgb.astype(np.uint8)).save(
                os.path.join(carpeta, 'captura_%05d_%06dms.png' % (n, n * 250)))
        nombres = por_orden_de_tiempo(carpeta)
        cand = analizar(imagenes, nombres, 24, 3, tmp)
        if not cand:
            fallos.append('no se encontro nada en una secuencia con el artefacto')
        else:
            elegido = cand[0][1]
            if elegido != 20:
                fallos.append('el artefacto esta en el 20 y se eligio el %d' % elegido)
            else:
                print('autocomprobacion: el fotograma sintetico 20 se encuentra primero '
                      '(area %d px, %.1f %% del cuadro)' % (cand[0][0], 100.0 * cand[0][0] / (alto * ancho)))
        # Sin artefacto, solo un giro continuo: no debe haber candidatos.
        sin = [np.stack([escena(n)] * 3, axis=2).astype(np.int16) for n in range(40)]
        cand_sin = analizar(sin, nombres, 24, 3, tmp)
        if cand_sin:
            fallos.append('un giro sin artefacto dio %d candidatos (el mayor %d px)'
                          % (len(cand_sin), cand_sin[0][0]))
        else:
            print('autocomprobacion: un giro de camara sin artefacto no da ningun candidato')
        # Y un fundido a negro de un fotograma tampoco es el artefacto: es plano y cambia el cuadro entero,
        # que es exactamente lo que ganaba la lista antes de mirar la luminancia global.
        negro = []
        for n in range(40):
            t = escena(n)
            rgb = np.stack([t, t, t], axis=2).astype(np.int16)
            if n == 20:
                rgb = np.zeros_like(rgb)  # un fotograma en negro
            negro.append(rgb)
        cand_negro = analizar(negro, nombres, 24, 3, tmp)
        if cand_negro:
            fallos.append('un fotograma en negro dio %d candidatos (el mayor %d px)'
                          % (len(cand_negro), cand_negro[0][0]))
        else:
            print('autocomprobacion: un fotograma en negro no es un candidato')
    for f in fallos:
        print('FALLO: %s' % f)
    return 1 if fallos else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('carpeta', nargs='?',
                    default=os.path.join(os.environ.get('LOCALAPPDATA', ''), 'cod2-nx', 'pc', 'capturas'))
    ap.add_argument('--umbral', type=int, default=24, help='niveles de diferencia para contar un pixel')
    ap.add_argument('--tope', type=int, default=12)
    ap.add_argument('--desde_ms', type=int, default=0)
    ap.add_argument('--hasta_ms', type=int, default=1 << 40)
    ap.add_argument('--prueba', action='store_true')
    args = ap.parse_args()

    if args.prueba:
        return autocomprobacion()

    carpeta = args.carpeta
    nombres = [n for n in por_orden_de_tiempo(carpeta)
               if args.desde_ms <= int(re.search(r'_(\d+)ms\.png$', n).group(1)) <= args.hasta_ms]
    if len(nombres) < 5:
        print('solo hay %d capturas en ese tramo de %s' % (len(nombres), carpeta))
        return 1
    print('%d capturas en %s' % (len(nombres), carpeta))
    imagenes = [cargar(os.path.join(carpeta, n)) for n in nombres]
    if any(i.shape != imagenes[0].shape for i in imagenes):
        print('las capturas no son todas del mismo tamano; usa una carpeta limpia')
        return 1
    analizar(imagenes, nombres, args.umbral, args.tope, carpeta)
    return 0


if __name__ == '__main__':
    sys.exit(main())
