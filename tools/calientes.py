"""Lists the functions the game spends its CPU in, hottest first (the input for orden_funciones.ld).

Usage: python tools/calientes.py <rex_perfil.log> <cod2.elf> <output.txt> [app/orden_funciones.ld]

rex_perfil.log is written by the sampling profiler of the app: every sample carries the addresses it saw (pc, lr
and stack) as imagen+0x.... Each address is resolved with addr2line against the unstripped ELF of the same build.
The three kinds of address are pooled, as they always were; only the weight changed.

Weighted by the CPU of the thread the sample came from, NOT by the number of samples. The profiler pauses and reads
every thread, sleeping ones included, so an idle thread collects just as many samples as a busy one: on the first
Call of Duty 2 session all 15 threads had 407 samples each, while their CPU ranged from 0.0% to 96.0%. Summing
samples therefore ranks the functions of whichever threads happen to be asleep, and the top of the list came out as
svcWaitProcessWideKeyAtomic, condvarWaitTimeout and errno_from_result - the inside of a sleeping thread. The header
line of each thread in the report carries its CPU, and that is the weight now.

A known skew that is not corrected here: the stack and lr addresses of every thread are pooled with its pc, so a
function that appears in every thread's call chain (the thread entry points, the guest's own lock helpers) ranks
higher than the code actually running. On the first session that puts the SDK's thread plumbing first and the
hottest guest function third. Ordering the binary by that is only mildly wrong; ranking by samples, as it did
before, was very wrong.

With the fourth argument the list is also written into the linker script (app/orden_funciones.ld), which is what
places those functions at the start of .text. Two things about that:

  - The names are checked against the ELF's symbol table first, and a name that is not there is dropped. The
    linker matches by mangled name and silently ignores what it does not find, so a list of another game's
    addresses produces no error and no effect at all - which is exactly the state this file was left in.
  - Each function is written under both of its names, `sub_X` and `__imp__sub_X`, when the ELF has both. See
    variantes() below for why one is not enough.
  - Only as many entries are written as the script already had, so that the size of the hot region does not
    change: the point is to put the right functions there, not more of them.

The preamble of the existing script (crt0 and the assembly functions that have no section of their own, matched by
object file) is kept as it is; those cannot be named by symbol.
"""
import collections
import os
import re
import subprocess
import sys

if len(sys.argv) not in (4, 5):
    raise SystemExit(__doc__)
LOG, ELF, SALIDA = sys.argv[1:4]
A2L = os.path.join(os.environ.get('DEVKITPRO', '/opt/devkitpro'), 'devkitA64', 'bin', 'aarch64-none-elf-addr2line')
NM = os.path.join(os.environ.get('DEVKITPRO', '/opt/devkitpro'), 'devkitA64', 'bin', 'aarch64-none-elf-nm')
peso = collections.Counter()
cpu = 0.0
for l in open(LOG, encoding='utf-8', errors='ignore'):
    m = re.match(r'^-- hilo \d+ "([^"]*)": CPU ([\d.]+)%', l)
    if m:
        cpu = float(m.group(2))
        continue
    m = re.match(r'^\s+([\d.]+)%\s+(pc|lr|pila) (.*)', l)
    if not m:
        continue
    w = float(m.group(1)) * cpu / 100
    for a in set(re.findall(r'imagen\+0x([0-9a-f]+)', m.group(3))):
        peso[int(a, 16)] += w
addrs = sorted(peso)
# In batches: a whole session carries a few thousand addresses and Windows rejects a command line over ~32k
# characters (WinError 206, "the filename or extension is too long"), which is what one call per address does.
fn = collections.Counter()
LOTE = 400
for i in range(0, len(addrs), LOTE):
    lote = addrs[i:i + LOTE]
    out = subprocess.run([A2L, '-f', '-e', ELF] + [hex(a) for a in lote],
                         capture_output=True, text=True).stdout.split('\n')
    for k, a in enumerate(lote):
        fn[out[2 * k]] += peso[a]
fn.pop('??', None)
lista = [f for f, _ in fn.most_common()]
open(SALIDA, 'w').write('\n'.join(lista))
print(len(lista), 'hot functions;', sum(1 for f in lista if 'sub_8' in f), 'from the game')
print(lista[:15])


def variantes(nombre):
    """The two names a function can be listed under, its own first.

    A recompiled function has two symbols: `sub_X`, the name the game's address is known by and the one a hook
    replaces, and `__imp__sub_X`, the generated body. Which of the two owns the section (and so which one the
    linker script has to name) is not fixed: on the Switch build `DEFINE_REX_FUNC` emits the body under
    `__imp__sub_X` with `sub_X` as an alias of it, but a hook in app/src defines a real `sub_X` of its own, and
    then the two are different addresses and different sections. Listing both is what places the body; listing
    the wrong one is silent, because the linker ignores a pattern that matches nothing.
    """
    if nombre.startswith('__imp__'):
        return (nombre, nombre[len('__imp__'):])
    return (nombre, '__imp__' + nombre)


if len(sys.argv) == 5:
    # The linker script. The preamble is everything up to the last object-file match (`*libc.a:...(.text)`):
    # those are assembly functions without a section of their own, and they can only be named that way.
    ORDEN = sys.argv[4]
    texto = open(ORDEN, encoding='utf-8').read()
    corte = 0
    for m in re.finditer(r'^  \*[^*].*\.a:.*$', texto, re.M):
        corte = m.end()
    cabecera, cierre = texto[:corte], texto[texto.rindex('}'):]
    # What the script already ordered. Keeping the same count keeps the size of the hot region as it was.
    cuantos = len(re.findall(r'^  \*\(\.text\.', texto, re.M))
    definidos = set(subprocess.run([NM, '--defined-only', ELF], capture_output=True, text=True).stdout.split())
    elegidas = []
    puestas = 0
    for f in lista:
        if f not in definidos:
            continue
        if puestas >= cuantos:
            break
        puestas += 1
        for v in variantes(f):
            if v in definidos and v not in elegidas:
                elegidas.append(v)
    assert elegidas, 'ningun simbolo de la lista existe en el ELF: se estaria comparando contra otro binario'
    open(ORDEN, 'w', encoding='utf-8', newline='\n').write(
        cabecera.rstrip('\n') + '\n' + '\n'.join(f'  *(.text.{f})' for f in elegidas) + '\n' + cierre)
    print(puestas, 'functions of', cuantos, 'written to', ORDEN, 'as', len(elegidas), 'entries',
          '(%d of the list had no symbol in this ELF)' % (len(lista) - len([f for f in lista if f in definidos])))
