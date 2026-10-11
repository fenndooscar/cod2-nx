<div align=center>

<img src="extras/banner.png" alt="Call of Duty 2" width="80%">

</div>

<h1 align=center>Call of Duty 2 — Nintendo Switch port</h1>

A work-in-progress native Nintendo Switch port of the Xbox 360 version of **Call of Duty 2** (2005), built by static
recompilation with [ReXGlue](https://github.com/rexglue/rexglue-sdk).

> [!IMPORTANT]
> **The port runs.** This repository is a hard fork of [nfsmw-nx](https://github.com/StevensND/nfsmw-nx) at commit
> `4e3ffc24fe80285189f2cea601606c529d5f1b97`, with the identifiers and file names renamed from `nfsmw` to `cod2`.
> Call of Duty 2's `default.xex` has been translated with ReXGlue, and the translated program runs on Windows: it
> starts, mounts the game root, brings up D3D12, input and audio, plays its four startup cutscenes, reaches the main
> menu, loads campaign levels, saves and returns to the menu, and keeps the campaign's progress across a restart.
> **The Switch NRO now builds too** (`app/out/sw8/cod2.nro`, 44 MB) against a Mesa/NVK SDK built from this
> repository's patch. What is *not* verified is the game running on the console, and gameplay as opposed to the menu —
> see "Not done".
> Everything that names an address, a hook or a measured number is still being re-derived from this game's own
> `default.xex`; treat any value inherited from the fork as a checklist of *what to look for*, not as an answer.

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
  (`sdk/src/codegen/phase_discover.cpp`, `dataPointerScan`; turn it off with the `data_pointer_scan` cvar). 1,031
  entry points, 21,301 functions analysed instead of 20,122.
- **Stopped that scan from inventing functions inside functions.** The scan takes any aligned dword in
  `.rdata`/`.data` that lands in a code region as an entry point, but the compiler also stores *internal branch
  targets* there, and three consecutive dwords at `0x82068A14` name three labels of one loop. Declaring them
  started a second decode inside a working function, and that function's own backward branches then landed
  outside it and were emitted as `REX_FATAL` in the middle of the loop. The scan now skips a candidate that
  falls inside the blocks of a function already discovered (1,179 → 1,031 entry points), and one hand-added
  entry in `app/huecos.toml` that papered over the same thing was removed. **`app/codegen.log` now reports
  zero unresolved branches**, down from nine.
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
- **Built the NVK driver and linked the first Switch NRO.** `mesa/mesa-switch-cod2.patch` applies cleanly to
  mesa-switch at `1a8c1a66d6f`, `build-unified.sh` runs to `[143/143]`, and the SDK stages with `libEGL.a`, `libGL.a`,
  `libGLESv1_CM.a`, `libGLESv2.a`, `libglapi.a` and `libvulkan.a` (87 MB, NAK inside it). `tools/build.ps1` then
  produces `app/out/sw8/cod2.nro`, 44 MB. Three things blocked it, all silent or misleading, and all three are
  written up in [mesa/README.md](mesa/README.md): `MESA_SWITCH_RUST_TARGET` has to be exported or `ninja` compiles
  NAK for the host (200 errors that look like broken Rust); `switch-libdrm_nouveau` cannot be downloaded —
  `pkg.devkitpro.org` answers with a Cloudflare 403 — so the portlib is built from its four `.c` files with
  devkitPro's own MSYS2 `make`, and working around it by pointing the switch winsys at the **drm** winsys' `nouveau.h`
  compiles but links objects into the wrong list; and `sdk/cmake/rexglue_switch.cmake` no longer names `zstd`, which
  devkitPro does not package and which `nm --undefined-only` proves nothing in the link needs.
- **Found why the campaign kept falling back to the first mission, and fixed it.** After finishing a mission the game
  returned to the menu and offered mission one again. The checkpoint in `savegame.svg` was never the problem: the
  campaign's progress is a **profile** setting, `63E83FFE`, holding `set mis_01 … mis_10` and `mis_difficulty`, and it
  was being dropped on the way in. `XamUserWriteProfileSettings` skips a record whose `type` byte is `UNSET`, and the
  game never writes that byte: it fills only `from`, `user_index`, `setting_id`, `binary.size` and `binary.ptr`, so a
  perfectly good 384-byte record arrived with `type = 0xFF` and a real length and pointer. The guard in
  `sdk/src/kernel/xam/xam_user.cpp` now only drops a record with **no length and no pointer**, and promotes the rest to
  `BINARY`. Verified on the game's own path: the mission after the first writes `G_WriteGame 'tankhunt'` and the profile
  file then reads `set mis_01 "10"` / `set mis_02 "1"`, where before the fix no `63E83FFE` file was written at all.
  `app/src/cod2_consola.cpp` (new, gated on `cod2_consola`) hooks the seven functions on that path and logs what the
  game writes, which is how the record was caught.
- **Fixed `tools/build.ps1` reporting a build it had not done.** `cod2.nro` is a POST_BUILD step of the `cod2` target,
  not a ninja output, so once the ELF is up to date `ninja` has nothing to do: deleting the NRO and rebuilding printed
  `Built … in 0 min` and left no NRO at all. The script now removes the ELF when the NRO is missing, which forces the
  relink and the step, and fails loudly if the build still leaves no NRO.
- **Asked the console for the GPU profile the game never asks for itself.** Every profiler report of an in-game session
  carried the same line, `relojes: CPU 1020.0 MHz, GPU 307.2 MHz, memoria 2131.2 MHz`, and neither `switch-logs/logs/`
  nor `switch-logs/logs-old-1/` had a single `[apm]` line in `rex_stderr.log`: nothing was requesting anything, so the
  GPU sat at the 307.2 MHz Horizon hands a game that asks for nothing. `app/src/cod2_rendimiento.cpp` now calls
  `RexSwitchApmPedirGpuMhz` / `RexSwitchApmAplicar` from `OnPostInitLogging` and asks for the official handheld profile
  `0x92220008` — GPU 460.8 MHz with the memory left at 1331.2 MHz — which is what the previous port did in its own step
  5. It is not an overclock: it goes through `apm`, the same service any commercial game uses. `cod2_switch_gpu_mhz = 0`
  in `cod2.toml` leaves the console alone, and the next report says which clock really came in.

Not done, in the order they have to happen:

1. **Fetch the third-party sources**: `python tools/fetch_thirdparty.py` (step 1).
2. **Extract the disc** with `tools/fase1_extraer.py` (step 2).
3. **Run the code generator on `default.xex`** and read the import line in `app/codegen.log` first: it must report a
   non-zero number of resolved imports. CoD2's import table does not set the record-type byte the SDK currently looks
   at, so this is the first thing that can block everything.
4. **Re-derive `app/overrides.toml`** for CoD2's addresses. `app/huecos.toml` is done: 111 entries, regenerated from
   this game's own generated code with `tools/huecos.py` and checked with `tools/comprobar_huecos.py`.
5. **Find out what the game is drawing.** It is further along than the last note here claimed, and the earlier
   "smooth gradient" reading was wrong: it came from analysing a capture that was never checked against the window's
   own pixels. What is measured now, from the runtime's own log, from the file system and from captures of the window:
   - The game **plays the whole startup movie sequence** — `legal.bik`, `atvi.bik`, `IW_logo.bik`, `cod2_intro.bik` —
     and then reaches the **main menu**, where the attract loop opens `cod2_attract.bik`. It reacts to input and
     streams level data: a run that was sent keystrokes loaded `demolition.ff` (a multiplayer map) and the ordinary
     startup reaches `moscow.ff`, the first campaign mission. Captures of the window during `cod2_intro.bik` show the
     real cutscene, not noise: consecutive frames differ over 89–99 % of their pixels at 3 s apart and over 73–100 %
     at 250 ms apart, and the frames carry the intro's own title cards.
   - **The `…bik` files are the source of those frames.** Renaming `cod2_intro.bik` out of `assets/game_root` makes the
     run log `FAILED: path='D:\cod2_intro.bik' -> 0xc000000f` and never reaches the menu. The guest contains its own
     Bink player: `Not a Bink file.` (`0x82066FA4`), `Error reading Bink header.` (`0x82066FFC`), `Error opening file.`
     (`0x82066F90`) and `The file doesn't contain any compressed frames yet.` (`0x82066FB8`), referenced from
     `sub_82496B20` and `sub_8232C848`. So item 8 below is about the port's *own* decode path, not about whether the
     movies play.
   - It streams about **20 MB per 15 s and it is all first reads**: `rangos: 0 releidas de 324 (0 %); 324 seguidas
     (100 %)`. The earlier `915 releidas de 918 (100 %)` figure came from a run that had been sent keystrokes into a
     level; on the ordinary startup path there is nothing to fix here.
   - The game's own `Com_Error` buffer (`0x83D45480`, read with `tools/pc/vigilar_error.py`) held
     `Game session no longer available` in one run and stayed empty for the first 25 s of another, so it is written
     late, not at startup. Nothing of it reaches the port's log.
   Next: identify what the *level* is drawing — the menu and the movies are accounted for, the in-level scene is not.
6. **Replace the app's game-specific code** (`app/src/cod2_*.cpp`): the hooks, the D3D trace, the native renderer,
   the audio server — all of it is still the other game's, and it is parked in `app/src/aparcado/` rather than
   compiled.
7. **Run the NRO on the console.** It builds and links, and has never been executed on real hardware: that needs a
   Switch with homebrew enabled. The SD card layout it expects is `sdmc:/switch/cod2-nx/` (see
   [docs/building.md](docs/building.md)).
8. **Add a Bink path** over FFmpeg for the cutscenes, and the matching FFmpeg decoders
   (`sdk/thirdparty/FFmpeg/config_cod2_wmv3.h` is still a WMV3 configuration). The game already plays its movies
   through its own Bink player, so this is for replacing that path, not for unblocking the menu.
9. **Unpack the `.ff` fastfiles** before scanning for shader containers, then build `cod2_shaders.nfsp`.
10. **Record a PGO profile** on the console.

~~11. Write the missing `XamUserWriteProfileSettings` setting types.~~ Not a real gap: the
`Unimplemented data type 189` line that used to be quoted here appears in no log this port has produced, and the
`default:` branch that would print it is only reached for a type outside the enum. The type byte the game leaves
unset was the campaign bug fixed above, and it is handled.

~~12. Clear the nine unresolved branches.~~ Done: `app/codegen.log` reports zero. The cause was `dataPointerScan`
declaring functions inside functions, and one hand-added entry in `app/huecos.toml`; both are fixed.

13. **Bring in-game performance to 30 FPS at 720p.** Measured on the console in handheld mode, from
    `switch-logs/logs-old-1/rex/rex_perfil.log` (47 in-game reports): **3.26 FPS**, and it is the GPU, not the CPU.
    The evidence, all from the same log:
    - `libnx por segundo` spends **834 ms of every second** waiting for fences (83 % of the wall time), while the
      game's own main thread answers `0 % esperando en el kernel` — it is not blocked, it is spinning on the GPU.
    - A heavy frame takes 306 ms and spends about **255 ms of it inside the GPU**; the emulated GPU's own command
      recorder (`GPU Commands`) uses only 11.8 % of a core, so it is not the one working.
    - The two guest threads that do burn a core (95.1 % and 95.2 %) are in **busy-waits**: `sub_823800D8` scans nine
      40-byte slots and then spins on the guest flag at `0x82BD6360`, and `sub_8237FE40` is the same shape over
      `sub_8237FD08`. That is 190 % of a core doing nothing, and it buys no frames while the GPU is the wall — the
      previous port only found these worth replacing once the GPU fitted in the budget.

    What is left, in the order the previous port had to do it (`docs/performance-history.md` steps 2–5), and the first
    one is already in:
    1. the GPU profile (done, above): 307.2 → 460.8 MHz, +50 % of GPU;
    2. the GPU work itself, which is the real wall. 424 draws, **65.9 resolves (6.35 screens of area), 437 transfer
       calls, 65.9 texture loads and 28.08 MB of shared memory uploaded per frame** is work the Xbox 360 never did:
       its EDRAM made the resolve and the overdraw free, and this path pays for both. The previous port cut exactly
       this from 60 to 33 ms per frame (one pass instead of tiling, resolves by swapping images, dynamic uniform
       buffers, ZCULL, cheaper shadow maps) — and it needed the native renderer to do it, which is item 6 above;
    3. the guest busy-waits, once 2 fits;
    4. function ordering: `app/orden_funciones.ld` still names 555 of the other game's addresses and matches nothing,
       so it is inert. Regenerating it with `tools/calientes.py` against a console log of *this* game is free FPS.

    Before optimising anything here, read [docs/measuring.md](measuring.md): the `tijera … pantallas` counter is the
    sum of every draw's scissor rectangle, not the pixels shaded, so it is an upper bound of an upper bound and cannot
    be used to size the fill. The numbers that do hold are the fence wait, the per-frame counters above, and the
    profiler's per-thread stacks.

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
| **`MESA_SWITCH_RUST_TARGET`** = `aarch64-unknown-linux-gnu` | step 3 (only `build-unified.sh` exports it; a bare `ninja` compiles NAK for the host) | export it in the shell you rebuild from |
| **`switch-libdrm_nouveau`** | step 3 (the Switch nouveau winsys needs its `struct nouveau_list`; the package cannot be downloaded) | built from source and installed into `C:/devkitPro/portlibs/switch` |
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
