#!/usr/bin/env python3
"""cod2 - PC tool: read guest memory out of a running cod2.exe.

Why it exists
    The XEX is compressed, so the game's data tables are not readable from the file on disk.
    They are, however, readable from the process once the runtime has mapped the image. This
    prints words from a guest address without needing a rebuild or a debugger.

Usage
    python tools/pc/leer_guest.py 0x824337B8 [cuantas]     # 4-byte words
    python tools/pc/leer_guest.py 0x824337B8 24 --bytes    # raw bytes
    python tools/pc/leer_guest.py --regiones               # find the guest memory window
    python tools/pc/leer_guest.py --volcar salida.bin      # the whole loaded image

The game must be running. Guest addresses are the 0x82xxxxxx ones the recompiler prints.
The recompiled code turns a guest address into a host one as `base + addr`, where `base` is
the guest memory window this tool locates by looking for the multi-gigabyte reservation.

`--volcar` writes the loaded image to a file so it can be searched with ordinary tools. It exists
because the XEX on disk is compressed: strings, tables and the `.rdata` the code points at are not
readable from the file, only from the process. Pages the runtime has not committed are written as
zeroes and counted, so a hole is visible rather than silent.
"""

import ctypes
import struct
import sys

PROCESS_QUERY_INFORMATION = 0x0400
PROCESS_VM_READ = 0x0010
MEM_COMMIT = 0x1000
MEM_IMAGE = 0x1000000

TAM_INVITADO = 0x100000000  # REX_MEMORY_SIZE


class MEMORY_BASIC_INFORMATION64(ctypes.Structure):
    _fields_ = [
        ("BaseAddress", ctypes.c_ulonglong),
        ("AllocationBase", ctypes.c_ulonglong),
        ("AllocationProtect", ctypes.c_ulong),
        ("__alignment1", ctypes.c_ulong),
        ("RegionSize", ctypes.c_ulonglong),
        ("State", ctypes.c_ulong),
        ("Protect", ctypes.c_ulong),
        ("Type", ctypes.c_ulong),
        ("__alignment2", ctypes.c_ulong),
    ]


def regiones(proceso):
    """Every committed region of the process, biggest first."""
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    mbi = MEMORY_BASIC_INFORMATION64()
    direccion = 0
    fuera = []
    while direccion < 0x7FFFFFFFFFFF:
        leidos = k32.VirtualQueryEx(proceso, ctypes.c_void_p(direccion),
                                    ctypes.byref(mbi), ctypes.sizeof(mbi))
        if not leidos:
            break
        if mbi.State == MEM_COMMIT and mbi.Type != MEM_IMAGE:
            fuera.append((mbi.AllocationBase, mbi.BaseAddress, mbi.RegionSize, mbi.Protect))
        direccion = mbi.BaseAddress + mbi.RegionSize
        if mbi.RegionSize == 0:
            break
    fuera.sort(key=lambda r: -r[2])
    return fuera


def ventana_invitado(proceso):
    """Base of the guest memory window, read from the log the runtime writes on startup.

    The runtime maps the whole 4 GB arena once and prints where ("Guest memory arena mapped:
    virtual base 0x..."). Guessing it from the process' regions does not work: the arena is
    reserved in one piece but committed in many, and the biggest committed block is only a
    few hundred megabytes.
    """
    import glob
    import os
    import re

    # Both places a run can leave its log: the build tree (tools/build.ps1 and a direct launch) and the
    # folder tools/pc/jugar.ps1 runs from, which is where the game normally is. Looking in only one of
    # them reports "no se encontro la ventana de memoria de invitado" against a game that is running.
    aqui = os.path.dirname(os.path.abspath(__file__))
    candidatos = [
        os.path.join(aqui, "..", "..", "app", "out", "pc", "logs"),
        os.path.join(os.environ.get("LOCALAPPDATA", ""), "cod2-nx", "pc", "logs"),
    ]
    logs = []
    for carpeta in candidatos:
        logs += glob.glob(os.path.join(carpeta, "cod2_*.log"))
    logs.sort(key=os.path.getmtime)
    for ruta in reversed(logs):
        for linea in open(ruta, encoding="utf-8", errors="ignore"):
            m = re.search(r"Guest memory arena mapped: virtual base 0x([0-9A-Fa-f]+)", linea)
            if m:
                return int(m.group(1), 16)
    return None


