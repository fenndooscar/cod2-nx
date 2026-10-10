#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""cod2 - PC tool: decode a handful of PowerPC instructions out of a running cod2.exe.

Why it exists
-------------
The generated C++ is a *translation* of the guest code, and it carries the guest instruction as a
comment. That makes it useless for checking the translation: if the code generator mis-decoded an
instruction, the comment and the emitted C++ agree with each other and disagree with the disc.

This decodes the real bytes from the process instead, with no dependency and no disassembler
library, for the small set of opcodes a control-flow argument needs. It is deliberately not a full
disassembler: an opcode it does not know prints as `.long`.

Usage:
    python tools/pc/ppc.py 0x8233AB80 48            # 48 instructions from that guest address
    python tools/pc/ppc.py 0x8233AB80 48 --crudo    # the raw words, for checking by hand

The game must be running. Guest addresses are the 0x82xxxxxx ones the recompiler prints.
"""

import ctypes
import struct
import sys

sys.path.insert(0, __file__.rsplit("\\", 1)[0])

VENTANA_INVITADO = 0x100000000  # REX_MEMORY_SIZE; the arena base the runtime prints on startup


def leer(guest, cuantos):
    """Raw bytes of guest memory, or None."""
    import leer_guest as lg

    pids = lg.procesos()
    if not pids:
        return None
    proceso = lg.abrir(pids[0])
    if not proceso:
        return None
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    datos = ctypes.create_string_buffer(cuantos)
    leidos = ctypes.c_size_t(0)
    ok = k32.ReadProcessMemory(proceso, ctypes.c_void_p(VENTANA_INVITADO + guest), datos,
                               ctypes.c_size_t(cuantos), ctypes.byref(leidos))
    return datos.raw[: leidos.value] if ok else None


def sx(valor, bits):
    """Sign-extend a `bits`-wide field."""
    signo = 1 << (bits - 1)
    return (valor ^ signo) - signo


def desmontar(palabra, direccion):
    """One instruction as text. Only the opcodes a control-flow argument needs."""
    op = palabra >> 26
    rt = (palabra >> 21) & 0x1F
    ra = (palabra >> 16) & 0x1F
    rb = (palabra >> 11) & 0x1F
    sim = palabra & 0xFFFF
    sim_s = sx(sim, 16)

    def r(n):
        return "r%d" % n

    if op == 18:  # b / bl
        li = sx(palabra & 0x03FFFFFC, 26)
        lk = palabra & 1
        aa = palabra & 2
        destino = (direccion + li) if not aa else li
        return "%s 0x%08X" % ("bl" if lk else "b", destino & 0xFFFFFFFF)
    if op == 16:  # bc: the branch depends on BO *and* on which CR bit BI selects
        bo = (palabra >> 21) & 0x1F
        bi = (palabra >> 16) & 0x1F
        bd = sx(palabra & 0xFFFC, 16)
        cr = bi >> 2
        bit = bi & 3
        # BO 12 tests CR[BI] == 1 and BO 4 tests CR[BI] == 0; the mnemonic comes from BI.
        afirmativo = {0: "blt", 1: "bgt", 2: "beq", 3: "bso"}
        negativo = {0: "bge", 1: "ble", 2: "bne", 3: "bns"}
        if bo in (12, 4):
            mn = (afirmativo if bo == 12 else negativo)[bit]
            campo = "cr%d" % cr if cr else ""
            return "%s %s,0x%08X" % (mn, campo, (direccion + bd) & 0xFFFFFFFF)
        if bo in (16, 18):  # bdnz / bdz, which do not test a CR bit
            return "%s 0x%08X" % ("bdnz" if bo == 16 else "bdz", (direccion + bd) & 0xFFFFFFFF)
        return "bc(bo=%d,bi=%d) 0x%08X" % (bo, bi, (direccion + bd) & 0xFFFFFFFF)
    if op == 15:  # lis / addis
        return "%s %s,%d" % ("lis" if ra == 0 else "addis", r(rt), sim_s)
    if op == 14:  # addi / li
        return "%s %s,%s,%d" % ("li" if ra == 0 else "addi", r(rt), r(ra), sim_s)
    if op == 24:  # ori / nop
        if palabra == 0x60000000:
            return "nop"
        return "ori %s,%s,%d" % (r(ra), r(rt), sim)
    if op == 32:  # lwz
        return "lwz %s,%d(%s)" % (r(rt), sim_s, r(ra))
    if op == 34:  # lbz
        return "lbz %s,%d(%s)" % (r(rt), sim_s, r(ra))
    if op == 36:  # stw
        return "stw %s,%d(%s)" % (r(rt), sim_s, r(ra))
    if op == 38:  # stb
        return "stb %s,%d(%s)" % (r(rt), sim_s, r(ra))
    if op == 40:  # lhz
        return "lhz %s,%d(%s)" % (r(rt), sim_s, r(ra))
    if op == 44:  # sth
        return "sth %s,%d(%s)" % (r(rt), sim_s, r(ra))
    if op == 11:  # cmpi
        cr = (palabra >> 23) & 7
        return "cmpwi cr%d,%s,%d" % (cr, r(ra), sim_s)
    if op == 10:  # cmpli
        cr = (palabra >> 23) & 7
        return "cmplwi cr%d,%s,%d" % (cr, r(ra), sim)
    if op == 31:
        xo = (palabra >> 1) & 0x3FF
        # mtspr/mfspr split the SPR number across the two register fields.
        spr = ((palabra >> 16) & 0x1F) << 5 | ((palabra >> 11) & 0x1F)
        if xo == 444:  # or rA,rS,rB, which is how mr is written
            return "mr %s,%s" % (r(ra), r(rb)) if (palabra >> 21) & 0x1F == rb else \
                "or %s,%s,%s" % (r(ra), r((palabra >> 21) & 0x1F), r(rb))
        if xo == 467:  # mtspr
            return "mtspr %d,%s" % (spr, r(rt))
        if xo == 339:  # mfspr
            nombres = {1: "xer", 8: "lr", 9: "ctr"}
            return "mf%s %s" % (nombres.get(spr, "spr%d" % spr), r(rt))
        if xo == 19:   # mfcr
            return "mfcr %s" % r(rt)
        if xo == 40:   # subf
            return "subf %s,%s,%s" % (r(rt), r(ra), r(rb))
        return ".long 0x%08X  (op31 xo=%d)" % (palabra, xo)
    if op == 58 or op == 62:  # ld / std, DS-form: the low two bits of the field are a tag
        ds = sx(palabra & 0xFFFC, 16)
        return "%s %s,%d(%s)" % ("ld" if op == 58 else "std", r(rt), ds, r(ra))
    return ".long 0x%08X" % palabra


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    direccion = int(sys.argv[1], 0)
    cuantos = int(sys.argv[2], 0) if len(sys.argv) > 2 and not sys.argv[2].startswith("--") else 16
    crudo = "--crudo" in sys.argv

    datos = leer(direccion, cuantos * 4)
    if datos is None:
        print("no se pudo leer: esta cod2.exe en marcha?")
        return 1

    for i in range(len(datos) // 4):
        (palabra,) = struct.unpack_from(">I", datos, i * 4)
        aqui = direccion + i * 4
        if crudo:
            print("  %08X  %08X" % (aqui, palabra))
        else:
            print("  %08X  %-40s  ; %08X" % (aqui, desmontar(palabra, aqui), palabra))
    return 0


if __name__ == "__main__":
    sys.exit(main())
