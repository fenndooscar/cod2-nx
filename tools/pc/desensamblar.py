# cod2 - decode a range of the guest's own instructions, straight from the running process.
#
# Why this exists
#     The generated C++ is a translation, and a translation can be wrong in a way that looks
#     perfectly reasonable: a dropped store leaves a struct field that the C++ never mentions, and
#     reading the C++ then produces a confident story about a field "the game does not set". The
#     bytes of the guest function are the only thing that settles it, and they are already in the
#     process's memory.
#
# How to use it
#     python tools/pc/desensamblar.py 0x822CA8A8 0x822CA9C0
#
# Only the forms a compiler emits for this kind of code are decoded (D-form, X-form, branches,
# rotate/compare); anything else prints as `op <n>`, which is itself informative.

import argparse
import ctypes
import struct
import sys

sys.path.insert(0, str(__import__("pathlib").Path(__file__).resolve().parent))
import leer_guest as lg  # noqa: E402

XO = {
    23: "lwzx", 55: "lwzux", 87: "lbzx", 119: "lbzux", 151: "stwx", 183: "stwux",
    215: "stbx", 247: "stbux", 279: "lhzx", 311: "lhzux", 343: "lhax", 375: "lhaux",
    407: "sthx", 439: "sthux", 266: "add", 40: "subf", 104: "neg", 235: "mullw",
    491: "divw", 459: "divwu", 28: "and", 444: "or", 316: "xor", 124: "nor",
    476: "nand", 792: "sraw", 824: "srawi", 536: "srw", 24: "slw", 0: "cmp",
    32: "cmpl", 467: "mcrxr", 19: "mfcr", 339: "mfspr", 467: "mtspr", 954: "extsb",
    922: "extsh", 26: "cntlzw", 0: "?",
}
D_FORM = {
    14: ("addi", "rS,rA,imm"), 15: ("addis", "rS,rA,imm"), 7: ("mulli", "rS,rA,imm"),
    8: ("subfic", "rS,rA,imm"), 12: ("addic", "rS,rA,imm"), 13: ("addic.", "rS,rA,imm"),
    24: ("ori", "rS,rA,imm"), 25: ("oris", "rS,rA,imm"), 26: ("xori", "rS,rA,imm"),
    27: ("xoris", "rS,rA,imm"), 28: ("andi.", "rS,rA,imm"), 29: ("andis.", "rS,rA,imm"),
    32: ("lwz", "rD,imm(rA)"), 33: ("lwzu", "rD,imm(rA)"), 34: ("lbz", "rD,imm(rA)"),
    35: ("lbzu", "rD,imm(rA)"), 36: ("stw", "rS,imm(rA)"), 37: ("stwu", "rS,imm(rA)"),
    38: ("stb", "rS,imm(rA)"), 39: ("stbu", "rS,imm(rA)"), 40: ("lhz", "rD,imm(rA)"),
    41: ("lhzu", "rD,imm(rA)"), 42: ("lha", "rD,imm(rA)"), 43: ("lhau", "rD,imm(rA)"),
    44: ("sth", "rS,imm(rA)"), 45: ("sthu", "rS,imm(rA)"), 46: ("lmw", "rD,imm(rA)"),
    47: ("stmw", "rS,imm(rA)"), 48: ("lfs", "fD,imm(rA)"), 50: ("lfd", "fD,imm(rA)"),
    52: ("stfs", "fS,imm(rA)"), 54: ("stfd", "fS,imm(rA)"), 58: ("ld", "rD,imm(rA)"),
    62: ("std", "rS,imm(rA)"), 10: ("cmpli", "cr,rA,imm"), 11: ("cmpi", "cr,rA,imm"),
}
BRANCH = {16: "bc", 18: "b"}


def leer(proc, ventana, direccion, tamano):
    buf = ctypes.create_string_buffer(tamano)
    leidos = ctypes.c_size_t(0)
    ok = ctypes.WinDLL("kernel32", use_last_error=True).ReadProcessMemory(
        proc, ctypes.c_void_p(ventana + direccion), buf, ctypes.c_size_t(tamano), ctypes.byref(leidos)
    )
    if not ok:
        return None
    return buf.raw[: leidos.value]


