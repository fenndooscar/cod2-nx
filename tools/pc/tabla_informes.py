"""Correlaciona los informes de 10 s del log del renderizador nativo en una tabla.

Uso: python tools/pc/tabla_informes.py [ruta_del_log]

Para que: el usuario reporta que el FPS cae al mover la camara en vertical. El
log ya tiene un informe cada 10 s con la distribucion de intervalos entre Swaps
y otro con el coste del anillo. Ponerlos en la misma fila es lo que dice si la
caida coincide con las copias de vertices, con un pase de mas, o con nada de eso.
"""
import re
import sys
import os
from collections import defaultdict

BALDES = ['<15', '15-18', '18-25', '25-30', '30-36', '36-50', '50-60', '60-75', '75-100', '100-150', '>=150']


def parsear(ruta):
    filas = defaultdict(dict)
    with open(ruta, 'r', encoding='utf-8', errors='replace') as f:
        for linea in f:
            m = re.match(r'\[[\d-]+ (\d\d:\d\d):(\d\d)\.\d+\]', linea)
            if not m:
                continue
            # Los informes de un mismo intervalo no caen en el mismo segundo (el de copias lo escribe el hilo
            # de copias). Se agrupan por decena: es el intervalo que el propio log usa.
            t = '%s:%02d' % (m.group(1), int(m.group(2)) // 10 * 10)
            if 'C2: intervalos entre Swaps' in linea:
                nums = re.findall(r'(\d+)', linea.split('Swaps:')[1].split(';')[0])
                # los primeros 11 numeros son los baldes; el resto son <15/15-18... no: son 11 numeros
                if len(nums) >= 11:
                    filas[t]['int'] = [int(x) for x in nums[:11]]
                m2 = re.search(r'peor fotograma ([\d.]+) ms', linea)
                if m2:
                    filas[t]['peor'] = float(m2.group(1))
            elif 'C6 copias de vertices (build' in linea:
                m2 = re.search(r'^.*ultimos 10 s: (\d+) encoladas \(([\d.]+) MB\).*?el anillo ayudo con (\d+) \(([\d.]+) MB, ([\d.]+) ms\) en (\d+) de (\d+) esperas', linea)
                if m2:
                    filas[t]['copias'] = int(m2.group(1))
                    filas[t]['copias_mb'] = float(m2.group(2))
                    filas[t]['anillo_ms'] = float(m2.group(5))
                    filas[t]['esperas_con'] = int(m2.group(6))
                    filas[t]['esperas'] = int(m2.group(7))
            elif 'C2: GPU por Swap' in linea:
                m2 = re.search(r'hueco entre trabajos ([\d.]+)', linea)
                if m2:
                    filas[t]['hueco'] = float(m2.group(1))
                m3 = re.search(r'escena ([\d.]+),', linea)
                if m3:
                    filas[t]['gpu_escena'] = float(m3.group(1))
                m4 = re.search(r'menores ([\d.]+)', linea)
                if m4:
                    filas[t]['gpu_menores'] = float(m4.group(1))
            elif 'C6 copia de vertices con huella' in linea:
                m2 = re.search(r'(\d+) copias comprobadas, (\d+) con los bytes cambiados.*?([\d.]+) ms', linea)
                if m2:
                    filas[t]['huella_ms'] = float(m2.group(3))
            elif 'C6 etapas' in linea:
                filas[t]['etapas'] = linea.split('[nativo]')[-1].strip()[:150]
    return filas


def main():
    ruta = sys.argv[1] if len(sys.argv) > 1 else None
    if not ruta:
        d = os.path.join(os.environ.get('LOCALAPPDATA', ''), 'cod2-nx', 'pc', 'logs')
        cands = [os.path.join(d, n) for n in os.listdir(d) if n.startswith('cod2_') and n.endswith('.log')]
        ruta = max(cands, key=os.path.getmtime)
    print('# %s' % ruta)
    filas = parsear(ruta)
    print('%8s %6s %6s %7s %8s %7s %8s %8s %8s' % ('hora', '30-36', '36-50', 'copias', 'MB', 'anillo', 'esperas', 'hueco', 'peor'))
    for t in sorted(filas):
        f = filas[t]
        if 'int' not in f:
            continue
        i = f['int']
        lentos = i[4] + i[5]
        if lentos < 5 and 'copias' not in f:
            continue
        print('%8s %6d %6d %7s %8s %7s %8s %8s %8s' % (
            t, i[4], i[5], f.get('copias', '-'), f.get('copias_mb', '-'), f.get('anillo_ms', '-'),
            f.get('esperas', '-'), f.get('hueco', '-'), f.get('peor', '-')))


if __name__ == '__main__':
    main()
