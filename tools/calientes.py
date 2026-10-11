"""Lists the functions the game spends its CPU in, hottest first (the input for orden_funciones.ld).

Usage: python tools/calientes.py <rex_perfil.log> <cod2.elf> <output.txt>

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
"""
import collections
import os
import re
import subprocess
import sys

if len(sys.argv) != 4:
    raise SystemExit(__doc__)
LOG, ELF, SALIDA = sys.argv[1:4]
A2L = os.path.join(os.environ.get('DEVKITPRO', '/opt/devkitpro'), 'devkitA64', 'bin', 'aarch64-none-elf-addr2line')
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
