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
  pases/fot      render passes the frame ends. On a tile-based GPU each end stores the whole tile buffer
                 to memory and the next begin loads it back, so this times the attachments is a floor on
                 the frame's traffic that no shading change can remove.
  area MB/fot    the attachment pixels of those passes, in MB per frame, colour plane only. This is what
                 "pases/fot" is multiplied by to get the traffic, so it is the number that says whether
                 cod2_area_de_dibujo_ajustada (draw only into the part of the render target the guest's
                 scissor covers) is actually shrinking the render area: with it off, a 1280-wide target
                 gives 10 MB per pass, because the image is as tall as the EDRAM allocation (2048 rows),
                 not as tall as the guest's 720 (3.5 MB).
  barrera/fot    draws that push a barrier over the whole shared memory buffer, because the port cannot
                 narrow down where the memexport writes. Each one also cuts the render pass in two.
  destino/fot    of "pases/fot", the ones that had to give way to a different render pass, framebuffer or
  barrera/fot*   render area, and the ones ended so that pipeline barriers could be recorded. These two are
  vacia/fot      the structural part: the destinations are what the guest asked for, the barriers are the
                 port's own. "vacia/fot" is a caller asking for the end with nothing to order and
                 "otra/fot" is everything else, occlusion queries among it.
  consultas/fot  guest occlusion queries per frame. Each one ends the render pass twice, and neither end has
                 anything to do with the attachments, so if this is a large part of "pases/fot" then
                 occlusion_query_enable = false in cod2.toml measures what they cost.

Reading it: the scene whose "espera ms/s" is near zero is not limited by the GPU, so lowering the
render resolution cannot help it. The scene with a saturated thread and a saturated GPU is limited by
both, and there the resolution does matter. If "pases/fot" is of the same order as "dib/fot", the frame
is being cut into a render pass per draw and the cost is in the passes, not in the pixels. The pass
reasons say which lever moves it: many "destino" passes mean the frame alternates between render
targets, many "barrera" ones mean the port's own synchronization is cutting it.

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


def media_de(total, clave):
    """Media de una columna sobre todos los informes, sin separar por escena."""
    v = [x for escena_ in total.values() for x in escena_]
    return sum(x[clave] for x in v) / len(v) if v else 0.0


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
        ("pases", r"pases de render: ([\d.]+) finales por fotograma"),
        ("area", r"\| area ([\d.]+) MB por fotograma"),
        ("barrera", r"\| ([\d.]+) dibujados con barrera de toda la memoria compartida"),
        # Los motivos de pase, de su propia linea. Solo el primero lleva "pases por motivo", asi que los demas
        # se anclan a los separadores para no casar con la linea de "pases de render" ni con "barrera/fot".
        ("destino", r"pases por motivo: destino ([\d.]+)"),
        ("pbarrera", r"destino [\d.]+ \| barrera ([\d.]+) \|"),
        ("vacia", r"barrera vacia ([\d.]+)"),
        ("otra", r"\| otra ([\d.]+) \|"),
        ("consultas", r"consultas de oclusion ([\d.]+)"),
        ("poresolve", r"transferencias: [\d.]+ llamadas \(([\d.]+) por resolve\)"),
        ("tr", r"transferencias: ([\d.]+) llamadas"),
        # Los pases por clase, de su propia linea. "transferencias" sale dos veces en el informe, asi que esta se
        # ancla al separador de la linea de clases.
        ("pdib", r"pases por clase: dibujados ([\d.]+)"),
        ("pdibmb", r"pases por clase: dibujados [\d.]+ \(([\d.]+) MB\)"),
        ("ptrans", r"\| transferencias ([\d.]+) \("),
        ("ptransmb", r"transferencias [\d.]+ \(([\d.]+) MB\)"),
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
                "MainXThread", "GPU Cmds", "2o XThread", "pases/fot", "area MB/f", "barrera/fot")
    print("%-11s %4s %8s %6s %8s %8s %10s %11s %11s %9s %10s %10s %10s %12s" % cabecera)
    for nombre in ("menu", "juego", "cinematica"):
        v = total.get(nombre)
        if not v:
            continue
        n = len(v)
        media = lambda c: sum(x[c] for x in v) / n
        print("%-11s %4d %8.2f %6.0f %8.1f %8.2f %10.0f %11.0f %10.1f%% %8.1f%% %9.1f%% %10.1f %10.2f %12.1f" % (
            nombre, n, media("fps"), media("cpu"), media("dib"), media("mb"), media("fallos"),
            media("espera"), media("main"), media("gpu"), media("otro"), media("pases"),
            media("area"), media("barrera")))
    print()
    # Los motivos de pase, en su propia tabla: la de arriba ya es ancha y estos numeros se leen juntos.
    print("por que termina cada pase (por fotograma; la suma es pases/fot):")
    print("%-11s %10s %10s %10s %9s %9s %13s" % ("escena", "pases/fot", "destino", "barrera", "vacia", "otra",
                                                  "consultas/fot"))
    for nombre in ("menu", "juego", "cinematica"):
        v = total.get(nombre)
        if not v:
            continue
        n = len(v)
        media = lambda c: sum(x[c] for x in v) / n
        print("%-11s %10.1f %10.1f %10.1f %9.1f %9.1f %13.1f" % (
            nombre, media("pases"), media("destino"), media("pbarrera"), media("vacia"), media("otra"),
            media("consultas")))
    print()
    # Para que son esos pases. Los "dibujados" son el dibujo del juego y solo se quitan dibujando menos; los de
    # "transferencias" son la maquinaria del port (materializar un render target para poder muestrearlo como
    # textura, y limpiar lo resuelto) y esos si se pueden quitar tocando el port.
    print("para que son esos pases (por fotograma):")
    print("%-11s %11s %10s %13s %11s %9s %13s" % ("escena", "dibujados", "MB", "transferencias", "MB", "pases/fot",
                                                  "suma-dif"))
    for nombre in ("menu", "juego", "cinematica"):
        v = total.get(nombre)
        if not v:
            continue
        n = len(v)
        media = lambda c: sum(x[c] for x in v) / n
        print("%-11s %11.1f %10.2f %13.1f %11.2f %9.1f %13.1f" % (
            nombre, media("pdib"), media("pdibmb"), media("ptrans"), media("ptransmb"), media("pases"),
            media("pdib") + media("ptrans") - media("pases")))
    print()
    print("transferencias por fotograma: %.1f llamadas, de las cuales %.1f las pide un resolve del juego"
          % (media_de(total, "tr"), media_de(total, "poresolve")))
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
