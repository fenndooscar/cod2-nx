#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Finds code gaps with no function assigned in the codegen output.

The problem
-----------
The binary dies with:
    [FATAL] Call to invalid or unregistered function at guest address 0xXXXXXXXX

That happens when something makes an indirect call (through a pointer or vtable) to an
address the analysis did not mark as a function start. The Validate phase does not
detect them: it only checks direct branches (b / bl). Indirect ones only show up at
run time, one at a time.

How it finds them
-----------------
Every PowerPC instruction takes 4 bytes and the codegen emits exactly one comment line
"\\t// <asm>" per instruction. So:

    fin_de_funcion = inicio + 4 * numero_de_comentarios

With the start of each function (codegen.partition.json) and its computed end, every
stretch between the end of one and the start of the next is a gap with no owner.
Gaps that contain real code are the candidates for breaking startup.

Usage:
    python tools\\huecos.py
    python tools\\huecos.py --min 8
    python tools\\huecos.py --comprobar 0x8215FEA8 0x826BE258
    python tools\\huecos.py --min 8 --log app/codegen.log --toml app/huecos.toml
"""


# ============================================================================
#  Known limitation - a gap is not always one function
#
#  This tool emits one declaration per gap, at its start address. That
#  assumes each gap contains exactly one function, and it is false.
#
#  A gap is simply space that automatic discovery did not claim. It can
#  contain several small functions in a row, typically tables of 8- and
#  16-byte thunks. By declaring only the start, the Discover phase swallows
#  the whole gap as one single function and the other entry points become
#  unreachable. The symptom is:
#
#     [FATAL] Call to invalid or unregistered function at guest address 0x...
#
#  with an address that falls inside a gap that is already declared.
#
#  Verified: four different crashes fell inside declared gaps, at +8, +24,
#  +64 and +96 bytes from their start. That is why the crash seemed to
#  "move": each fix uncovered the next entry of the same gap.
#
#  Splitting every gap every 8 bytes is not the solution: that would be more
#  than 3000 declarations, and most gaps do contain a single function, so
#  entry points would be declared in the middle of functions.
#
#  What does work:
#    1. Split only the gaps proven to have several entries (where a crash
#       already happened inside). See the block at the end of app/huecos.toml.
#    2. A probing run of the release build, which finds them empirically in
#       one go.
# ============================================================================
#
#  THE OTHER HALF: A GAP CAN BE THE TAIL OF THE FUNCTION BEFORE IT
#
#  A gap is not always a new function. When the analysis cut a function short
#  (a jump table, a `bctr` into the middle, a chunk the linear sweep stopped
#  at), the gap is the rest of that same function, and the function's own code
#  branches back into it. Declaring it as a separate function then breaks the
#  branch:
#
#     codegen:  Unresolved conditional branch to 0xX from 0xY
#               Unresolved b target 0xX from 0xY
#               Unresolved function 0xX from 0xY (no CallTarget in FunctionNode)
#               Jump target 0xX unresolved at bctr 0xY
#     runtime:  [FATAL] Unresolved branch from 0xY to 0xX
#
#  The fix is not a bigger declaration: the parent has to be EXTENDED to cover
#  the gap, so the target is inside a single contiguous function:
#
#     "0xPARENT" = { end = 0xGAP_END }
#
#  Which of the two a gap is cannot be told from the gap alone, so the tool
#  reads the previous codegen log (--log) and looks at where the unresolved
#  targets fell. Run it, run the codegen, run it again with the new log: it
#  converges in two or three passes. This is what first boot of Call of Duty 2
#  needed - the fork's file had the same two kinds, found the same way.

import argparse
import bisect
import json
import os
import re
import sys

RE_FUNC = re.compile(r'^DEFINE_REX_FUNC\((?:sub_)?([0-9A-Fa-f]{8})\)')
RE_OBJETIVO = re.compile(r'0x([0-9A-Fa-f]{8})')
RE_PAR_OBJ = re.compile(r'0x([0-9A-Fa-f]{8}).*?0x([0-9A-Fa-f]{8})')

# Header of the file that is included from cod2_manifest.toml. Kept here so the
# generated file explains itself to whoever finds it next.
CABECERA_TOML = """\
# ============================================================================
#  Code gaps of Call of Duty 2 - GENERATED, do not edit by hand.
#
#  Regenerate with:
#      python tools/huecos.py --min {minimo} --log app/codegen.log --toml app/huecos.toml
#
#  A gap is the span between the computed end of a function and the start of
#  the next one. The analysis does not mark them as functions, so an indirect
#  call (through a pointer, a vtable or the CRT's initializer tables) to one of
#  them kills the process at run time with:
#
#      [FATAL] Call to invalid or unregistered function at guest address 0x...
#
#  The Validate phase cannot see them: it only checks direct branches.
#
#  First boot of Call of Duty 2, measured: the process died at 0x82494B78, a
#  24-byte gap right after 0x82494B68 (a CRT static initializer that tail-calls
#  0x822310A8), reached from the game's own initializer table. That is what put
#  this file back into cod2_manifest.toml.
#
#  The codegen now finds most of those itself: the Discover phase scans .rdata
#  and .data for aligned dwords that land in a code region and treats each as a
#  function entry point (dataPointerScan, cvar data_pointer_scan). This tool is
#  for what is left: the gaps nothing points at, and the gaps that are really
#  the tail of the function before them.
#
#  Two kinds of entry, and they are NOT interchangeable:
#
#    "0xGAP" = {{ }}                  the gap is a function of its own
#    "0xPARENT" = {{ end = 0xEND }}   the gap is the tail of the function before it
#
#  Getting the kind wrong is not a build error, it is a crash: a standalone gap
#  declared as a tail makes the target unreachable, and a tail declared as a
#  function leaves the parent's own branches unresolved. tools/huecos.py decides
#  by reading the previous codegen log (--log), so the loop is:
#
#      rexglue codegen cod2_manifest.toml > codegen.log
#      python tools/huecos.py --log app/codegen.log --toml app/huecos.toml
#
#  until the log stops saying "Unresolved". {n} entries, gaps of {minimo} bytes and up.
# ============================================================================

