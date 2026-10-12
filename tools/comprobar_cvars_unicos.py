# cod2 - one-off check: no cvar is defined twice. A duplicate is invisible on the Switch (its link
# line carries --allow-multiple-definition) and only fails on the PC build, so it is worth a scan.
import re
import pathlib
import collections

pat = re.compile(r'REXCVAR_DEFINE_(?:INT32|UINT32|BOOL|DOUBLE|STRING|INT64)\s*\(\s*([A-Za-z_][A-Za-z0-9_]*)')
d = collections.defaultdict(list)
for p in pathlib.Path("app/src").rglob("*.cpp"):
    for m in pat.finditer(p.read_text(encoding="utf-8", errors="replace")):
        d[m.group(1)].append(str(p).replace("\\", "/"))

dups = {k: v for k, v in d.items() if len(v) > 1}
print(f"cvars definidos: {len(d)}; definidos mas de una vez: {len(dups)}")
for k, v in sorted(dups.items()):
    print(f"  {k}: {v}")
assert not dups, "un cvar definido en dos ficheros: en Switch gana el orden de enlazado y calla"
