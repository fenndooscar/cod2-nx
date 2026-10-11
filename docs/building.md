# Building the port yourself

## In short

Players do not need this page: the [installer page](https://fenndooscar.github.io/cod2-nx-installer/) makes the package
from their own copy of the game, with the released NROs. This page is for building the NRO (the Switch program) and
the shader library yourself, from the source code in this repository.

The steps, in order: get the missing libraries, extract the game, build the graphics driver, build the code
generator, translate the game to C++, build the NRO, build the shader library, and copy everything to the SD card.
Each step says what to do and why. If a word is new to you, it is in the [glossary](glossary.md).

Everything was done on Windows 10/11 (64-bit). The scripts are PowerShell, bash (Git Bash or MSYS2) and Python. Only
the driver build is tied to Windows; other systems should work, but nobody has tried them.

## What you need

- [devkitPro](https://devkitpro.org/wiki/Getting_Started), with devkitA64 (the compiler for the Switch) and libnx (the
  Switch library). It installs to `C:/devkitPro`; if you put it somewhere else, set the `DEVKITPRO` environment
  variable to that folder. The installer also leaves `DEVKITPRO=/opt/devkitpro` in the machine environment, which is
  the path the MSYS2 shell uses and does not exist for a native Windows program; the toolchain file ignores it when it
  does not point at a real `libnx/switch.specs`, so the usual location works without you doing anything.
- CMake 3.25 or newer, Ninja, Git, and Python 3.10 or newer, all three on `PATH` (they are run by name).
- A C++ compiler for your PC, to build the code generator. Clang 20 or newer was used. **The LLVM installer does not
  add `C:\Program Files\LLVM\bin` to `PATH`**, so `clang` will not be found in a new shell until you add it by hand.
  This is the one thing people trip over first; the build stops with `CMAKE_C_COMPILER not set`.
- Your own copy of Call of Duty 2 for Xbox 360 (an ISO or the extracted files). Nothing from the game is in this
  repository, and nothing made from it may be added to it.
- About 16 GB of RAM. The translated game code is big, and the final optimization step ([LTO](glossary.md#lto)) alone
  takes about ten minutes.

Only if you need them:

- MSYS2 and Rust, to build the graphics driver yourself (see [mesa/README.md](../mesa/README.md)).
- The Vulkan SDK (for its DXC and spirv-val tools) and MinGW g++, to build the shader library on your PC instead of in
  the installer page.
- Emscripten, to rebuild the tools that the installer page runs in the browser (`shaders/wasm/`).

## 1. Get the missing libraries

**Why:** to keep the repository small, `sdk/thirdparty` only has the files this port changed. The rest comes from the
[ReXGlue](glossary.md#rexglue) version this port is based on (v0.10.0).

```sh
python tools/fetch_thirdparty.py
```

It downloads the rest, and it never overwrites a file that is already there, so the port's changes stay. Two things it
also does, both needed to make the tree buildable: it leaves a `.git` marker in each vendored library (the SDK's CMake
refuses to configure without one), and it replaces the symbolic links Git writes as small text files on Windows
(`libmspack`, `MoltenVK`) with real copies. On a first run it reports about 18,900 files copied; a second run reports
`0 files copied` and is harmless.

## 2. Extract the game

**Why:** the code generator needs the game's program, `default.xex`, and the port needs the game files.

```sh
python tools/fase1_extraer.py path/to/COD2.iso -o assets/game_root
```

This extracts the disc into `assets/game_root` ("fase1_extraer" means "phase 1: extract"). Git ignores the `assets/`
folder, so the game never ends up in the repository.

Call of Duty 2 for Xbox 360 is a single worldwide release (Title ID `415607D1`); the disc also carries a second
program, `default_mp.xex`, for multiplayer (Title ID `415607DA`), which is a different one and would need its own
build. The main folder, `app/`, points at `assets/game_root/default.xex` — the single-player program. The
[editions](editions.md) machinery came with the fork and is still set up for the many regional editions of Need for
Speed: Most Wanted; it has not been adapted yet.

## 3. Build the graphics driver

**Why:** the NRO carries its own Vulkan driver, [NVK](glossary.md#nvk), with this port's changes.

Follow [mesa/README.md](../mesa/README.md): build [mesa-switch](https://github.com/danfromtico/mesa-switch) at commit
`1a8c1a66d6f` with `mesa/mesa-switch-cod2.patch` applied. That page also lists what has to be installed first — the
MSYS2 **CLANG64** host tools, a native gcc, a Rust toolchain with the `aarch64-unknown-linux-gnu` target, and
`switch-libdrm_nouveau` built from source (devkitPro's package server refuses to serve it). None of them come with
devkitPro. The result is an SDK folder. The app needs its subfolder `opt/devkitpro/portlibs/switch`, which contains
`lib/libvulkan.a`.

Verified on this machine: the driver builds, stages, and `tools/build.ps1` links the NRO against it.

## 4. Build the code generator

**Why:** the code generator (`rexglue`) is the program that translates the game to C++. It runs on your PC, so you
build it with your PC's compiler, not with the Switch one.

```sh
cmake -S sdk -B out/host -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build out/host --target rexglue
```

`out/host` is only where CMake keeps its build records. The program itself is written next to the SDK's other
output, in `sdk/out/win-amd64/rexglue.exe` (the SDK puts its binaries under `out/<platform>`). That is the path the
next step needs.

This version has a change that makes the translated code faster: the Xbox 360 processor's registers live in normal C++
variables, shared by the pieces of a split function (`share_registers`, see [toolchain.md](toolchain.md)).

## 5. Translate the game to C++

```sh
REXGLUE=sdk/out/win-amd64/rexglue.exe tools/codegen.sh app
```

This does three things:

1. It runs the code generator with the settings in `app/cod2_manifest.toml`. The C++ goes to `app/generated/default`,
   which git ignores because it is made from the game.
2. It runs `tools/llamadas_directas.py` ("direct calls"). It turns calls between translated functions into direct C++
   calls, so that LTO can optimize across files.
3. It runs `tools/copia_literal.py` ("literal copy"). It writes `app/src/copias_literales/`: exact copies of five game
   functions. The [guards](glossary.md#guard) of their native replacements compare against them (see
   [native-renderer.md](native-renderer.md)).

Then check the declarations, before building anything:

```sh
python tools/comprobar_huecos.py
```

Every address declared in `app/huecos.toml` has to have got a body in the generated C++. A declaration without one is
not a code generator error: it is registered in the function table anyway and surfaces much later, at link time, as
`lld-link: error: undefined symbol: sub_XXXXXXXX`, with nothing pointing at the TOML. The command exits 1 and names
the reason. The two reasons are a data block embedded in `.text` and an import thunk; both are handled by
`tools/huecos_excluir.txt`.

If a run dies with `Call to invalid or unregistered function at guest address 0x...`, that address is an entry point
the analysis never found. Add it to `app/huecos.toml` as a bare `"0xADDR" = { }` line and regenerate; `tools/huecos.py
--comprobar 0xADDR` says whether the address is a function start and which gap it falls in.

## 6. Build the NRO

From PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File tools\build.ps1 -MesaSdk C:\path\to\mesa-sdk\opt\devkitpro\portlibs\switch
```

Or by hand:

```sh
cmake -S app -B app/out/sw8 -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=$PWD/tools/switch/cmake/switch-devkitA64.cmake \
  -DREXSDK_DIR=$PWD/sdk \
  -DREXGLUE_SWITCH_NVK_SDK=/path/to/mesa-sdk/opt/devkitpro/portlibs/switch \
  -DCOD2_PGO= -DCOD2_BUILD_LAUNCHER=OFF
cmake --build app/out/sw8 -j 4
```

The toolchain file path has to be absolute: CMake reads a relative `CMAKE_TOOLCHAIN_FILE` from the build folder, not
from where you run the command. On Windows use `$PWD` from Git Bash, or the full path from PowerShell.

The result is `app/out/sw8/cod2.nro`. Rename it to `cod2-nx.nro` when you copy it to the SD card (step 8).

`cod2.nro` is not a ninja output: it is a POST_BUILD step of the `cod2` target, so `cmake --build` only regenerates it
when it relinks the ELF. If the ELF is already up to date and you deleted the NRO, `ninja` reports `no work to do`,
the build succeeds, and no NRO appears. `tools/build.ps1` handles this by removing the ELF when the NRO is missing, and
it fails with `the build left no app/out/sw8/cod2.nro` if the build still leaves none. If you build by hand and the NRO
is missing, delete `app/out/sw8/cod2` and build again.

Build options, in `app/CMakeLists.txt`:

| Option | Normally | What it does |
|---|---|---|
| `COD2_LTO` | ON | Optimizes the translated code and the app as a whole at the end of the build ([LTO](glossary.md#lto)). It uses 2 jobs (`-flto=2`), because more do not fit in 16 GB of RAM. |
| `COD2_PGO` | empty | `usar` ("use") builds with the recorded profile in `pgo/<edition>`. `generar` ("generate") builds a special NRO that records a new profile. **There is no profile for this game yet**, so leave it empty (see below). |
| `COD2_ORDEN_FUNCIONES` | ON | Puts the functions that run most at the start of the program (list in `app/orden_funciones.ld`), which makes them faster to fetch. It also makes the link write `cod2.map` next to the ELF, which is what `tools/comprobar_orden.py` reads to check that the list did something: the linker ignores an entry it cannot match, silently. |

The icon for the Homebrew Menu is not included. Put a 256x256 JPEG at `tools/switch/icono/cod2_icono.jpg`, or the
NRO gets libnx's default icon.

### The PGO profile

[PGO](glossary.md#pgo) uses a record of which code runs most. **`pgo/` is empty on purpose.** The profiles that came
with the fork were deleted: a GCC profile matches each function by a fingerprint of its name, and every recompiled
function is named after its guest address, so a profile recorded for one game is worthless for another.

To record one:

1. Configure with `-DCOD2_PGO=generar` and build.
2. Play. The NRO writes its counters to `sdmc:/switch/cod2-nx/pgo/` every three minutes.
3. Copy the `.gcda` files into `pgo/<edition>/`.
4. Build again with `-DCOD2_PGO=usar`.

**Why the folder paths matter:** GCC finds each function in the profile by a fingerprint of its name. For functions
that only exist inside one file (`static` functions, or those in an anonymous namespace), the fingerprint also includes
the path of that file. So those functions only match if you build from the same folder paths the profile was recorded
with. Otherwise GCC builds them without the profile: a small loss, not an error.

[toolchain.md](toolchain.md) has the details and the traps.

## 7. Build the shader library

The game's [shaders](glossary.md#shader) are Xbox 360 [microcode](glossary.md#microcode) inside the disc files.
`cod2_shaders.nfsp` has them translated to [SPIR-V](glossary.md#spir-v) (see [shaders.md](shaders.md)). The
installer page builds it in the browser from the disc; you only need this step to build it on your PC.

```sh
shaders/cod2_regenerar_biblioteca_pcf.sh out/library /path/to/extracted/containers
```

The containers (the files that hold the shaders) are taken out of the disc files by
`shaders/cod2_buscar_contenedores.cpp` ("find containers"). For Call of Duty 2 they are inside the `.ff` [fastfiles](glossary.md#fastfile),
which are zlib streams and have to be inflated first.

## 8. Copy it to the SD card

The NRO expects this layout, the same one the installer page makes:

```
sdmc:/switch/cod2-nx/
    cod2-nx.nro         the NRO, renamed
    cod2.toml           the startup settings (cvars)
    game_root/          the files of the disc, loose
```

`tools/preparar_sd.ps1` ("prepare SD") builds that tree for you, from the built NRO, `app/cod2.toml` and
`assets/game_root`:

```powershell
powershell -ExecutionPolicy Bypass -File tools\preparar_sd.ps1 -Destino E:\
powershell -ExecutionPolicy Bypass -File tools\preparar_sd.ps1 -Destino E:\ -Comprobar
```

`-Destino` is the root of the SD card, or any folder to stage the tree in. The script refuses to write inside the
repository (5 GB of game data there would be a `.gitignore` problem), checks the free space before it starts, and
`-Comprobar` only reports what is missing or the wrong size. It copies with `robocopy`, so a copy that is interrupted
can be resumed by running it again.

Three things that are not obvious:

- **The NRO has to be named `cod2-nx.nro`.** The name is what the Homebrew Menu shows and what a
  [forwarder](glossary.md#forwarder) looks for.
- **The game files have to be loose under `game_root/`; the ISO cannot be used.** On the Switch a disc image has to be
  mapped whole into RAM, because Horizon has no file `mmap`
  (`sdk/src/core/mapped_memory_switch.cpp`, cap 256 MB), and the game's ISO is about 7 GB. The extracted folder is
  read with `pread` instead and has no such limit.
- **`cod2_shaders.nfsp` is not needed by this build.** It belongs to the native shader library, which is parked in
  `app/src/aparcado/` and not compiled (step 7 above). The NRO draws through the SDK's emulated Xbox 360 GPU, which
  translates the shaders itself.

`cod2.toml` has the settings read at startup ([cvars](glossary.md#cvar)). The one the installer page gives players is
`release/cod2.toml` in the [installer's repository](https://github.com/fenndooscar/cod2-nx-installer).

Start the NRO from the Homebrew Menu in [title takeover](glossary.md#title-takeover) mode (hold R while you start a
game), or with a [forwarder](glossary.md#forwarder). Started from the album, it does not get enough memory.

## Other editions

**Not adapted to Call of Duty 2 yet.** The tools are here and work, but every file under `tools/editions/*/` holds
Need for Speed: Most Wanted addresses, and there is no `pgo/<edition>` to match. The one case this game has is
`default_mp.xex` (multiplayer), which is a different program and would need its own tree:

- `tools/editions/crear_arbol.py` ("create tree") makes a copy of the app, `app_<edition>`, with all of our addresses
  translated to that edition.
- `tools/editions/build_edition.ps1` builds it from the same folder paths as the main one, so the PGO profile matches.

[editions.md](editions.md) explains the machinery as it was used for the other game.