[functions]

"""


def leer_objetivos(ruta):
    """[(target, source)] from the codegen log's unresolved lines.

    Four shapes, all with the target first and the source second:
        Unresolved conditional branch to 0xT from 0xS
        Unresolved b target 0xT from 0xS
        Unresolved function 0xT from 0xS (no CallTarget in FunctionNode)
        Jump target 0xT unresolved at bctr 0xS

    Both matter. A target inside a declared gap means the gap is not a function
    of its own: the function before it branches into it, so the two are one
    function. A source inside a declared gap means the same thing from the other
    side: the gap's own code branches back out into the function before it.
    """
    pares = []
    if not ruta or not os.path.exists(ruta):
        return pares
    for linea in open(ruta, encoding='utf-8', errors='ignore'):
        if 'unresolved' not in linea.lower():
            continue
        m = RE_PAR_OBJ.search(linea)
        if m:
            pares.append((int(m.group(1), 16), int(m.group(2), 16)))
    return pares


RE_DECL = re.compile(r'^"0x([0-9A-Fa-f]{8})"\s*=\s*\{\s*(?:end\s*=\s*0x([0-9A-Fa-f]{8}))?')
RE_FUERA_REGION = re.compile(r'function 0x([0-9A-Fa-f]{8}) not in any code region')
RE_IMPORT = re.compile(r'0x([0-9A-Fa-f]{8}) outranked by import')


def leer_excluidas(ruta, log):
    """{address: reason} of declarations that must not be made.

    Two sources, and the log is the one that matters: the codegen says there
    which of the addresses it was handed it could not turn into code, and why.
    Both reasons break the build, not the codegen - the address is still
    registered in the function table, so the failure is

        lld-link: error: undefined symbol: sub_XXXXXXXX

    with nothing pointing at huecos.toml. The file holds what the log does not
    report, which for this game is nothing so far.
    """
    excluidas = {}
    if ruta and os.path.exists(ruta):
        for linea in open(ruta, encoding='utf-8', errors='ignore'):
            linea = linea.split('#')[0].strip()
            if linea.startswith('0x'):
                excluidas[int(linea, 16)] = 'tools/huecos_excluir.txt'
    if log and os.path.exists(log):
        for linea in open(log, encoding='utf-8', errors='ignore'):
            m = RE_FUERA_REGION.search(linea)
            if m:
                excluidas[int(m.group(1), 16)] = 'not in any code region (data in .text)'
                continue
            m = RE_IMPORT.search(linea)
            if m:
                excluidas[int(m.group(1), 16)] = 'outranked by import (import thunk)'
    return excluidas


def leer_declaraciones(ruta):
    """{start: end or None} from a huecos TOML.

    None means a standalone function declared at that address; an end means the
    function at that address was extended to cover the gap that followed it.
    """
    declaraciones = {}
    if not ruta or not os.path.exists(ruta):
        return declaraciones
    for linea in open(ruta, encoding='utf-8', errors='ignore'):
        m = RE_DECL.match(linea.strip())
        if m:
            declaraciones[int(m.group(1), 16)] = int(m.group(2), 16) if m.group(2) else None
    return declaraciones


def leer_crashes(ruta):
    """Addresses the game was observed to call that are interior to a gap.

    Runtime evidence, not analysis: [FATAL] Call to invalid or unregistered
    function at guest address 0x... is the only way to find them, because the
    call is indirect. A gap holding several entry points (a table of 16-byte
    thunks) produces one crash per entry, which is why the crash seems to move
    as each one is declared.
    """
    direcciones = set()
    if not ruta or not os.path.exists(ruta):
        return direcciones
    for linea in open(ruta, encoding='utf-8', errors='ignore'):
        linea = linea.split('#')[0].strip()
        if linea.startswith('0x'):
            direcciones.add(int(linea, 16))
    return direcciones


def medir_funciones(gen_dir):
    """Returns {start: instruction_count} by walking the generated C++."""
    tamanos = {}
    ficheros = sorted(f for f in os.listdir(gen_dir) if f.endswith('.cpp'))
    for idx, nombre in enumerate(ficheros, 1):
        ruta = os.path.join(gen_dir, nombre)
        actual = None
        n = 0
        with open(ruta, 'r', encoding='utf-8', errors='replace') as fh:
            for linea in fh:
                if linea.startswith('DEFINE_REX_FUNC('):
                    m = RE_FUNC.match(linea)
                    if m:
                        actual = int(m.group(1), 16)
                        n = 0
                elif actual is not None:
                    if linea.startswith('\t// '):
                        n += 1
                    elif linea.startswith('}'):
                        tamanos[actual] = n
                        actual = None
        sys.stdout.write("\r  leidos %d/%d archivos" % (idx, len(ficheros)))
        sys.stdout.flush()
    print()
    return tamanos


def main():
    p = argparse.ArgumentParser(description="Busca huecos de codigo sin funcion.")
    p.add_argument("--gen", default="app/generated/default",
                   help="carpeta con el codigo generado")
    p.add_argument("--min", type=int, default=4,
                   help="tamano minimo de hueco a listar (por defecto 4)")
    p.add_argument("--comprobar", nargs="*", default=[],
                   help="direcciones concretas a localizar, p.ej. 0x8215FEA8")
    p.add_argument("--salida", default="docs/huecos.txt")
    p.add_argument("--toml", default="tools/huecos_functions.toml")
    p.add_argument("--log", default="",
                   help="codegen log of the previous run: the gaps whose interior holds an "
                        "unresolved target are continuations of the function before them")
    p.add_argument("--declarado", default="",
                   help="the TOML that produced --log (usually app/huecos.toml). Its entries are "
                        "the gaps to classify; without it every gap becomes a standalone function")
    p.add_argument("--excluir", default="tools/huecos_excluir.txt",
                   help="addresses that must never be declared, with the reason, one per line")
    p.add_argument("--directo", action="store_true",
                   help="write --toml ready to be included from cod2_manifest.toml "
                        "(default: a reference block that must be trimmed by hand)")
    args = p.parse_args()

    part = os.path.join(args.gen, "codegen.partition.json")
    if not os.path.exists(part):
        raise SystemExit("No existe %s. Lanza antes el codegen." % part)

    print("Leyendo reparto de funciones...")
    asignaciones = json.load(open(part))["assignments"]
    inicios = sorted(int(k, 16) for k in asignaciones)
    print("  %d funciones" % len(inicios))

    print("Midiendo cada funcion en el C++ generado...")
    tamanos = medir_funciones(args.gen)
    print("  %d medidas" % len(tamanos))

    faltan = [a for a in inicios if a not in tamanos]
    if faltan:
        print("  aviso: %d funciones sin medir (se ignoran)" % len(faltan))

    # Calcular huecos
    huecos = []
    for i, a in enumerate(inicios[:-1]):
        n = tamanos.get(a)
        if n is None:
            continue
        fin = a + 4 * n
        siguiente = inicios[i + 1]
        if fin < siguiente:
            huecos.append((fin, siguiente - fin, a, siguiente))

    grandes = [h for h in huecos if h[1] >= args.min]

    # Distribution by size
    dist = {}
    for _, tam, _, _ in huecos:
        dist[tam] = dist.get(tam, 0) + 1

    lineas = []
    def w(s=""):
        print(s)
        lineas.append(s)

    w()
    w("=" * 60)
    w("  HUECOS DE CODIGO SIN FUNCION ASIGNADA")
    w("=" * 60)
    w("Funciones            : %d" % len(inicios))
    w("Huecos totales       : %d" % len(huecos))
    w("Huecos >= %d bytes    : %d" % (args.min, len(grandes)))
    w("Bytes en huecos      : %d" % sum(h[1] for h in huecos))
    w()
    w("Distribucion por tamano de hueco:")
    for tam in sorted(dist):
        w("   %5d bytes  x %d" % (tam, dist[tam]))
    w()

    if args.comprobar:
        w("-" * 60)
        w("Direcciones consultadas:")
        finales = sorted(h[0] for h in huecos)
        for s in args.comprobar:
            t = int(s, 16)
            es_inicio = bisect.bisect_left(inicios, t) < len(inicios) and \
                        inicios[bisect.bisect_left(inicios, t)] == t
            dentro = None
            for fin, tam, ini, sig in huecos:
                if fin <= t < fin + tam:
                    dentro = (fin, tam, ini, sig)
                    break
            w("  0x%08X  inicio de funcion: %s" % (t, "SI" if es_inicio else "NO"))
            if dentro:
                w("      cae en hueco de %d bytes: 0x%08X - 0x%08X"
                  % (dentro[1], dentro[0], dentro[0] + dentro[1]))
                w("      (tras la funcion 0x%08X, antes de 0x%08X)"
                  % (dentro[2], dentro[3]))
            elif not es_inicio:
                w("      no cae en ningun hueco conocido")
        w()

    w("-" * 60)
    w("Primeros 60 huecos de >= %d bytes:" % args.min)
    for fin, tam, ini, sig in grandes[:60]:
        w("  0x%08X  %5d bytes   (tras 0x%08X, antes de 0x%08X)"
          % (fin, tam, ini, sig))
    if len(grandes) > 60:
        w("  ... y %d mas" % (len(grandes) - 60))

    os.makedirs(os.path.dirname(args.salida) or ".", exist_ok=True)
    with open(args.salida, "w", encoding="utf-8") as fh:
        fh.write("\n".join(lineas) + "\n")
        fh.write("\n" + "-" * 60 + "\nTODOS los huecos >= %d bytes:\n" % args.min)
        for fin, tam, ini, sig in grandes:
            fh.write("  0x%08X  %5d bytes   (tras 0x%08X, antes de 0x%08X)\n"
                     % (fin, tam, ini, sig))
    print()
    print("Informe completo en %s" % args.salida)

    # Read everything before opening the output: --declarado is usually the same
    # file as --toml, and opening it for writing truncates it.
    objetivos = leer_objetivos(args.log)
    previas = leer_declaraciones(args.declarado)

    os.makedirs(os.path.dirname(args.toml) or ".", exist_ok=True)
    with open(args.toml, "w", encoding="utf-8") as fh:
        if not args.directo:
            fh.write("# Generado por tools/huecos.py - NO incluir tal cual.\n")
            fh.write("# Declarar una funcion en un hueco que sea relleno o datos puede\n")
            fh.write("# romper el codegen. Copiar solo las entradas que hagan falta.\n")
            fh.write("[functions]\n")
            for fin, tam, ini, sig in grandes:
                fh.write('"0x%08X" = { }   # %d bytes, tras 0x%08X\n' % (fin, tam, ini))
            print("Bloque TOML de referencia en %s" % args.toml)
            return

        # The declarations that produced that log. Without it there is nothing
        # to classify against: everything the analysis left unclaimed becomes a
        # standalone function, which is right for the first pass and wrong for
        # every gap that is really the tail of the function before it.
        if previas:
            base = []
            for a, fin in sorted(previas.items()):
                if fin is None:
                    i = bisect.bisect_left(inicios, a)
                    sig = inicios[i + 1] if i + 1 < len(inicios) else None
                    if sig is None:
                        continue
                    base.append((a, sig))
                else:
                    base.append((a, fin))
            # Parent of a gap that starts at its own address: the function just
            # before it. The codegen does not record it, and it is not needed
            # for the extended entries (there the parent is the key itself).
            padres = {}
            for a, fin in base:
                if a in previas and previas[a] is None:
                    i = bisect.bisect_left(inicios, a)
                    if i:
                        padres[a] = inicios[i - 1]
        else:
            base = [(fin, fin + tam) for fin, tam, _, _ in huecos if tam >= args.min]
            padres = {}
            for a, _ in base:
                i = bisect.bisect_left(inicios, a)
                if i:
                    padres[a] = inicios[i - 1]

        # A declaration the codegen cannot turn into code is worse than no
        # declaration: the address still goes into the function table, so the
        # build fails at link time instead of the codegen saying anything.
        excluidas = leer_excluidas(args.excluir, args.log)
        quitadas = [(a, fin) for a, fin in base if a in excluidas]
        base = [(a, fin) for a, fin in base if a not in excluidas]

        sueltos = []
        continuaciones = []
        for a, fin in base:
            dentro = lambda x: a <= x < fin
            if any(dentro(t) or dentro(s) for t, s in objetivos):
                padre = padres.get(a, a)
                continuaciones.append((padre, fin))
            else:
                sueltos.append((a, fin))

        arregladas = set()
        for a, fin in base:
            arregladas.update(range(a, fin, 4))
        sin_hueco = sorted({t for t, _ in objetivos if t not in arregladas})
        sin_origen = sorted({s for _, s in objetivos if s not in arregladas})

        fh.write(CABECERA_TOML.format(minimo=args.min, n=len(sueltos) + len(continuaciones)))
        for a, fin in sorted(sueltos):
            fh.write('"0x%08X" = { }   # %d bytes\n' % (a, fin - a))
        for padre, fin in sorted(continuaciones):
            fh.write('"0x%08X" = { end = 0x%08X }   # cola de la funcion anterior\n'
                     % (padre, fin))
        print("Huecos: %d funciones sueltas, %d colas de la funcion anterior"
              % (len(sueltos), len(continuaciones)))
        if quitadas:
            motivos = {}
            for a, _ in quitadas:
                motivos.setdefault(excluidas[a], []).append(a)
            print("Excluidas %d declaraciones que no habrian dado cuerpo:" % len(quitadas))
            for motivo, lista in sorted(motivos.items()):
                print("   %-38s %d  (%s)" % (motivo, len(lista),
                                             " ".join("0x%08X" % a for a in sorted(lista)[:6])))
        for etiqueta, lista in (("destino", sin_hueco), ("origen", sin_origen)):
            if lista:
                print("AVISO: %d direcciones sin resolver (%s) caen fuera de todo hueco "
                      "declarado, este fichero no las arregla: %s"
                      % (len(lista), etiqueta, " ".join("0x%08X" % t for t in lista[:12])))
        if args.log and not objetivos:
            print("El log no tenia ninguna direccion sin resolver: nada que reclasificar.")
        print("TOML para incluir en %s" % args.toml)


if __name__ == "__main__":
    main()
