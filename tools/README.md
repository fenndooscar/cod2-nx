# Tools

This folder has the scripts that build the port: they get the missing libraries, extract the game, translate it to
C++, fix up the generated code, check the result and build the NRO. Look here when a step of
[docs/building.md](../docs/building.md) names a script and you want to know what it does. The names are in Spanish;
each one is translated below.

## Main steps

In the order of [docs/building.md](../docs/building.md):

| File | What it does |
|---|---|
| `fetch_thirdparty.py` | Fetches the third-party sources of the SDK into `sdk/thirdparty`. The repository only keeps the files this port changed. |
| `fase1_extraer.py` ("phase 1: extract") | Extracts the Xbox 360 ISO into `assets/game_root` and prints the [XEX](../docs/glossary.md#xex) information. |
| `codegen.sh` | Runs the code generator (`rexglue codegen`, which translates the game to C++) on an edition tree (`app/`, or the `app_<edition>` copy of another [edition](../docs/glossary.md#edition)), then `llamadas_directas.py` and `copia_literal.py`. |
| `build.ps1` | Configures and builds the [NRO](../docs/glossary.md#nro) of the default tree (`app/`). |

## After every code generation

The build depends on these two tools. `codegen.sh` runs them for you, right after the code generator. If you run the
code generator another way, run them afterwards, in this order.

| File | What it does |
|---|---|
| `llamadas_directas.py` ("direct calls") | Turns calls between recompiled functions into direct calls, except for calls to functions with a [hook](../docs/glossary.md#hook). [LTO](../docs/glossary.md#lto) needs this to optimise across them (see [docs/toolchain.md](../docs/toolchain.md#direct-calls)). |
| `copia_literal.py` ("literal copy") | Writes the literal copies of five game functions. The [guards](../docs/glossary.md#guard) of their [native versions](../docs/glossary.md#native-replacement) compare against these copies. |

## Checks and analysis

| File | What it does |
|---|---|
| `huecos.py`, `huecos_excluir.txt` ("gaps", "gaps to exclude") | Finds code the code generator's analysis left without a function and writes it to `app/huecos.toml`. `huecos_excluir.txt` lists the gaps to leave out. |
| `comprobar_huecos.py` ("check gaps") | Checks that every address declared in `app/huecos.toml` got a body in the generated C++. A declaration without one is not a codegen error; it fails at link time as `undefined symbol: sub_XXXXXXXX`. Run it between the codegen and the build. |
| `punteros_datos.py` ("data pointers") | Lists the addresses inside `.text` that the game's own data (`.rdata`, `.data`) points at: initializer tables, callback arrays, vtable slots. Those entry points are reached through a register, so branch following cannot find them. Reports which are already functions and which are not. |
| `lee_antes.py` ("read before") | Lists, per recompiled function, the non-volatile registers (the ones a function must keep unchanged for its caller) it reads before writing them. See [docs/toolchain.md](../docs/toolchain.md#registers-as-c-locals) for why this matters. |
| `comprobar_registros.py` ("check registers") | Checks that no generated function writes a register its caller expects back (`r14`..`r31`) without having saved it first. A missing save does not fail where it happens: it corrupts an unrelated function that kept a pointer there across the call, which is how it was found. Reports none today, which is the answer: this was a suspect for the crash in `sub_8233AB80` and it is not the cause. |
| `calientes.py` ("hot ones") | Resolves the samples of the console profiler (`rex_perfil.log`) to functions, hottest first. Its output is the input for `app/orden_funciones.ld`, the list of functions placed first in the program ([function ordering](../docs/glossary.md#function-ordering)). |
| `comprobar_clave_textura.py` ("check texture key") | Checks in the built ELF (the program before it is packed as an NRO) that the texture key is written before it is hashed. It catches an LTO/PGO aliasing pitfall (see [docs/toolchain.md](../docs/toolchain.md#an-lto-and-pgo-pitfall-strict-aliasing-in-xxhash)). |

## PC diagnostics

The PC build (`app/out/pc/cod2.exe`) is where a problem is easiest to look at, so these live next to it. They read
what the port writes into `app/out/pc/logs/` (`cod2_pc_simbolos.cpp`, `cod2_pc_excepciones.cpp`,
`cod2_perfil_pc.h`) and the linker map of the SDK.

| File | What it does |
|---|---|
| `pc/direccion.py` ("address") | Turns a host address from a crash log into the `sub_XXXXXXXX+0xNN` the generated code speaks in, using `logs/simbolos.csv`. Accepts `0x...` or `base+0x...`. |
| `pc/pilas.py` ("stacks") | Reads a `logs/pilas_N.txt` dump of every thread's stack and resolves each frame the same way. Options: `--hilo`, `--csv`, `--todos`. |
| `pc/dllmap.py` | Resolves an offset inside the SDK's own `rexruntime.dll` to a function name. A stripped DLL has no symbols, so it bounds the function with `.pdata` first and only then looks the start address up in the linker map (`-DCMAKE_SHARED_LINKER_FLAGS=-Wl,/MAP`). |
| `pc/pantalla.ps1` ("screen") | Captures the game window to a PNG (`-Salida`, default `app/out/pc/logs/pantalla.png`). Declares the process DPI aware first, or the capture is a scaled-down half-resolution copy. |
| `pc/rafaga.ps1` ("burst") | Calls `pantalla.ps1` several times in a row (`-Cuadros`, `-SeparacionMs`). A still capture cannot tell a frozen menu from a playing cutscene; comparing consecutive captures can. |
| `pc/mapa.py` ("map") | Prints a coarse luminance map of a capture as text, so the layout of what the guest draws can be judged without looking at the image. The capture is 2560x1440 and viewing it downscales it; the map says which region to crop. |
| `pc/recorte.py` ("crop") | Crops a region out of a capture at full resolution (`<entrada.png> <x> <y> <ancho> <alto> <salida.png>`). The guest draws its UI text at 2560x1440, so cropping first is what makes it legible. |
| `pc/tiras.py` ("strips") | Cuts a capture into N horizontal strips of readable width. Same reason as `recorte.py`, for when the region is not known in advance. |
| `pc/leer_guest.py` ("read guest") | Reads guest memory out of the running `cod2.exe` with `ReadProcessMemory`. The XEX is compressed, so the game's data tables cannot be read from the file on disk; they are readable from the process once the image is mapped. `--regiones` finds the guest window. |
| `pc/ppc.py` | Decodes a few PowerPC instructions out of the running `cod2.exe`, from the real bytes rather than from the generated C++ — the generated code carries the guest instruction as a comment, so a mis-decoded instruction makes the comment and the emitted C++ agree with each other and disagree with the disc. Deliberately not a full disassembler: an opcode it does not know prints as `.long`. Usage: `python tools/pc/ppc.py 0x8233AB80 48`, `--crudo` for the raw words. |
| `pc/vigilar_error.py` ("watch error") | Polls the game's own fatal-error buffer (`0x83D45480`, where `Com_Error` formats its message) and its re-entrancy flag (`0x83D376AC`) in the running process and prints every change. The game prints that message through its own console and not through an Xbox import, so nothing of it reaches the port's log, and reading the buffer out of the process is the only way to see what it was complaining about. `--segundos`, `--cada-ms`. |
| `pc/quien_soy.cpp` ("who am I") | Prints this process's token integrity level, and tries to create the folder given as its argument. Written to explain an `Access is denied` (error 5) that the same user only gets from this one executable: the answer was that a child started from inside the workspace runs at low integrity (`S-1-16-4096`) and one started from outside it at medium (`S-1-16-8192`), and only the second may write outside the workspace. Build: `cl /nologo /O2 /std:c++20 /EHsc /Fe:tools\pc\quien_soy.exe /Fo:tools\pc\ tools\pc\quien_soy.cpp /link advapi32.lib`. |
| `pc/jugar.ps1` ("play") | Copies `cod2.exe`, `rexruntime.dll` and `rexgpu-xenos.dll` out of the build tree and starts the game from there, pointing `--game_data_root` back at `assets/game_root`. It exists because of the low-integrity token above: run from inside the repository the game cannot write its saves **and SDL reports zero joysticks**, so the gamepad never appears. `-Destino` changes where it runs from, `-GameDataRoot` where the game data is, `-SinCopiar` skips the copy. |
| `pc/mando_ver.cpp` ("see pad") | Prints, for every game pad Windows knows about, the XInput slots (`XInputGetState` and `XInputGetCapabilities` separately, because SDL enumerates through the second and the two can disagree), the raw-input HID devices SDL's Windows driver accepts, and whether the pad actually changes state over a few seconds. Build: `cl /nologo /O2 /std:c++20 /EHsc /Fe:tools\pc\mando_ver.exe /Fo:tools\pc\ tools\pc\mando_ver.cpp /link xinput.lib user32.lib`. |
| `pc/ventana.ps1` ("window") | Prints the game window's rectangle, its client rectangle and its title. |
| `pc/teclas.py` ("keys") | Sends keystrokes to the running game window through `user32`'s `keybd_event`, so "is it stuck on a loading screen or waiting at a menu?" can be answered by pressing something. `SendKeys` and `WScript.Shell.AppActivate` hang for over 90 s on this machine; this returns immediately. Usage: `python tools/pc/teclas.py return down down return`, `--titulo` to pick the window, names are `return escape space up down left right tab f1..f12 a..z 0..9`. Pair it with `pc/rafaga.ps1`: compare a burst taken while pressing with one taken while idle, because the scene animates on its own and a single pair of captures proves nothing. |
| `pc/vigilante_fallo.cpp` ("fault watcher") | A debugger that starts the game and prints the exception it dies of. Windows' own handler turns an unhandled C++ exception into `0xC0000409` and loses the type; under this one the same death is visible. Build: `cl /nologo /O2 /std:c++20 /EHsc /DPSAPI_VERSION=2 /Fe:tools\pc\vigilante_fallo.exe /Fo:tools\pc\ tools\pc\vigilante_fallo.cpp /link psapi.lib`. |

## Folders

| Folder | What it has |
|---|---|
| `pc/` | The PC-only diagnostics above. Nothing here is built for the Switch. |
| `switch/cmake/` | The CMake toolchain for devkitA64 and [libnx](../docs/glossary.md#libnx), `switch-devkitA64.cmake`: the file that tells CMake how to compile for the Switch. |
| `editions/` | Support for the other editions: address matching, tree creation, checks, profile translation, per-edition build ([docs/editions.md](../docs/editions.md)). |
