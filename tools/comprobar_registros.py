"""cod2 - check that generated functions do not clobber a callee-saved register they never saved.

Why this exists
---------------
A guest function is handed r14..r31 by its caller and must give them back unchanged; that is what
`__savegprlr_N` / `std rN,-K(r1)` in the prologue and `__restgprlr_N` in the epilogue are for. If the
translator emits a function that writes r27 (for instance) without saving it, the corruption does not
show up where it happens: it shows up much later, in some unrelated function that kept a pointer in r27
across the call. That is the shape of the observed crash in `sub_8233AB80` (Com_Error), where `ctx.r27`
held a heap pointer instead of the critical section it had set three instructions after entry.

So this walks every `DEFINE_REX_FUNC` body in `app/generated/` and reports the functions whose body
writes a callee-saved register that its prologue never saved.

Three shapes are skipped because all three are correct code and all three write r14..r31 on purpose:

  * `__restgprlr_N` and friends: restoring r14..r31 from the caller's frame is the whole point of them.
  * Fragment continuations. The translator splits one guest function into several `DEFINE_REX_FUNC`
    bodies when it is too large, and only the first carries the prologue; the rest use the registers the
    first one saved. A body with no `mflr`, no `stwu` and no `__savegprlr_` is such a fragment.
    `sub_82299DE0` is the example: it starts with a `bl` and touches r29/r30/r31 that its parent saved.
  * Entries that take r14..r31 from somewhere other than their own frame: a longjmp reading them out of
    a jmp_buf (`sub_8229BF80`, reached from Com_Error), and the GCC exception landing pads that get r31
    from r12 and then read the registers out of the unwinder's frame (`sub_8229B018`, `sub_822B3B78`).
    Nothing here owns those registers, so nothing here has to give them back.

Those last two shapes are exactly the six functions the check used to report, which is why it now finds
none: the generated code does not have this bug. That is the useful half of the answer.

The check is deliberately coarse: a callee-saved register counts as saved when the prologue stores it
anywhere below the incoming r1, whatever slot the compiler picked. The generated code is not uniform
about the layout — `sub_8228EA28` puts r31 at -8 and r12 at -16, `__savegprlr_27` puts r31 at -16 —
so matching exact slots reports dozens of false positives.

Usage:  python tools/comprobar_registros.py [--generado app/generated/default] [--limite 40]
Exit code 1 when something was found, so it can gate a build.
"""

import argparse
import re
import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
VOLATILES = set(range(0, 14))  # r0-r13 the caller may destroy; r1 is the stack pointer.
CALLEE_SAVED = set(range(14, 32))

# `ctx.r27.u64 = ...` but not `ctx.r27.u32 + 8` (a read) and not `ctx.r27.u64 ==` (a comparison).
ESCRITURA = re.compile(r"ctx\.r(\d+)\.(?:u|s|f)\d+\s*=(?!=)")
# `REX_STORE_U64(ctx.r1.u32 + -48, ctx.r27.u64)` -> the direct `std r27,-48(r1)`.
GUARDADO_DIRECTO = re.compile(r"REX_STORE_U(?:64|32)\(ctx\.r1\.u32 \+ (-?\d+), ctx\.r(\d+)\.u(?:64|32)\)")
# `__savegprlr_27(ctx, base)` -> saves r27..r31.
GUARDADO_AYUDA = re.compile(r"__savegprlr_(\d+)\(ctx, base\)")
CABECERA = re.compile(r"^DEFINE_REX_FUNC\((.+?)\) \{", re.MULTILINE)
# A body that starts its own frame. Anything else is a continuation of the function above it.
PROLOGO = re.compile(r"__savegprlr_|__savefpr_|\bstwu r1,|// mflr r12|// stw r12,-8\(r1\)")
# A callee-saved register read from a frame that is not this function's: `lwz r27,164(r31)` with r31
# taken from r12 (a GCC unwinder landing pad), or a whole register set read out of one pointer, which is
# a longjmp's jmp_buf (`sub_8229BF80` restores r14..r31 that way).
ENTRADA_EXTERNA = re.compile(r"// addi r31,r12,-")
CARGA_DE_BASE = re.compile(
    r"ctx\.r(1[4-9]|2\d|3[01])\.u64 = REX_LOAD_U(?:32|64)\(ctx\.r(\d+)\.u32 \+ \d+\)"
)
MINIMO_PARA_SER_LONGJMP = 8
# `lwz r27,164(r31)` in a body that never writes r31: r31 is the caller's, so this is a return into the
# caller's frame and the registers come back from there.
LECTURA_DE_R31 = re.compile(
    r"ctx\.r(?:1[4-9]|2\d|3[01])\.u64 = REX_LOAD_U(?:32|64)\(ctx\.r31\.u32 \+ \d+\)"
)
ESCRITURA_DE_R31 = re.compile(r"ctx\.r31\.(?:u|s)\d+ =")