def s16(v):
    return v - 0x10000 if v & 0x8000 else v


def decodificar(palabra, direccion):
    op = palabra >> 26
    if op in D_FORM:
        nombre, _ = D_FORM[op]
        rD = (palabra >> 21) & 31
        rA = (palabra >> 16) & 31
        imm = s16(palabra & 0xFFFF)
        if nombre in ("addi", "addis", "mulli", "subfic", "addic", "addic.") and rA == 0:
            return "%s r%d, %d" % (nombre, rD, imm)
        if nombre in ("ori", "oris", "xori", "xoris", "andi.", "andis."):
            return "%s r%d, r%d, 0x%04X" % (nombre, (palabra >> 21) & 31, rA, palabra & 0xFFFF)
        if nombre in ("cmpi", "cmpli"):
            return "%s cr%d, r%d, %d" % (nombre, (palabra >> 23) & 7, rA, s16(palabra & 0xFFFF))
        if nombre[0] in "ls" and nombre not in ("lmw", "stmw"):
            return "%s r%d, %d(r%d)" % (nombre, rD, imm, rA)
        return "%s r%d, %d(r%d)" % (nombre, rD, imm, rA)
    if op in BRANCH:
        li = palabra & 0x03FFFFFC
        if li & 0x02000000:
            li -= 0x04000000
        destino = direccion + li
        if palabra & 1:
            return "b%s 0x%08X" % ("l" if (palabra >> 31) else "", destino)
        return "b%s cr 0x%08X" % ("l" if (palabra >> 31) else "", destino)
    if op == 31:
        xo = (palabra >> 1) & 0x3FF
        rD = (palabra >> 21) & 31
        rA = (palabra >> 16) & 31
        rB = (palabra >> 11) & 31
        nombre = XO.get(xo, "xo%d" % xo)
        if nombre in ("lwzx", "lbzx", "lhzx", "lhax"):
            return "%s r%d, r%d, r%d" % (nombre, rD, rA, rB)
        if nombre in ("stwx", "stbx", "sthx"):
            return "%s r%d, r%d, r%d" % (nombre, rD, rA, rB)
        if nombre in ("add", "subf", "mullw", "divw", "divwu", "and", "or", "xor", "nor", "slw", "srw"):
            return "%s r%d, r%d, r%d" % (nombre, rD, rA, rB)
        if nombre == "srawi":
            return "srawi r%d, r%d, %d" % (rD, rA, (palabra >> 11) & 31)
        if nombre == "mfspr":
            return "mfspr r%d, %d" % (rD, ((palabra >> 11) & 0x1F) | ((palabra >> 6) & 0x3E0))
        if nombre in ("cmp", "cmpl"):
            return "%s cr%d, r%d, r%d" % (nombre, (palabra >> 23) & 7, rA, rB)
        if nombre in ("extsb", "extsh", "cntlzw"):
            return "%s r%d, r%d" % (nombre, rA, rD)
        if nombre == "mtspr":
            return "mtspr %d, r%d" % (((palabra >> 11) & 0x1F) | ((palabra >> 6) & 0x3E0), rD)
        return "%s r%d, r%d, r%d" % (nombre, rD, rA, rB)
    return "op%d 0x%08X" % (op, palabra)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("inicio")
    ap.add_argument("fin")
    args = ap.parse_args()
    inicio, fin = int(args.inicio, 0), int(args.fin, 0)

    procesos = lg.procesos()
    if not procesos:
        print("no hay juego en marcha")
        return 1
    proc = lg.abrir(procesos[0])
    ventana = lg.ventana_invitado(proc)
    datos = leer(proc, ventana, inicio, fin - inicio)
    if datos is None:
        print("no se ha podido leer 0x%08X" % inicio)
        return 1
    for off in range(0, len(datos), 4):
        palabra = struct.unpack(">I", datos[off : off + 4])[0]
        print("0x%08X  %08X  %s" % (inicio + off, palabra, decodificar(palabra, inicio + off)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
