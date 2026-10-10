#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""cod2 - PC tool: watch the game's own fatal-error buffer and print what it says.

Why it exists
-------------
The game sometimes dies right after a save with `Unhandled guest access violation: read of guest
0x0000004C`, on the thread that closed the save. Resolving the stack put the fault inside
`sub_8233AB80`, which formats `"********************\nERROR: %s\n********************\n"` and
`"recursive error after: %s"` -- that is Call of Duty 2's own `Com_Error`, so the game had already
decided something was fatal and the access violation is the error path breaking, not the cause.

The message it formats lands in a 4096-byte static at guest 0x83D45480, and the game prints it
through its own console rather than through an Xbox import, so nothing of it reaches the port's log.
Reading that buffer out of the live process is the only way to see it.

Usage:
    python tools/pc/vigilar_error.py                     # until stopped
    python tools/pc/vigilar_error.py --segundos 300
    python tools/pc/vigilar_error.py --cada-ms 20

The game must be running. The two addresses are for this build of Call of Duty 2 and were found by
disassembling `sub_8233AB80` out of the process (tools/pc/ppc.py).
"""

import argparse
import ctypes
import sys
import time

sys.path.insert(0, __file__.rsplit("\\", 1)[0])

from ppc import VENTANA_INVITADO  # noqa: E402

BUFFER = 0x83D45480       # Com_Error's formatted message
BUFFER_TAM = 1024
EN_ERROR = 0x83D376AC     # Com_Error's re-entrancy flag: "an error is already being reported"


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--segundos", type=float, default=0, help="0 = hasta que lo pares")
    p.add_argument("--cada-ms", type=int, default=50)
    args = p.parse_args()

    import leer_guest as lg

    pids = lg.procesos()
    if not pids:
        print("cod2.exe no esta en marcha")
        return 1
    proceso = lg.abrir(pids[0])
    if not proceso:
        print("no se pudo abrir el proceso")
        return 1
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)

    def leer(guest, cuantos):
        datos = ctypes.create_string_buffer(cuantos)
        leidos = ctypes.c_size_t(0)
        ok = k32.ReadProcessMemory(proceso, ctypes.c_void_p(VENTANA_INVITADO + guest), datos,
                                   ctypes.c_size_t(cuantos), ctypes.byref(leidos))
        return datos.raw[: leidos.value] if ok else None

    print("pid %d: vigilando 0x%08X cada %d ms" % (pids[0], BUFFER, args.cada_ms))
    visto = b""
    bandera_vista = 0
    fin = time.time() + args.segundos if args.segundos else None
    while fin is None or time.time() < fin:
        crudo = leer(BUFFER, BUFFER_TAM)
        if crudo is not None:
            mensaje = crudo.split(b"\x00", 1)[0]
            if mensaje != visto:
                visto = mensaje
                print("[%s] 0x%08X = %r" % (time.strftime("%H:%M:%S"), BUFFER, mensaje))
        bandera = leer(EN_ERROR, 4)
        if bandera is not None:
            valor = int.from_bytes(bandera, "big")
            if valor != bandera_vista:
                bandera_vista = valor
                print("[%s] bandera de error 0x%08X = %d" % (time.strftime("%H:%M:%S"), EN_ERROR,
                                                            valor))
        time.sleep(args.cada_ms / 1000.0)
    return 0


if __name__ == "__main__":
    sys.exit(main())
