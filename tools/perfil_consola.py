"""Reads the console profiler log and says, per kind of scene, where the time goes.

Usage: python tools/perfil_consola.py <rex_perfil.log>

rex_perfil.log is written by the sampling profiler of the app (sdk/src/ui/switch_perf.cpp) to
<NRO folder>/logs/rex/. It has one report every 10 s, and each report is a block of lines starting with
"==== <seconds> s | modo: <mode> | juego <fps> fps ...".

The reports are grouped into the three scenes the port cares about, by the number of draws per frame:
a cutscene draws 1-4, the menu draws 5-100, the game draws more than 100. For each group it prints the
average of the numbers that say which resource is exhausted:

  fps            guest presents per second.
  CPU%           total CPU of the process, where 400% = 4 cores. The console gives the port 3.
  dib/fot        draws per frame.
  MB/fot         bytes uploaded to shared memory per frame, divided by 1024*1024.
  fallos/s       guest reads emulated by the exception handler. On Horizon a watched page cannot be
                 read-only, so it is unmapped and every read of it faults (see
                 sdk/src/core/exception_handler_switch.cpp, point 1 of the hot path).
  espera ms/s    milliseconds per second the renderer thread spends inside nvFenceWait, that is,
                 blocked on the GPU. Zero means the GPU is idle; near 1000 means it is the wall.
  MainXThread    CPU of the guest's main thread. 100% is one whole core.
  GPU Cmds       CPU of the renderer thread. Note this is CPU, not GPU use: a low figure with a high
                 "espera ms/s" is a GPU that is busy while its thread waits.
  2o XThread     the busiest other guest thread.

Reading it: the scene whose "espera ms/s" is near zero is not limited by the GPU, so lowering the
render resolution cannot help it. The scene with a saturated thread and a saturated GPU is limited by
both, and there the resolution does matter.

The "dibujados por ancho de superficie" line of each report also shows the width of the surfaces the
game draws to; 1280-1599 means the guest is rendering at 720p or more.
"""
import re
import sys

if len(sys.argv) != 2:
    raise SystemExit(__doc__)

ESCENAS = ((0, 4, "cinematica"), (4, 100, "menu"), (100, 1 << 30, "juego"))


def escena(dibujados):
    for lo, hi, nombre in ESCENAS:
        if lo <= dibujados < hi:
            return nombre
    return "arranque"


def bloques(texto):
    actual = []
    for linea in texto.split("\n"):
        if linea.startswith("==== "):
            if actual:
                yield actual
            actual = [linea]
        elif actual:
            actual.append(linea)
    if actual:
        yield actual


def numeros(bloque):
    t = "\n".join(bloque)
    m = re.match(r"^==== ([\d.]+) s \| modo: ([^|]+)\| juego ([\d.]+) fps \| CPU total (\d+)%", bloque[0])
    if not m:
        return None
    d = {"s": float(m.group(1)), "modo": m.group(2).strip(), "fps": float(m.group(3)),
         "cpu": int(m.group(4))}
    for clave, patron in (
        ("fallos", r"lectura emulada ([\d.]+)"),
        ("dib", r"por fotograma: ([\d.]+) dibujados"),
        ("mb", r"([\d.]+) MB de memoria compartida subida"),
        ("main", r'"Main XThread \(F": CPU ([\d.]+)%'),
        ("gpu", r'"GPU Commands \(F": CPU ([\d.]+)%'),
        ("espera", r"fences consultadas [\d.]+ \([\d.]+ ms\), esperadas [\d.]+ \(([\d.]+) ms\)"),
    ):
        f = re.search(patron, t)
        d[clave] = float(f.group(1)) if f else 0.0
    otros = [float(x) for x in re.findall(r'XThread[0-9A-F]+": CPU ([\d.]+)%', t)]
    d["otro"] = max(otros) if otros else 0.0
    anchos = re.search(r"dibujados por ancho de superficie: (.*)", t)
    if anchos:
        # "1600+ 0 | 1280-1599 11 | 640-1279 0 | 256-639 0 | menos de 256 0"
        d["anchos"] = [float(x) for x in re.findall(r"\|\s*[\w+ -]+\s+([\d.]+)", anchos.group(1))]
        primera = re.match(r"\s*1600\+\s+([\d.]+)", anchos.group(1))
        if primera:
            d["anchos"].insert(0, float(primera.group(1)))
    return d


def main():
    # La consola de Windows no siempre es UTF-8 (cp1251 en esta maquina) y el texto lleva tildes.
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    texto = open(sys.argv[1], encoding="utf-8", errors="ignore").read()
    if "Muestreo de pilas: activo" in texto:
        print("Muestreo de pilas: ACTIVO (hay atribucion por funcion; pasalo por tools/calientes.py)")
    else:
        print("Muestreo de pilas: desactivado (crea <logs>/rex/perfil_pilas.flag y repite para saber en QUE funcion)")
    print()

    total = {}
    informes = 0
    for bloque in bloques(texto):
        d = numeros(bloque)
        if d is None:
            continue
        informes += 1
        # Los informes sin dibujados son el arranque: no son ninguna de las tres escenas.
        if d["dib"] <= 0:
            continue
        e = total.setdefault(escena(d["dib"]), [])
        e.append(d)

    cabecera = ("escena", "n", "fps", "CPU%", "dib/fot", "MB/fot", "fallos/s", "espera ms/s",
                "MainXThread", "GPU Cmds", "2o XThread")
    print("%-11s %4s %8s %6s %8s %8s %10s %11s %11s %9s %10s" % cabecera)
    for nombre in ("menu", "juego", "cinematica"):
        v = total.get(nombre)
        if not v:
            continue
        n = len(v)
        media = lambda c: sum(x[c] for x in v) / n
        print("%-11s %4d %8.2f %6.0f %8.1f %8.2f %10.0f %11.0f %10.1f%% %8.1f%% %9.1f%%" % (
            nombre, n, media("fps"), media("cpu"), media("dib"), media("mb"), media("fallos"),
            media("espera"), media("main"), media("gpu"), media("otro")))
    print()
    print("informes leidos: %d (%d s de sesion)" % (informes, informes * 10))
    print("dibujados por ancho de la superficie, medio por escena:")
    for nombre in ("menu", "juego", "cinematica"):
        v = [x for x in total.get(nombre, []) if x.get("anchos")]
        if not v:
            continue
        n = len(v)
        anchos = [sum(x["anchos"][i] for x in v) / n for i in range(5)]
        print("  %-11s 1600+ %.1f | 1280-1599 %.1f | 640-1279 %.1f | 256-639 %.1f | <256 %.1f" % tuple([nombre] + anchos))


if __name__ == "__main__":
    main()