def procesos(nombre="cod2.exe"):
    """(pid, base de la imagen del host) of every process with that name."""
    import subprocess

    salida = subprocess.run(
        ["tasklist", "/FI", f"IMAGENAME eq {nombre}", "/FO", "CSV", "/NH"],
        capture_output=True, text=True,
    ).stdout
    fuera = []
    for linea in salida.splitlines():
        campos = [c.strip('"') for c in linea.split('","')]
        if len(campos) >= 2 and campos[0].lower() == nombre.lower():
            fuera.append(int(campos[1]))
    return fuera


def abrir(pid):
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    return k32.OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, False, pid)


# The guest image, from the XEX's own headers: base 0x82000000, and the loaded image reaches a little
# past 0x84290000. The file is written with offset 0 at IMAGEN_BASE, so a hit at file offset N is guest
# address IMAGEN_BASE + N and needs no arithmetic.
IMAGEN_BASE = 0x82000000
IMAGEN_FIN = 0x84300000
TROZO = 0x100000


def volcar(proceso, destino):
    """Writes the loaded guest image to a file, one megabyte at a time."""
    ventana = ventana_invitado(proceso)
    if ventana is None:
        print("no se encontro la ventana de memoria de invitado")
        return 1
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    leidos = ctypes.c_size_t(0)
    buzon = ctypes.create_string_buffer(TROZO)
    huecos = 0
    escritos = 0
    with open(destino, "wb") as f:
        for base in range(IMAGEN_BASE, IMAGEN_FIN, TROZO):
            ok = k32.ReadProcessMemory(proceso, ctypes.c_void_p(ventana + base), buzon,
                                       ctypes.c_size_t(TROZO), ctypes.byref(leidos))
            if ok and leidos.value == TROZO:
                f.write(buzon.raw)
                escritos += TROZO
            else:
                # A page the runtime has not committed yet. Written as zeroes so the offsets of
                # everything after it stay right; the count says how much of the image is missing.
                f.write(b"\x00" * TROZO)
                huecos += 1
    print(f"{destino}: {IMAGEN_FIN - IMAGEN_BASE} B, {escritos} legibles, "
          f"{huecos} de {TROZO // 1024} KB a cero; invitado 0x{IMAGEN_BASE:08X} = offset 0")
    return 0


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2

    pids = procesos()
    if not pids:
        print("cod2.exe no esta en marcha")
        return 1
    pid = pids[0]
    proceso = abrir(pid)
    if not proceso:
        print(f"pid {pid}: no se pudo abrir (error {ctypes.get_last_error()})")
        return 1

    if sys.argv[1] == "--regiones":
        for asignada, base, tam, prot in regiones(proceso)[:12]:
            print(f"  base 0x{base:012X} tam 0x{tam:012X} ({tam / 2**20:9.1f} MB) "
                  f"asignada 0x{asignada:012X} prot 0x{prot:X}")
        print(f"ventana de invitado: {ventana_invitado(proceso)}")
        return 0

    if sys.argv[1] == "--volcar":
        return volcar(proceso, sys.argv[2] if len(sys.argv) > 2 else "invitado.bin")

    direccion = int(sys.argv[1], 0)
    cuantos = int(sys.argv[2], 0) if len(sys.argv) > 2 and not sys.argv[2].startswith("--") else 16
    en_bytes = "--bytes" in sys.argv

    ventana = ventana_invitado(proceso)
    if ventana is None:
        print("no se encontro la ventana de memoria de invitado")
        return 1
    host = ventana + direccion
    print(f"pid {pid}: invitado 0x{direccion:08X} -> host 0x{host:012X}")

    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    leidos = ctypes.c_size_t(0)
    datos = ctypes.create_string_buffer(cuantos * 4)
    ok = k32.ReadProcessMemory(proceso, ctypes.c_void_p(host), datos,
                               ctypes.c_size_t(cuantos * 4), ctypes.byref(leidos))
    if not ok:
        print(f"no es legible (error {ctypes.get_last_error()}, leidos {leidos.value})")
        return 1
    crudo = datos.raw[: leidos.value]
    if en_bytes:
        for i in range(0, len(crudo), 16):
            trozo = crudo[i : i + 16]
            print("  +%04X  %s  |%s|" % (
                i, " ".join(f"{b:02X}" for b in trozo),
                "".join(chr(b) if 32 <= b < 127 else "." for b in trozo)))
    else:
        for i in range(len(crudo) // 4):
            (valor,) = struct.unpack_from(">I", crudo, i * 4)  # guest words are big-endian
            print(f"  [{i:3d}] 0x{direccion + i * 4:08X}  0x{valor:08X}")
    return 0



if __name__ == "__main__":
    sys.exit(main())
