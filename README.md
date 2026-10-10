<div align=center>

<img src="extras/banner.png" alt="Call of Duty 2" width="80%">

</div>

<h1 align=center>Call of Duty 2 — Nintendo Switch port</h1>

A work-in-progress native Nintendo Switch port of the Xbox 360 version of **Call of Duty 2** (2005), built by static
recompilation with [ReXGlue](https://github.com/rexglue/rexglue-sdk).

> [!IMPORTANT]
> **Nothing has been recompiled for Call of Duty 2 yet.** This repository is a hard fork of
> [nfsmw-nx](https://github.com/StevensND/nfsmw-nx) at commit `4e3ffc24fe80285189f2cea601606c529d5f1b97`, with the
> identifiers and file names renamed from `nfsmw` to `cod2`. The reusable machinery (the Switch layer in `sdk/`, the
> Mesa patch, the shader tooling, the build scripts) is in place. Everything that knows about *this specific game*
> still contains Need for Speed: Most Wanted's addresses and code, and has to be re-derived from Call of Duty 2's own
> `default.xex`. **No NRO builds yet.**

## What Call of Duty 2 is, technically

Measured from the retail disc (`default.xex`, Title ID `415607D1`):

| | |
|---|---|
| Engine | Infinity Ward 3.0 |
| Image base | `0x82000000`, entry point `0x8228BAB0` |
| `.text` | `0x82090000` – `0x82495E14`, 4,218,388 bytes (4.0 MB), 11,082 functions in `.pdata` |
| XEX packing | AES-128, basic compression, 3 blocks |
| Imports | `xboxkrnl.exe` (325 records), `xam.xex` (256 records) |
| Assets | all inside `.ff` fastfiles (zlib, stream starts at `+0x14`) |
| Video | Bink (`.bik`) — `cod2_intro.bik` is 182 MB, `cod2_attract.bik` 191 MB |
| Audio | XMA and IMA ADPCM |
| Shaders | no loose files; 108 valid Xenos shader containers found inside the fastfiles |

These numbers come from one pass over the retail disc. Re-read them from the XEX header of your own copy before
declaring any address: the code generator rejects anything outside the image, and a stale address is the most common
cause of a confusing error.

Two things here differ from the game this fork came from, and both need new work: the cutscenes are **Bink** rather
than WMV3, and the shaders live **inside compressed fastfiles** rather than in files a scanner can walk.

## What is reused from nfsmw-nx

| Path | State |
|---|---|
| `sdk/` — Horizon layer, memory, threads, crash handling, clocks, audio output, ReXGlue with the Switch changes | Reusable as is |
| `mesa/` — the NVK driver patch and its build script | Reusable as is |
| `shaders/` — XenosRecomp, the 2005 shader container reader, the packer, the WebAssembly tools | Reusable; the container format matches |
| `tools/` — third-party fetch, direct calls, gap finder, literal copies, build scripts | Reusable; game-specific addresses come from configuration |
| `app/` — the CMake project and the renderer skeleton | **Not usable yet**: its hooks, native replacements and addresses belong to the other game |

## State of this fork

Done and verified on Windows (see [docs/building.md](docs/building.md)):

- **Renamed** every `nfsmw_*` / `Nfsmw*` / `NFSMW*` file, symbol and identifier to `cod2`. 87 files renamed, 154
  rewritten. The one deliberate survivor is the build string `NfsMWEurope...Release` in
  [docs/editions.md](docs/editions.md), which is what the game itself reports.
- **Rewrote [README.md](README.md)**, [pgo/README.md](pgo/README.md) and the parts of
  [docs/building.md](docs/building.md) that named the other game.
- **Deleted the inherited PGO profiles** (1,328 `.gcda` files under `pgo/`). A GCC profile matches functions by a
  fingerprint of their name, and every recompiled function is named after its guest address, so the old profiles can
  never match this game. `pgo/` is empty on purpose.
- **Replaced the inherited address tables.** `app/huecos.toml` held 774 of the other game's addresses; it now holds
  112 of this game's, regenerated with `tools/huecos.py` from CoD2's own generated code and checked with
  `tools/comprobar_huecos.py`. The `app/overrides.toml` entries are still the other game's and are commented out,
  with the explanations kept as a checklist of what to look for.
- **Fixed the linker script.** `app/orden_funciones.ld` matches symbols by their mangled C++ name, and the rename
  changed the length prefix (`_ZN5cod2` → `_ZN4cod2`, `_ZN8Cod2App` → `_ZN7Cod2App`). 39 entries corrected. A wrong
  length does not fail the build — it silently stops matching — so this one is worth knowing about.
- **Set the Title ID** the extractor expects to `415607D1` (single player), with a secondary branch for `415607DA`
  (`default_mp.xex`).
- **Repointed the installer links** to `fenndooscar/cod2-nx-installer`.
- **Fixed the third-party fetch** (`tools/fetch_thirdparty.py`): it now leaves a `.git` marker in each vendored
  library, which the SDK's CMake requires, and it turns the symbolic links Git writes as text files on Windows
  (libmspack, MoltenVK) into real copies. Without both, step 1 left a tree that could not be configured or compiled.
- **Fixed the code generator build**: the SDK's x86-64 code uses SSSE3/SSE4.1 intrinsics that clang does not enable by
  default, so `sdk/CMakeLists.txt` now adds `-msse4.1` for the SDK's own libraries. `cmake --build out/host --target
  rexglue` now succeeds with clang 23.1.3 and produces `sdk/out/win-amd64/rexglue.exe`.
- **Kept the repository clean**: `.gitignore` now excludes the ~19,000 vendored files step 1 downloads, so only the
  seven files this port actually changes under `sdk/thirdparty/` are tracked.
- **Made the code generator find the entry points only a pointer reaches.** CoD2's static initializer tables are
  arrays of addresses in `.rdata` that are called through a register, so nothing in the code names them and branch
  following cannot see them: the game died at the first one with `Call to invalid or unregistered function at guest
  address 0x...`, and declaring that address only uncovered the next entry of the same table. The Discover phase now
  scans `.rdata`/`.data` for aligned dwords that land inside a code region and treats each as a function
  (`sdk/src/codegen/phase_discover.cpp`, `dataPointerScan`; turn it off with the `data_pointer_scan` cvar). 1,180
  entry points, 21,000 functions analysed instead of 20,100.
- **Let a call to a known function be emitted even when the graph has no edge for it.** Two functions can overlap —
  one is discovered first and its blocks run into the other's code — and the calls of the overlapping body were then
  emitted as `REX_FATAL` in the middle of a working function. `BuilderContext::emit_call_to_known_entry` emits the
  call instead. Unresolved branches fell from 142 to 9, and the remaining 9 are three functions the analysis still
  cannot place.
- **Got Call of Duty 2 running on a PC.** With the two fixes above and `app/huecos.toml` regenerated from this game
  (112 entries, all with bodies), the game starts, mounts the game root, brings up D3D12, SDL input and audio,
  creates its threads, loads 35 MB of game data from `assets/game_root` and draws. Two crashes stood between that and
  a run that stays up:
  - **A mistranslated jump table.** The code generator read the null test of a `bctr` guard (`cmpi cr6, r11, 0`) as the
    table's upper bound, so a four-entry table came out with a single `case 0` and every other index reached the
    `__builtin_trap()` of the default case. The process died of `STATUS_ILLEGAL_INSTRUCTION` with no log line.
    `sdk/src/codegen/function_scanner.cpp` now stops at a comparison against zero instead of reading it as a bound.
    Of the 294 switch tables in this game, this was the only one it got wrong.
  - **A save that killed the process.** `ContentManager::CreateContent` and `WriteContentHeaderFile` used the throwing
    `std::filesystem::create_directories`. They run inside the deferred-completion dispatcher, which has nothing to
    catch an exception, so a save that could not be written ended the run with
    `[FATAL] Dispatch thread: deferred completion threw 'create_directories: Access is denied.'` instead of returning
    an error code. Both now use the non-throwing overload and return `X_ERROR_ACCESS_DENIED`.

  With those two, the game runs indefinitely and draws a scene. See "Not done" 5 for what it does not do yet.
- **Found why the game died intermittently after a save, and fixed it: the guest `longjmp` was returning.** The run
  ended with an access violation inside `rexruntime.dll`, in `HostToGuestFunction<RtlLeaveCriticalSection_entry>`, on
  a guest pointer of `0x00000238` — a value with no business being a pointer. The guest `lr` at the fault was
  `0x8233AC50`, the instruction right after the `RtlLeaveCriticalSection` call in `Com_Error` (`0x8233AB80`), and the
  argument came from `r27`. `Com_Error` reaches that call through a `longjmp`, and `app/overrides.toml` declared no
  `longjmp_address`, so the generator emitted an ordinary call: it *returns* into the next instruction, which the
  original only reaches by branching. The guest `longjmp` (`0x8229BF80`) restores `r13`–`r31` and `r1` out of the
  buffer it is handed, so after it "returned" `r27` held whatever the `setjmp` site had. Both addresses are now
  declared (`setjmp_address = 0x8229F930`, `longjmp_address = 0x8229BF80`, derived from this game's own generated
  code), so the generator emits `ppc_longjmp`/`ppc_setjmp` and the jump never comes back. Two traps here, both
  silent: the keys are **root keys**, so at the bottom of `overrides.toml` they land inside `[functions]` and the
  codegen ignores them without a word (the first attempt regenerated zero files); and an undeclared `longjmp` does
  not fail — it runs the game's code down a path it never takes.
- **Found why the game loses its saves *and* its gamepad when it is started from inside this repository.**
  A process whose executable lives under the repository runs with a restricted, low-integrity token
  (`S-1-16-4096`); the same bytes copied anywhere else run at medium (`S-1-16-8192`). Measured with
  `tools/pc/quien_soy.cpp`, which prints its own token and then tries to create a folder: from the repository
  `ec=5` (`Access is denied`), from outside `ec=0`. Two symptoms follow, and both read like game bugs:
  `XamContentCreate` cannot create its folder, and **SDL enumerates zero joysticks**, so the port never receives
  `SDL_EVENT_GAMEPAD_ADDED` and the pad is simply absent. Neither is in the port's own code. The fix is to run the
  binaries from outside the repository: `tools/pc/jugar.ps1` copies them somewhere writable and points
  `--game_data_root` back at `assets/game_root`.

Not done, in the order they have to happen:

1. **Fetch the third-party sources**: `python tools/fetch_thirdparty.py` (step 1).
2. **Extract the disc** with `tools/fase1_extraer.py` (step 2).
3. **Run the code generator on `default.xex`** and read the import line in `app/codegen.log` first: it must report a
   non-zero number of resolved imports. CoD2's import table does not set the record-type byte the SDK currently looks
   at, so this is the first thing that can block everything.
4. **Re-derive `app/overrides.toml`** for CoD2's addresses. `app/huecos.toml` is done: 112 entries, regenerated from
   this game's own generated code with `tools/huecos.py` and checked with `tools/comprobar_huecos.py`.
5. **Find out what the game is drawing.** The process now stays up and presents frames, but consecutive frames differ
   in every pixel (a mean of 53–75 per channel over 3.1 M of 3.69 M pixels) and no legible text has been found in any
   capture, so it is not a menu and not normal gameplay. Either the game is still early in its startup sequence and
   the picture is a loading state, or the presenter path is handing over the wrong surface or format. The nine
   unresolved branches left in `app/codegen.log` are worth clearing first: they are `REX_FATAL` calls in the
   generated code, and a wrong function boundary in any of those three areas can corrupt anything downstream.
6. **Replace the app's game-specific code** (`app/src/cod2_*.cpp`): the hooks, the D3D trace, the native renderer,
   the audio server — all of it is still the other game's, and it is parked in `app/src/aparcado/` rather than
   compiled.
7. **Build the NVK driver** (`mesa/README.md`) and produce a first NRO.
8. **Add a Bink path** over FFmpeg for the cutscenes, and the matching FFmpeg decoders
   (`sdk/thirdparty/FFmpeg/config_cod2_wmv3.h` is still a WMV3 configuration).
9. **Unpack the `.ff` fastfiles** before scanning for shader containers, then build `cod2_shaders.nfsp`.
10. **Record a PGO profile** on the console.
11. **Write the missing `XamUserWriteProfileSettings` setting types.** The port logs
    `XamUserWriteProfileSettings: Unimplemented data type 189` (`sdk/src/kernel/xam/xam_user.cpp`, which handles only
    `CONTENT` and `BINARY`); 189 is outside the SDK's `Setting::Type` enum, so the value has to be read out of this
    game's own `default.xex` before it can be given a meaning.
12. **Clear the nine unresolved branches** left in `app/codegen.log`. They are `REX_FATAL` calls in the generated
    code, and a wrong function boundary in any of those three areas can corrupt anything downstream.

## Requirements

Everything below is needed for steps 1–6. Verified installed on the machine this fork was prepared on:

| Need | For | State |
|---|---|---|
| [devkitPro](https://devkitpro.org) with devkitA64 and libnx at `C:/devkitPro` | step 6 (the NRO) | installed, `[SwitchDev] Enabled=1` |
| CMake 3.25+ | steps 4, 6 | installed (4.4.4) |
| Ninja, Git, Python 3.10+ | steps 1–6 | installed |
| **Clang 20+ on `PATH`** | step 4 (the code generator) | installed at `C:\Program Files\LLVM\bin` — **add it to `PATH` by hand**, the LLVM installer does not |
| MSYS2 with the CLANG64 host tools, meson, ninja, python, git | step 3 (the Mesa driver) | installed |
| Rust with the `aarch64-unknown-linux-gnu` target | step 3 (NVK's shader compiler is Rust) | installed on both toolchains |
| **devkitPro reachable at `C:/msys64/opt/devkitpro`** | step 3 (the Mesa cross file hardcodes that path) | done: `C:\msys64\opt\devkitpro` is a junction to `C:\devkitPro` |
| `bindgen` on the CLANG64 `PATH` | step 3 (NVK's Rust reads C headers through it) | installed: `/clang64/bin/bindgen` |
| no `clangarm64` packages | step 3 (ARM64 binaries cannot run on x86-64, and the wrapper picks them by executable bit) | clean: 0 packages, directory gone |
| `SPIRV-Tools` + `LLVMSPIRVLib` | step 3 (the host pass builds `mesa_clc`, which makes both required) | installed: 2026.3.1 and 22.1.0.0 |
| **`MESA_SWITCH_RUSTC`** pointing at the GNU toolchain's `rustc.exe` | step 3 (without it `build-unified.sh` exits 2, and the MSVC default cannot link Meson's Rust probe) | set it once as a Windows **user** env var — not a package |
| Vulkan SDK | step 7 (DXC, spirv-val) | installed (1.4.363.0); `Bin` is on the machine `PATH`, so MSYS2 sees `dxc` and `spirv-val` too |
| MinGW g++ and Emscripten | step 7, browser tools | both in MSYS2 **UCRT64** (`/ucrt64/bin/g++` = gcc 16.2.0, `/ucrt64/lib/emscripten/emcc` = 6.0.10). The shader script is a `.sh`, so run it from a UCRT64 shell, not from PowerShell |
| Your own copy of Call of Duty 2 for Xbox 360 | everything | you must provide it |

Everything needed for step 3 is installed. The one row in bold is not a package but a single environment variable, set
once in Windows; [mesa/README.md](mesa/README.md) has the exact value and the list of what was actually run to check
each row. Do **not** set `RUSTUP_TOOLCHAIN` globally — see that file for why.

About 16 GB of RAM for the final optimization step. CoD2's `.text` is 4.0 MB against the other game's 13 MB, so this
should be lighter, but it has not been measured yet.

The full, step-by-step build instructions are in [docs/building.md](docs/building.md). Steps 1, 2, 4 and 5 are
accurate for this game as they stand; step 3 and step 7 describe the other game's toolchain and are correct about it.

## Layout

| Folder | Contents |
|---|---|
| `app/` | The game tree: CMake project, configuration, hooks, renderer, audio, video |
| `sdk/` | ReXGlue SDK with the Horizon layer and the code generator changes |
| `shaders/` | XenosRecomp with this port's changes, the library tools and their WebAssembly builds |
| `mesa/` | The patch for mesa-switch, and its build script |
| `pgo/` | Profile guided optimization profiles, one folder per edition (empty for now) |
| `tools/` | Code generation steps, build scripts, edition support |
| `docs/` | Documentation |

## Documentation

Start with [docs/README.md](docs/README.md). It explains how the whole port fits together, and every technical word
is in the [glossary](docs/glossary.md). The documents describe the inherited port, so read them as a guide to the
machinery rather than as a description of this game.

## Credits

- **Activision and Infinity Ward** — creators of Call of Duty 2. This is an unofficial, fan-made port with no
  affiliation.
- **[StevensND](https://github.com/StevensND/nfsmw-nx)** — nfsmw-nx, the port this repository is forked from. The
  Switch layer, the Mesa patch, the shader tooling and the build scripts are that project's work.
- **[madelrandel-blip](https://github.com/madelrandel-blip/NFSMW-Recompiled)** — NFSMW Recompiled, the recompilation
  project nfsmw-nx started from.
- **[Tom Clay](https://github.com/rexglue/rexglue-sdk)** — the ReXGlue SDK, built on the work of the
  **[Xenia](https://xenia.jp)** team.
- **[hedge-dev](https://github.com/hedge-dev/XenosRecomp)** — XenosRecomp, the Xenos shader translator.
- **[danfromtico](https://github.com/danfromtico/mesa-switch)** and **[NaGaa95](https://github.com/NaGaa95/mesa-switch)** — mesa-switch: Mesa, NVK and NAK on Horizon.
- **[devkitPro](https://devkitpro.org) and [switchbrew](https://github.com/switchbrew/libnx)** — devkitA64 and libnx.
- The other libraries listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Legal

No affiliation with Activision. "Call of Duty" is a trademark of Activision Publishing, Inc. This repository
contains no assets or program code from the original game. Users must provide their own legally obtained copy.
Running homebrew requires custom firmware, which violates Nintendo's ToS and can get a console banned — your call.

Source code is provided under the GPL-3.0 License (see [LICENSE](LICENSE)), inherited through nfsmw-nx from NFSMW
Recompiled. The SDK changes are under the SDK's BSD-3-Clause license and the shader translator and Mesa changes
under MIT, so other ports can reuse them (see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)).
