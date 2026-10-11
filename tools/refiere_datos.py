# cod2 - which recompiled function points at a given data address.
#
# The game's data is not in the generated C++: only the code is recompiled, and the strings, tables and
# initialisers stay in the guest image, where the runtime maps them. A function reaches them the way the
# original PowerPC did, with a `lis` that loads the high half of the address and an `addi`/`ori` that
# adds the low half, so the address is never written down anywhere as a number.
#
# This reads the assembly the generator leaves in a `// comment` above every translated instruction,
# follows those two instructions per register, and reports the function that ends up holding the
# address. It is the tool that answers "who uses this string" and "who calls this table entry".
#
# Usage:
#   py tools/refiere_datos.py 0x8205C208 0x82059EF4      # who points at these addresses
#   py tools/refiere_datos.py --rango 0x82000000 0x82090000 > app/out/datos.txt
#   py tools/refiere_datos.py --gen app/generated/default ...
#
# Addresses are guest ones. A `lis`+`addi` pair is only followed when the two instructions are next to
# each other in the same basic block, which is how the compiler emits them; a register that is reloaded
# from memory in between is dropped, so a miss is possible but a wrong hit is not reported as certain.
import os
import re
import sys

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GEN = os.path.join(RAIZ, 'app', 'generated', 'default')

FUNCION = re.compile(r'^DEFINE_REX_FUNC\((sub_[0-9A-F]+)\)')
LIS = re.compile(r'^//\s*lis\s+(r\d+),\s*(-?\d+)\s*$')
ADDIS = re.compile(r'^//\s*addis\s+(r\d+),\s*(r\d+),\s*(-?\d+)\s*$')
ADDI = re.compile(r'^//\s*(?:addi|ori)\s+(r\d+),\s*(r\d+),\s*(-?\d+)\s*$')


def s16(v):
    """The 16-bit immediate as a signed number, which is how the generator writes it."""
    return v - 0x10000 if v >= 0x8000 else v


def recorrer(rutas, rango=None):
    """Yields (direccion, funcion, fichero, linea) for every address a function builds."""
    for ruta in rutas:
        with open(ruta, encoding='utf-8', errors='ignore') as f:
            funcion = None
            alto = {}   # registro -> valor alto (u32) que le puso el ultimo lis
            traducido = False  # la linea anterior era un comentario ya procesado
            for numero, linea in enumerate(f, 1):
                m = FUNCION.match(linea)
                if m:
                    funcion = m.group(1)
                    alto = {}
                    traducido = False
                    continue
                if funcion is None:
                    continue
                s = linea.strip()
                if not s.startswith('//'):
                    # The line right after a comment is that instruction's own translation, which
                    # names the register it just set; treating it as a use would drop every address
                    # one line after it was built, which is why the first version of this found nothing.
                    if traducido:
                        traducido = False
                    else:
                        for reg in re.findall(r'\b(r\d+)\b', s):
                            alto.pop(reg, None)
                    continue
                m = LIS.match(s)
                if m:
                    # `lis` loads the immediate into the HIGH half, so the value is imm << 16; the
                    # generator agrees and writes it out as one signed 32-bit number.
                    alto[m.group(1)] = (int(m.group(2)) << 16) & 0xFFFFFFFF
                    traducido = True
                    continue
                m = ADDIS.match(s)
                if m:
                    base = alto.get(m.group(2))
                    if base is not None:
                        alto[m.group(1)] = (base + (int(m.group(3)) << 16)) & 0xFFFFFFFF
                    else:
                        alto.pop(m.group(1), None)
                    traducido = True
                    continue
                m = ADDI.match(s)
                if m:
                    base = alto.get(m.group(2))
                    if base is not None:
                        destino = (base + s16(int(m.group(3)))) & 0xFFFFFFFF
                        if rango is None or rango[0] <= destino < rango[1]:
                            yield destino, funcion, os.path.basename(ruta), numero
                    traducido = True
                    continue
                traducido = False
                # Any other instruction touching the register invalidates what we knew about it.
                for reg in re.findall(r'\b(r\d+)\b', s):
                    alto.pop(reg, None)


def main():
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')

    gen = GEN
    if '--gen' in sys.argv:
        gen = sys.argv[sys.argv.index('--gen') + 1]
    rutas = [os.path.join(gen, f) for f in sorted(os.listdir(gen))
             if f.startswith('cod2_recomp.') and f.endswith('.cpp')]

    if '--rango' in sys.argv:
        i = sys.argv.index('--rango')
        rango = (int(sys.argv[i + 1], 0), int(sys.argv[i + 2], 0))
        for direccion, funcion, fichero, linea in recorrer(rutas, rango):
            print('0x%08X %s %s:%d' % (direccion, funcion, fichero, linea))
        return 0

    buscadas = {int(a, 0) for a in sys.argv[1:] if not a.startswith('--')}
    if not buscadas:
        print(__doc__)
        return 2
    encontradas = {}
    for direccion, funcion, fichero, linea in recorrer(rutas):
        if direccion in buscadas:
            encontradas.setdefault(direccion, set()).add('%s %s:%d' % (funcion, fichero, linea))
    for direccion in sorted(buscadas):
        donde = sorted(encontradas.get(direccion, ()))
        print('0x%08X  %s' % (direccion, '\n           '.join(donde) if donde else 'sin referencias'))
    return 0


if __name__ == '__main__':
    sys.exit(main())
