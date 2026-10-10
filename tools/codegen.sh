#!/bin/bash
# Runs the code generator on one edition tree and the two steps the build depends on.
#
#   REXGLUE=sdk/out/win-amd64/rexglue.exe tools/codegen.sh app
#   REXGLUE=sdk/out/win-amd64/rexglue.exe tools/codegen.sh app_usa app_usa/enganchadas.txt app_usa/tabla.tsv
#
# The code generator is written into sdk/out/<platform>/, not into the CMake build folder out/host
# (see docs/building.md, step 4).
#
# The second and third arguments are for the trees made by tools/editions/crear_arbol.py: the list of hooked
# functions of that edition and the address table used to create it (see docs/editions.md).
# Set PYTHON if the Python launcher is not "python" (on Windows it is usually "py").
set -e
RAIZ=$(cd "$(dirname "$0")/.." && pwd)
APP=$RAIZ/${1:?usage: tools/codegen.sh <app folder> [hooked functions] [address table]}
REXGLUE=${REXGLUE:?set REXGLUE to the rexglue executable built from sdk/}
PY=${PYTHON:-python}
# The script cds into the app folder before running the generator, so the relative path the usage line
# shows (sdk/out/win-amd64/rexglue.exe) has to be resolved here or it stops at "No such file or directory".
case "$REXGLUE" in
  /*|[A-Za-z]:[/\\]*) ;;
  *) REXGLUE="$RAIZ/$REXGLUE" ;;
esac
cd "$APP"
echo "== codegen ($APP)"
# --ignore-stamp: app/generated/default/codegen.stamp is a fingerprint of the manifest and the toml files,
# and it does not include the generator binary or the analysis code. After a change to sdk/src/codegen/ the
# stamp still matches and the run is a no-op ("0 written, 0 unchanged, 0 deleted, 1 module(s) up to date")
# that leaves the previous generated code in place and reports success. Always regenerate.
"$REXGLUE" codegen --ignore-stamp cod2_manifest.toml > codegen.log 2>&1 || { echo "codegen failed, see $APP/codegen.log"; exit 1; }
cd "$RAIZ"
echo "== direct calls"
if [ -n "${2:-}" ]; then
  "$PY" tools/llamadas_directas.py --gen "$APP/generated/default" --enganchadas "$RAIZ/$2"
else
  "$PY" tools/llamadas_directas.py --gen "$APP/generated/default"
fi
echo "== literal copies"
# Only for the native replacements that compare themselves against a copy of the original. Call of Duty 2
# has none yet: tools/copia_literal.py holds Need for Speed: Most Wanted's addresses and fails on this
# game's code by design ("the generated code does not match"). It runs again as soon as something under
# app/src needs it, which is what the check below looks for.
#
# --exclude-dir=aparcado: the parked native replacements still #include copias_literales/*.inc, but nothing
# compiles them (app/src/aparcado/ is out of the build), and their addresses are the other game's. Without
# this the check answers "yes" and the step fails the whole script with "sub_XXXXXXXX not found".
# The pattern is the include directive, not the bare word: app/src/.gitignore also names the folder.
if grep -rqs --exclude-dir=aparcado 'include "copias_literales' "$APP/src" 2>/dev/null; then
  if [ -n "${3:-}" ]; then
    "$PY" tools/copia_literal.py "$APP/generated/default" "$APP/src/copias_literales" --tabla "$RAIZ/$3"
  else
    "$PY" tools/copia_literal.py "$APP/generated/default" "$APP/src/copias_literales"
  fi
else
  echo "   (no app source includes copias_literales; nothing to copy)"
fi
echo "done"
