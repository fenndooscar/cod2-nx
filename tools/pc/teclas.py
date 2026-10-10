# cod2 - sends keystrokes to the running game window, to find out whether it is interactive.
#
# Why it exists
#   The port presents frames, but the picture is a smooth gradient with no legible text, so from the
#   capture alone it is impossible to tell "the menu is drawn wrong" from "the game is still loading".
#   The cheapest way to tell them apart is to press keys: a menu answers with a different picture, a
#   stuck loading screen does not.
#
#   PowerShell's SendKeys and WScript.Shell.AppActivate hang for over 90 s on this machine, so this goes
#   straight to user32 with ctypes, which returns immediately.
#
# Usage
#   python tools/pc/teclas.py                 # Enter, Down, Down, Enter
#   python tools/pc/teclas.py return down down
#   python tools/pc/teclas.py --titulo cod2 escape
#
# Names are the KEYEVENTF names of the Windows virtual-key table, lower case: return, escape, space,
# up, down, left, right, tab, f1..f12, a..z, 0..9.
import ctypes
import sys
import time

user32 = ctypes.WinDLL('user32', use_last_error=True)

VK = {
    'return': 0x0D, 'escape': 0x1B, 'space': 0x20, 'tab': 0x09, 'back': 0x08,
    'up': 0x26, 'down': 0x28, 'left': 0x25, 'right': 0x27,
    'shift': 0x10, 'control': 0x11, 'alt': 0x12, 'delete': 0x2E,
}
VK.update({f'f{i}': 0x6F + i for i in range(1, 13)})
VK.update({c: ord(c.upper()) for c in 'abcdefghijklmnopqrstuvwxyz'})
VK.update({str(d): 0x30 + d for d in range(10)})

KEYEVENTF_KEYUP = 0x0002


def ventana(titulo):
    """The first visible top-level window whose title contains `titulo`."""
    encontradas = []

    @ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
    def cb(hwnd, _):
        if not user32.IsWindowVisible(hwnd):
            return True
        n = user32.GetWindowTextLengthW(hwnd)
        if n == 0:
            return True
        buf = ctypes.create_unicode_buffer(n + 1)
        user32.GetWindowTextW(hwnd, buf, n + 1)
        if titulo.lower() in buf.value.lower():
            encontradas.append((hwnd, buf.value))
        return True

    user32.EnumWindows(cb, None)
    return encontradas[0] if encontradas else (None, None)


def tecla(vk):
    user32.keybd_event(vk, 0, 0, 0)
    time.sleep(0.03)
    user32.keybd_event(vk, 0, KEYEVENTF_KEYUP, 0)
    time.sleep(0.15)


def main(argv):
    titulo = 'cod2'
    if '--titulo' in argv:
        i = argv.index('--titulo')
        titulo = argv[i + 1]
        del argv[i:i + 2]
    nombres = argv or ['return', 'down', 'down', 'return']

    hwnd, texto = ventana(titulo)
    if hwnd is None:
        print(f'no hay ventana visible que contenga "{titulo}"')
        return 1
    print(f'ventana {hwnd} "{texto}"')

    user32.SetForegroundWindow(hwnd)
    time.sleep(0.5)
    al_frente = user32.GetForegroundWindow() == hwnd
    print(f'al frente: {al_frente}')

    for nombre in nombres:
        vk = VK.get(nombre.lower())
        if vk is None:
            print(f'  tecla desconocida: {nombre}')
            continue
        tecla(vk)
        print(f'  enviada {nombre}')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