def desde_fuera(cuerpo):
    """True when this body takes r14..r31 from somewhere other than its own frame."""
    if ENTRADA_EXTERNA.search(cuerpo):
        return True
    if LECTURA_DE_R31.search(cuerpo) and not ESCRITURA_DE_R31.search(cuerpo):
        return True
    por_base = {}
    for registro, base in CARGA_DE_BASE.findall(cuerpo):
        por_base.setdefault(base, set()).add(registro)
    return any(len(registros) >= MINIMO_PARA_SER_LONGJMP for registros in por_base.values())


def funciones(texto):
    """Yield (nombre, cuerpo) for every DEFINE_REX_FUNC in one file."""
    marcas = list(CABECERA.finditer(texto))
    for i, marca in enumerate(marcas):
        fin = marcas[i + 1].start() if i + 1 < len(marcas) else len(texto)
        yield marca.group(1), texto[marca.end():fin]


def guardados(cuerpo):
    """The callee-saved registers this prologue stores below the incoming r1."""
    resultado = set()
    for desplazamiento, registro in GUARDADO_DIRECTO.findall(cuerpo):
        if int(desplazamiento) < 0:
            resultado.add(int(registro))
    for desde in GUARDADO_AYUDA.findall(cuerpo):
        resultado.update(range(int(desde), 32))
    return resultado


def revisar(nombre, cuerpo):
    """Return a list of complaint strings for one function body, or None when it does not apply."""
    if nombre.startswith("__restgprlr_") or nombre.startswith("__savegprlr_"):
        return None
    if not PROLOGO.search(cuerpo):
        return None  # continuation of the function above; its parent owns the frame.
    if desde_fuera(cuerpo):
        return None  # takes r14..r31 from a jmp_buf or the unwinder; they are not its to preserve.

    guardados_aqui = guardados(cuerpo)
    problemas = []
    for numero in ESCRITURA.findall(cuerpo):
        numero = int(numero)
        if numero in CALLEE_SAVED and numero not in guardados_aqui:
            problemas.append(f"escribe r{numero} sin guardarlo")
    return sorted(set(problemas))


def main():
    analizador = argparse.ArgumentParser(description=__doc__)
    analizador.add_argument("--generado", default=str(RAIZ / "app" / "generated" / "default"))
    analizador.add_argument("--limite", type=int, default=40, help="cuantas funciones listar")
    argumentos = analizador.parse_args()

    carpeta = Path(argumentos.generado)
    if not carpeta.is_dir():
        print(f"no existe {carpeta}", file=sys.stderr)
        return 2

    archivos = sorted(carpeta.glob("*.cpp"))
    if not archivos:
        print(f"no hay .cpp en {carpeta}", file=sys.stderr)
        return 2

    total = 0
    con_prologo = 0
    sospechosas = []
    for archivo in archivos:
        for nombre, cuerpo in funciones(archivo.read_text(encoding="utf-8", errors="replace")):
            total += 1
            problemas = revisar(nombre, cuerpo)
            if problemas is None:
                continue
            con_prologo += 1
            if problemas:
                sospechosas.append((archivo.name, nombre, problemas))

    print(f"{len(archivos)} archivos, {total} funciones, {con_prologo} con prologo propio, "
          f"{len(sospechosas)} escriben un registro guardado por el que llama sin guardarlo")
    for archivo, nombre, problemas in sospechosas[: argumentos.limite]:
        print(f"  {archivo}: {nombre}: {'; '.join(problemas)}")
    if len(sospechosas) > argumentos.limite:
        print(f"  ... y {len(sospechosas) - argumentos.limite} mas")
    return 1 if sospechosas else 0


if __name__ == "__main__":
    sys.exit(main())
