#!/usr/bin/env python3
"""cod2 - syntax-check an app source in seconds instead of a nine-minute build.

Why it exists
  The Switch build links 20,944 recompiled functions and takes about nine minutes. Finding one
  compile error through it costs a coffee per error, and porting the native renderer means hundreds
  of them. This runs the same compiler with the same flags the build uses - taken from the build's
  own ninja files, not written down here where they would rot - and stops after parsing.

What it checks and what it does not
  Syntax and type errors, and missing declarations, exactly as the build reports them. It does NOT
  check linking: a file can be clean here and still reference a symbol nothing defines. Run it
  before the build, never instead of it.

Usage
  python tools/comprobar_sintaxis.py app/src/cod2_nativo_sistema.cpp [more.cpp ...]
  python tools/comprobar_sintaxis.py --todos          # every source the cod2 target compiles

Exit code is 0 only if every file was clean.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
DIRECTORIO_BUILD = RAIZ / "app" / "out" / "sw8"
REGLA = "CXX_COMPILER__cod2_unscanned_Release"


def _variables_de_ninja(texto: str, nombre: str) -> dict[str, str]:
    """The `  KEY = value` lines of one ninja block (a rule or a build statement)."""
    salida: dict[str, str] = {}
    for linea in texto.splitlines():
        if not linea.startswith("  ") or "=" not in linea:
            break
        clave, _, valor = linea.strip().partition("=")
        salida[clave.strip()] = valor.strip()
    return salida


def _bloque(texto: str, cabecera: str) -> str | None:
    """The lines belonging to the block that starts with `cabecera`, or None."""
    inicio = texto.find(cabecera)
    if inicio < 0:
        return None
    resto = texto[inicio + len(cabecera):]
    return cabecera + resto.split("\n\n", 1)[0]


def _leer_compilacion() -> tuple[str, list[str], list[str]]:
    """(compiler, defines+flags+includes) as the cod2 target uses them.

    Read from the build tree so that a flag change in CMake is picked up here without an edit. The
    build statement is found by its rule name, so any app source will do as the sample.
    """
    reglas = DIRECTORIO_BUILD / "CMakeFiles" / "rules.ninja"
    build = DIRECTORIO_BUILD / "build.ninja"
    for ruta in (reglas, build):
        if not ruta.exists():
            raise SystemExit(
                f"no encuentro {ruta}. Configura el proyecto (tools/build.ps1) antes de usar esto."
            )

    bloque_regla = _bloque(reglas.read_text(encoding="utf-8", errors="replace"), f"rule {REGLA}\n")
    if bloque_regla is None:
        raise SystemExit(f"la regla {REGLA} no esta en {reglas}.")
    variables_regla = _variables_de_ninja(bloque_regla.split("\n", 1)[1], REGLA)
    comando = variables_regla.get("command", "")
    # The compiler is the first token of the command; ${LAUNCHER}${CODE_CHECK} are empty for this rule.
    tokens = re.sub(r"\$\{[A-Za-z_]+\}", "", comando).split()
    if not tokens:
        raise SystemExit("la regla no tiene comando.")

    texto_build = build.read_text(encoding="utf-8", errors="replace")
    muestra = re.search(rf"build [^\n]*: {REGLA} [^\n]*\n((?:  [^\n]*\n)*)", texto_build)
    if muestra is None:
        raise SystemExit(f"ningun objeto usa la regla {REGLA}: ¿esta el target cod2 configurado?")
    variables = _variables_de_ninja(muestra.group(1), REGLA)

    banderas: list[str] = []
    for clave in ("DEFINES", "INCLUDES", "FLAGS"):
        valor = variables.get(clave, "")
        # Two escapes to undo, and they are not the same thing. Ninja writes a space inside a value as
        # `$ `; the build's shell then turns `\"` into a plain `"` for a define such as
        # -DREXGLUE_BUILD_CONFIG=\"Release\". subprocess hands argv over without a shell, so the
        # backslashes have to go here or g++ sees a stray one and stops.
        banderas.extend(valor.replace("$ ", " ").replace('\\"', '"').split())
    # -flto and -MD are pointless for a parse-only run and slow it down.
    banderas = [b for b in banderas if not b.startswith(("-flto", "-MD", "-MT", "-MF"))]
    return tokens[0], banderas


def _fuentes_del_target() -> list[Path]:
    texto = (DIRECTORIO_BUILD / "build.ninja").read_text(encoding="utf-8", errors="replace")
    rutas = []
    for m in re.finditer(rf"build [^\n]*: {REGLA} (\S+)", texto):
        ruta = m.group(1).replace("$:", ":")
        if ruta.endswith((".cpp", ".cxx", ".cc")):
            rutas.append(Path(ruta))
    return sorted(set(rutas))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("fuentes", nargs="*", type=Path, help="app sources to check")
    parser.add_argument("--todos", action="store_true", help="check every source the cod2 target compiles")
    args = parser.parse_args()

    fuentes = _fuentes_del_target() if args.todos else args.fuentes
    if not fuentes:
        parser.error("da al menos un fichero, o --todos")

    compilador, banderas = _leer_compilacion()
    malos: list[Path] = []
    for fuente in fuentes:
        if not fuente.exists():
            print(f"[falta]    {fuente}")
            malos.append(fuente)
            continue
        orden = [compilador, *banderas, "-fsyntax-only", str(fuente)]
        resultado = subprocess.run(orden, capture_output=True, text=True, errors="replace")
        if resultado.returncode == 0:
            print(f"[bien]     {fuente}")
        else:
            print(f"[ERROR]    {fuente}")
            print(resultado.stderr.rstrip())
            malos.append(fuente)

    print(f"\n{len(fuentes) - len(malos)}/{len(fuentes)} sin errores de sintaxis.")
    return 1 if malos else 0


if __name__ == "__main__":
    sys.exit(main())
