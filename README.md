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
| `app/` — the CMake project and the renderer skeleton | The project builds and the NRO links. The native renderer's **core** is compiled and reachable with `cod2_renderizador = "nativo"`; its **hooks** are still the other game's and are parked |

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
  in `cod2.toml` leaves the console alone, and the next report says which clock really came in. The two 460.8 profiles
  are tried in order, the one that respects the memory first; `cod2_switch_ram_1600` (on) only decides what to do on a
  firmware that has the other one alone, and never forces 1600 by itself.

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
6. **Replace the app's game-specific code** (`app/src/cod2_*.cpp`). The previous game's code is parked in
   `app/src/aparcado/` rather than compiled — except the native renderer's core, which is about the hardware and is
   now built (see item 13). What is left of this item is the game-specific half: the hooks that tell the renderer
   which shader each draw uses, and the audio server. Both need their addresses re-derived from CoD2's own
   `default.xex`; [docs/inventario-renderer-nativo.md](docs/inventario-renderer-nativo.md) has the ones for the
   renderer, and `app/src/cod2_nativo_pendientes.cpp` lists what is still a placeholder.
7. **Run the NRO on the console.** It builds and links, and has never been executed on real hardware: that needs a
   Switch with homebrew enabled. The SD card layout it expects is `sdmc:/switch/cod2-nx/` (see
   [docs/building.md](docs/building.md)).
8. **Add a Bink path** over FFmpeg for the cutscenes, and the matching FFmpeg decoders
   (`sdk/thirdparty/FFmpeg/config_cod2_wmv3.h` is still a WMV3 configuration). The game already plays its movies
   through its own Bink player, so this is for replacing that path, not for unblocking the menu.
9. ~~Unpack the `.ff` fastfiles before scanning for shader containers, then build `cod2_shaders.nfsp`.~~ Done:
   `tools/extraer_shaders.py` inflates each fastfile and the existing container scan does the rest, and
   `shaders/cod2_regenerar_biblioteca.ps1` runs the whole pipeline. Measured over the retail data: **111 fastfiles,
   0 unreadable, 7,508 container occurrences, 1,124 distinct** (653 pixel / 471 vertex), all of them translating,
   compiling and passing `spirv-val`, and the library is byte-identical across three runs. See
   [docs/shaders.md](docs/shaders.md). What is *not* done is the part that matters: nothing has yet confirmed that a
   live draw asks for a container that is in the library.
10. **Record a PGO profile** on the console.

~~11. Write the missing `XamUserWriteProfileSettings` setting types.~~ Not a real gap: the
`Unimplemented data type 189` line that used to be quoted here appears in no log this port has produced, and the
`default:` branch that would print it is only reached for a type outside the enum. The type byte the game leaves
unset was the campaign bug fixed above, and it is handled.

~~12. Clear the nine unresolved branches.~~ Done: `app/codegen.log` reports zero. The cause was `dataPointerScan`
declaring functions inside functions, and one hand-added entry in `app/huecos.toml`; both are fixed.

13. **Bring in-game performance to 30 FPS at 720p.** Measured on the console in handheld mode, from
    `switch-logs/logs-old-1/rex/rex_perfil.log` (47 in-game reports): **3.26 FPS**, and it is the GPU, not the CPU.
    A second session, `switch-logs/logs` (2026-10-11, 14 in-game reports), with the GPU profile and the function
    ordering in place, reads **5.0–5.8 FPS**, 281–403 draws, 32–60 resolves, 33–60 texture loads and 26–28 MB
    uploaded per frame, and still **744–774 ms of every second waiting for fences**. The clock alone accounts for
    3.26 × 460.8/307.2 = 4.89 of that, so the ordering was worth about +10 % on top: real, and nowhere near enough.
    A third session (`switch-logs/logs`, 2026-10-12) reads **4.86 FPS over 13 in-game reports** with 333 draws and
    the two renderer defaults of `app/src/cod2_rendimiento.cpp` demonstrably applied — that is, the changes of the
    second round bought nothing, which is what the two counters added for it were for.
    The evidence, all from the first log:
    - `libnx por segundo` spends **834 ms of every second** waiting for fences (83 % of the wall time), while the
      game's own main thread answers `0 % esperando en el kernel` — it is not blocked, it is spinning on the GPU.
    - A heavy frame takes 306 ms and spends about **255 ms of it inside the GPU**; the emulated GPU's own command
      recorder (`GPU Commands`) uses only 11.8 % of a core, so it is not the one working.
    - The two guest threads that do burn a core (95.1 % and 95.2 %) are in **busy-waits**: `sub_823800D8` scans nine
      40-byte slots and then spins on the guest flag at `0x82BD6360`, and `sub_8237FE40` is the same shape over
      `sub_8237FD08`. That is 190 % of a core doing nothing, and it buys no frames while the GPU is the wall — the
      previous port only found these worth replacing once the GPU fitted in the budget.

    What is left, in the order the previous port had to do it (`docs/performance-history.md` steps 2–5). Points 1 and 4
    are already in:
    1. the GPU profile (done, above): 307.2 → 460.8 MHz, +50 % of GPU;
    2. the GPU work itself, which is the real wall. 424 draws, **65.9 resolves (6.35 screens of area), 437 transfer
       calls, 65.9 texture loads and 28.08 MB of shared memory uploaded per frame** is work the Xbox 360 never did:
       its EDRAM made the resolve and the overdraw free, and this path pays for both. The previous port cut exactly
       this from 60 to 33 ms per frame (one pass instead of tiling, resolves by swapping images, dynamic uniform
       buffers, ZCULL, cheaper shadow maps) — and it needed the native renderer to do it, which is item 6 above.
       Two settings the SDK carries and the guest never asked for are already switched off in
       `app/src/cod2_rendimiento.cpp`: `anisotropic_override = 3`, which overwrites the fetch constant's own
       anisotropic filtering with 4x on every texture that has mips, and `gamma_render_target_as_unorm16`, which
       makes a `k_8_8_8_8_GAMMA` target eight bytes per pixel instead of the four the EDRAM holds. Two further
       changes are in the SDK's own per-draw path. The first is a barrier in `VulkanRenderTargetCache::Update`
       that was pushed for every used render target on every update — once per draw in this game — and was a
       no-op whenever the target was already in the draw usage, because every field of it had the same source and
       destination. It is now skipped, and the second console session (`switch-logs/logs`, 2026-10-11, 14 in-game
       reports) says it was **worth nothing**: 4.9–5.4 FPS, inside the run-to-run spread. The reason is that
       `SubmitBarriersAndEnterRenderTargetCacheRenderPass` already returns early when the pass and the framebuffer
       are unchanged, so removing a barrier that ordered nothing removed no pass. The passes come from the call
       sites that pass `force_end_render_pass = true`, and those have to end the pass: what follows them — a
       resolve into a texture, a texture upload, a compute dispatch — cannot run inside a render pass at all;
    5. the render area of those passes (done, and the first change sized against a measured number). That same
       second session added two counters: `N pases de render` per frame and `N dibujados con barrera de toda la
       memoria compartida`. In game the first reads **141.9 against 333 draws**, and the second **0.0**, which
       rules the whole-shared-memory memexport barrier out. A render target image is as tall as the EDRAM
       allocation, not as tall as the image the guest uses: `RenderTargetCache::GetRenderTargetHeight` gives 2048
       rows for a 1280-wide 32bpp target, because that is the whole 10 MB of EDRAM, while this game's scissor is
       1280x720 (the driver's own ZCULL plane for the depth target says 1280x2048). Every one of those 141.9
       passes therefore stored and reloaded 2.8 times the pixels it had to. `Framebuffer` now carries the union
       of the scissors seen so far and the draw path passes it as the render area — the scissor is the hardware
       bound of what a draw can write, so the union is safe, and it only grows, so a pass is begun again only
       when the guest reaches further than before. The transfer and resolve passes, which write a destination of
       their own, get the union of the rectangles they are about to draw or clear — the tiles the guest resolved,
       plus the resolve clear — instead. `cod2_area_de_dibujo_ajustada = false` in `cod2.toml` gives the old area
       back without a rebuild, and the report's `area MB por fotograma` (counter 35) is what shows whether it
       worked: without it a 1280-wide pass costs 10 MB of colour attachment, with it 3.5 MB.

       The render area says what each pass costs; it does not say why there are 141.9 of them, and the next lever
       depends on that. `VulkanCommandProcessor::EndRenderPass` now takes a reason (counters 36–39: the next pass
       has a different destination, pipeline barriers had to be recorded, a caller asked for the end with nothing
       to order, or anything else) and the guest's occlusion queries are counted separately (counter 40, since each
       one ends the pass twice — once for the query pool reset and once for the result copy — and neither end has
       anything to do with the attachments). The report prints them as `pases por motivo: destino | barrera |
       barrera vacia | otra | consultas de oclusion`, and `tools/perfil_consola.py` puts them in a table of their
       own. Two of the four are the port's own work rather than the guest's: `barrera` is the SDK's synchronization
       cutting the frame, and `vacia` is a caller ending a pass for nothing. If `consultas` turns out to be a large
       part of the passes, `occlusion_query_enable = false` in `cod2.toml` measures it without a rebuild — at the
       cost of the guest reading whatever the query left behind, which changes what the game culls.

       The reason a pass ended is not the same question as what the pass was for, and the second is the one that
       decides who can remove it. The two render pass entry points are counted apart (counters 42–43 for the
       passes begun to transfer render target contents or clear a resolved region, 44–45 for the passes begun for
       the guest's own draws, each with the attachment pixels it covers, printed as `pases por clase: dibujados |
       transferencias`). The `dibujados` ones are the game's drawing and only drawing less removes them; the
       `transferencias` ones are the port materializing a render target so it can be sampled as a texture, which
       is work the Xbox 360 never did because its EDRAM made it free, and those *are* removable in the port — the
       SDK's own TODO above `PerformTransfersAndResolveClears` asks for exactly that ("perform all non-cross-copying
       transfers for the current framebuffer configuration in a single pass, to load / store only once"). The
       transfer line also separates the calls made by the guest's resolve command (counter 41) from those made by
       binding a render target as a texture, because the two have different fixes.

       **`barrera` is the reason to attack first, and the round-25 session showed how to.** It was 78.1 of the
       frame's 87.3 pass ends, and a pass end on a tile-based GPU stores the whole tile buffer and loads it back,
       so those 78.1 passes are round trips to memory that draw nothing. Vulkan does forbid a barrier that
       includes a non-graphics stage inside a pass begun with `vkCmdBeginRendering`
       (VUID-vkCmdPipelineBarrier2-None-09553/-09554), which is why `SubmitBarriers` ended the pass for every
       barrier; but most of these barriers were not that kind. `VulkanSharedMemory::Use` pushes one whenever the
       guest's use of the shared memory buffer changes between reading and writing, and this guest alternates
       memexport-capable draws with draws that only read it, so that is roughly once per draw - and because
       `GetUsageMasks` gave *every* `kRead` the `COMPUTE` and `TRANSFER` stages on the assumption that any read
       might be a compute dispatch or a buffer copy, every one of those draw-to-draw barriers looked illegal. A
       draw's read is neither. `Usage::kGuestDrawRead` is the same mask without those two stages, used only where
       the caller is a guest draw, and `cod2_barreras_en_pase` then records the batch inside the pass when it is
       provably legal: every barrier's two stage masks non-zero and inside the graphics stages, and no image
       barrier in the batch at all (an image barrier inside a dynamic rendering pass may not touch an attachment,
       and the SDK pushes those for the guest's own render-target-as-texture ordering). A transfer, a resolve, a
       compute write, an image barrier or a caller that asked for the end still ends the pass exactly as before,
       and on a device without `vertexPipelineStoresAndAtomics` - where a memexport-capable vertex shader becomes
       a compute shader - `kGuestDrawRead` keeps `COMPUTE` and `TRANSFER` rather than promising something untrue.
       It is a cvar, so the old behaviour is one line away, and the number to watch is
       `pases por motivo: barrera`: if it does not drop, no batch was eligible and the change is inert.

       **What that fix can be worth, before anyone expects 30 fps from it.** All 87.3 pass ends and every barrier
       batch are still recorded in `sin nada`, and the GPU busy time there is 24.5 ms of a 34.5 ms frame - so the
       pass and synchronisation structure costs *at least* 24.5 ms of a 169.5 ms frame, which is what it costs when
       the passes run back to back with nothing between them. Even at that floor, removing every one of them would
       take 5.9 fps to 6.9. So this is a few per cent and not a multiple: a cost the frame pays for nothing, worth
       shipping, not the lever. The 87.4 ms the guest's own draws cost is the lever, and whether that is the game's
       shading or the emulation path's per-draw structure is what `sin pixels (vista de 1 pixel)` measures.

       The arithmetic to beat, and what it says about the plan. At 4.86 FPS a frame is 206 ms. One pass over a
       1280-wide target stores and loads 21 MB of attachment (10.5 of colour and 10.5 of depth) while the render
       area is the whole EDRAM allocation, and 7 MB once it is the guest's 1280×720; the handheld memory bus moves
       34.1 GB/s at the clocks this port asks for. So 141.9 × 42 MB / 34.1 GB/s = **175 ms** before the render area
       is narrowed and 141.9 × 14.7 MB / 34.1 GB/s = **61 ms** after. Two things follow. First, the render-area
       change of point 5 is worth up to about three times, and if the console says otherwise the change is not
       reaching the passes. Second, and more important, 61 ms is not 206 ms: **even with the pass traffic gone
       entirely the frame would be about 10 FPS, so the passes alone cannot be the whole story and the next round
       must not be chosen on the assumption that they are.** The two ways to find out which term is really the wall
       need no build: play the same spot with `cod2_area_de_dibujo_ajustada` at its default and then `false` and
       compare `area MB/f` against the frame rate, and hold L+R+ZL in game to make the profiler remove one part of
       the GPU work per report (`sin dibujados`, `sin transferencias`, `sin resolves`, `sin cargas de texturas`, …,
       `sin nada`), where `sin nada` against `normal` is the whole cost of the GPU work and a `sin nada` that is
       just as slow as `normal` means the cost is not in the work at all. `tools/perfil_consola.py` reads those
       reports in a table of their own. See [docs/measuring.md](measuring.md#splitting-a-frame-that-is-cut-into-render-passes);

       **The A/B answered, and it answered no.** `switch-logs/logs` (2026-10-12, the round-22 set: 13 reports, one
       per skip mode, 10 s each, in game) gives **4.3 FPS** for `normal` and, for the modes that did run, `sin
       dibujados` 9.0, `sin transferencias` 6.8, `sin resolves` **9.6**, `sin cargas de texturas` 6.4, `sin efecto
       de presentacion` 5.9 — the rotation stopped after five of the eight modes, so `sin nada` was never measured.
       Removing *all* of the resolve dispatches is the best single cut and it is **2.2×**, not 7×, and the CPU total
       stays at 258 % of 400 % in every mode, which is a total that counts blocked time too, so it is not the split
       to read (the GPU wait below is). The render area did what it was supposed to (`area 668.60 MB ÷ 93.5 passes = 7.15 MB`, i.e. 3.57 MB per
       store and 3.57 MB per load, which is the guest's 1280×698 and not the 10 MB EDRAM allocation), so the
       remaining lever on that axis is the pass count alone. It is 93.5, of which `pases por motivo` says `destino
       9.1 | barrera 83.9 | barrera vacia 0.0 | otra 0.5`, and `pases por clase` says `dibujados 76.1 (556.05 MB) |
       transferencias 17.4 (112.56 MB)`: 84 of the 93 passes are the port's own synchronization, and 81 % of the
       attachment traffic is the guest's drawing inside them. **The reason the passes cannot simply be merged is
       Vulkan, not the port**: with dynamic rendering, `vkCmdPipelineBarrier` must not be called inside a render
       pass instance unless the `dynamicRenderingLocalRead` and tile-image-read features are enabled
       (VUID-vkCmdPipelineBarrier-None-09553/09554), so every batch of pending barriers has to be recorded between
       two pass instances. NVK *does* advertise `VK_KHR_dynamic_rendering_local_read`
       (`mesa/mesa-switch/src/nouveau/vulkan/nvk_physical_device.c:144`, `:516`) and the port could enable it, but
       that does not help either: `VUID-vkCmdPipelineBarrier-srcStageMask-09556` requires that a barrier recorded
       inside a `vkCmdBeginRendering` instance specify **only framebuffer-space stages**, and every barrier here is
       transfer↔vertex-input or transfer↔compute, which are not framebuffer-space. And a pass cannot be suspended
       across one instead: `VUID-vkCmdPipelineBarrier-suspended` forbids recording it between suspended pass
       instances. `VK_RENDERING_SUSPENDING_BIT`/`VK_RENDERING_RESUMING_BIT` was rejected on top of that — NVK really
       does implement it (it skips the linear-tile store and the resolve on suspend, and the whole load/clear setup
       on resume: `mesa/mesa-switch/src/nouveau/vulkan/nvk_cmd_draw.c:1877`, `:1999`, `:2086`) — because the flag has
       to be set in the *begin* info, so a pass would have to be suspended before knowing whether it can be
       resumed, an abandoned suspension silently drops the store and the MSAA resolve, and resuming after a compute
       dispatch gives up the rasterization-order guarantee between the two halves of the pass, which is what the
       barrier was there to replace. **The pass count is pinned by the Vulkan barrier rules, not by the port.**
       Counters 46–48
       (`barrera por contenido: memoria compartida entera | solo imagenes | otros buffers`) now say which of the 84
       is which, because only two of the three can be moved from the port side;

       **The other half of the frame is the guest's own CPU, and one thread is the critical path.**
       `XThreadB513080` runs at 90.4 % of a core and `Main XThread` at 88.2 %, and `Main XThread` spends **90.6 % of
       its samples in a single instruction** — the spin at `sub_823800D8+0x510` (`ldr w1,[x0]; cbnz w1,-4` over the
       guest lock word at `0x83631030`, `-2090663936 + 3672 + 472`, which is `loc_82380114` in
       `app/generated/default/cod2_recomp.44.cpp`), verified in the built NRO at host offset `0x28f0`, so clang did
       not hoist the load. It is waiting for `XThreadB513080`, which is inside the guest's own lock manager
       (`sub_823744E8` → `sub_8237FE40` release / `sub_8236D0D0` acquire), and that thread's time is 38 % in
       `sub_8237FE40`, 14 % in `sub_820A5E78`, 10 % in `sub_8236D0D0` and about **25 % in
       `KeGetCurrentProcessType_entry` and its `HostToGuestFunction` wrapper** — `sub_820A5E78` calls it once per
       lock operation. Two changes in `sdk/src/kernel/xboxkrnl/xboxkrnl_threading.cpp` attack that: the guest's
       `KeGetCurrentProcessType` is now a raw hook (`REX_FUNC(__imp__KeGetCurrentProcessType)`) that reads the PCR
       address straight out of `ctx.r13`, which is what the caller already handed it — `REX_EXPORT` threw the
       context away and `HostToGuestFunction` rebuilt it from the thread-local `ThreadState`, three dependent
       pointer chases for a register in hand, plus the `XThread::GetCurrentThread()` call; and the two
       `Memory::TranslateVirtual` calls per invocation are cached per host thread, because the translation is a
       pure function of the guest address (see the comment on `cod2_traducir_proceso_cacheado` in that file).
       `direct_host_resolve`, which is on by default in the SDK and whose `TryResolveCopyDirectly` ends in the same
       `DumpRenderTargets` the flag-off path calls, is now off in `app/src/cod2_rendimiento.cpp`: it made every one
       of the frame's 33.7 resolves do its rectangle and pipeline work twice;

       **The native renderer was parked because of its hooks, and it is no longer parked.** The address audit is
       real and it still stands: of the **205 distinct `sub_XXXXXXXX` addresses** that appeared in `app/src/aparcado/`
       and the two TOML tables, **203 do not exist in Call of Duty 2's 20,944 generated functions** (the two that do,
       `sub_821CF770` and `sub_8245DD60`, are about audio and shadows, not rendering). What that measures, though, is
       the *hooks* — the places where the previous port reached into its own game — and not the renderer. The core is
       about the Xbox 360's hardware: the PM4 ring, the Xenos register file, the EDRAM, the Vulkan device, the texture
       pool. That hardware is the same in both games. Measured before unparking it: **not one of the 22 files contains
       a real guest-address reference** — no `REX_EXTERN(`, no `REX_HOOK`, no `__imp__sub_`; all 37 textual
       `sub_XXXXXXXX` occurrences are in comments and log strings.

       So the core is now compiled, and `cod2_renderizador = "nativo"` starts it instead of the Xenos emulation
       (`app/src/cod2_app.h`'s `OnPreSetup` fills `config.graphics`, which is the SDK's own substitution point —
       `ReXApp::SetupPresentation` only loads the plugin when it is empty, `sdk/src/ui/rex_app.cpp:342`). What is left
       is the part the audit was actually about: re-deriving the hooks that tell the renderer which shader each draw
       uses. [docs/inventario-renderer-nativo.md](docs/inventario-renderer-nativo.md) has the addresses for those,
       found in this game's own generated code. Nothing here is a reason to conclude that 30 fps at 720p is out of
       reach; what it is, is a reason the hooks had to be found again before any of it could draw.

       **The GPU is where the frame is, and the port's own resolve machinery is the largest single piece of it.**
       `libnx por segundo` reports how much of every second is spent blocked inside `nvFenceWait` with a non-zero
       timeout — waiting for the GPU, timed directly, as opposed to the zero-timeout query that shares its name —
       and in the round-22 in-game report that is 595.8 ms of every second: **59.6 % of the wall time**, or 138.6 ms
       of a 232.6 ms frame. The A/B then says which part of the GPU work it is, per frame: `sin dibujados` (the 175
       draws) takes the GPU wait to 81.0 ms and the frame to 111 ms, `sin resolves` takes it to 67.7 ms and the frame
       to 104 ms, `sin transferencias` to 111.4 ms, `sin cargas de texturas` to 120.3 ms, `sin efecto de
       presentacion` to 133.7 ms. So the guest's draws are about 58 ms of GPU per frame and the port's 33.7 resolves
       about 71 ms, and **the resolve machinery — the one item here that is not the game's own work — is the largest
       single thing in the frame**. The remainder of the frame is not a fixed CPU cost either: the part that is not
       the measured GPU wait is 94 ms in `normal` and only 30-36 ms in *every* skip mode, so it is other GPU-related
       blocking plus CPU work, not a floor that stays put when the GPU gets faster.

       **That makes `sin nada` — every category of GPU work removed at once — the one number that decides whether
       30 fps is reachable at all, and it has never been measured.** 30 fps is 33.3 ms per frame, a 7× cut, and the
       largest single lever measured is 2.2×. If `sin nada` lands near 35 ms, the GPU work is the whole story and
       cutting it several times over is the way there; if it lands near 90 ms, then something outside the GPU work
       is the floor — the guest's own threads, one of which spends 90.6 % of its samples in a guest spin on a guest
       lock word — and 30 fps at 720p is out of reach on this hardware whatever the renderer is. The A/B rotation in
       `sdk/src/ui/switch_perf.cpp` was reordered to put `sin nada` **second** and to make each report five seconds,
       so that answer arrives ten seconds into a session instead of never; until it exists, any claim about where
       the frame's time really goes is a guess, this paragraph included. The one piece of the parked renderer that
       *would* pay is the direct resolve the SDK itself left parked: `TryResolveCopyDirectly` is a preflight that
       ends in `DumpRenderTargets`, `GetDirectResolvePipeline` hands back the EDRAM-reading copy shader, and the
       fused "host image → shared memory in one dispatch" shader was never written. It attacks the 71 ms item
       above and is worth the dump compute and one of the two round trips per resolve;

       **The port now measures how long the GPU is busy, instead of inferring it from how long the guest waited.**
       The fence wait above is a lower bound and it cannot separate the two shapes that look identical from the
       guest's side: a GPU that is the wall, and a guest and a GPU that take turns and each waits for the other. A
       timestamp query written at `TOP_OF_PIPE` as the first command of every submission and at `BOTTOM_OF_PIPE` as
       the last one measures the second shape directly, and the report's new line —
       `gpu: N ms por fotograma ocupada, M % del tiempo de reloj` — is that number per presented frame and as a
       share of the wall clock, next to `si fuera el limite, el juego iria a N fps`, which is what the frame rate
       would be if the GPU work were the only thing in the frame. The measurement cannot change what it measures:
       the readback is non-blocking (`VK_QUERY_RESULT_WITH_AVAILABILITY_BIT`), eight slots are rotated so a query is
       never reset while a submission may still be writing it, a slot is only counted once its submission really
       reached the queue, and the writer stops rather than lap the reader. `cod2_marca_escala` (default 1.627) is the
       conversion from timestamp units to nanoseconds: NVK reports `timestampPeriod = 1 ns` and it is wrong — the
       counter runs at the SoC's fixed 614.4 MHz, so one unit is 1.627 ns, which is what makes the share of the wall
       clock come out under 100 % instead of 39 % low. A share above 100 % means the scale is too large — but a
       couple of points above it does not, because the figure is a submission's span attributed to a presented
       frame and `normal` runs 1.5 submissions per frame, so a frame boundary lands inside a span; what says the
       scale is right is that `si fuera el limite` comes out at the measured frame rate (5.7 against 5.9 fps in the
       round-25 session) instead of 1.6x off it.
       [docs/measuring.md](measuring.md#gpu-timestamps-on-nvk-under-horizon) has the rest;
       **The measurement came back in the round-25 session: the GPU work is the whole frame, and the A/B deltas are
       the budget.** In game, `normal`, 5.9 fps: the GPU is busy **175.2 ms of a 169.5 ms frame - 103 % of the wall
       clock** - and `si fuera el limite` says 5.7 fps. The guest is not the wall (its fence wait is 133.9 ms of the
       frame, and its own work the rest), and neither is the port's structure: `sin nada`, which removes every
       category of GPU work at once and leaves the 89.3 render pass ends and the ~277 barrier batches in place, runs
       at **29.0 fps with the GPU busy 24.5 ms of a 34.5 ms frame**. So the floor under everything the port does on
       its own is 24.5 ms, and removing all of it would take 5.9 fps to 6.9. The A/B deltas say where the rest is, in
       GPU milliseconds per frame: **the guest's own draws 87.4**, resolves 63.2, transfers 25.0, texture loads 20.8,
       the presentation effect 4.8.

       **What those 87.4 ms are is the open question, and it is not the same question as "how many pixels does the
       game shade".** They are the cost of the guest's draws *as this port's emulation path executes them*: each draw
       is preceded by a shared memory barrier that ends the render pass, so a draw pays for a store and a load of the
       whole render area and for a pipeline drain on top of its own shading, and the resolve and transfer machinery
       the Xbox 360 never needed is charged separately at 63.2 ms. The port of Need for Speed: Most Wanted on this
       same console, at this same 720p, went from 1-3 fps to 32-35 fps by **deleting that path rather than optimising
       it** - its renderer reads the game's own Direct3D command ring and records its own Vulkan draws
       (`docs/performance-history.md`, `docs/native-renderer.md`), and it never touches
       `sdk/src/graphics/vulkan/` at all. Its GPU work was 60.2 ms per frame when the native renderer first ran at
       stock clocks, and the changes that brought it to 33 ms and then 23 ms are the ones this port cannot make from
       inside the emulation: one pass instead of tiling, resolves by swapping images instead of copying, constants
       through a dynamic uniform buffer. **So "30 fps is out of reach on this hardware" was written before that
       comparison was made and does not follow from these numbers.** What follows is only that no amount of cutting
       inside the emulation path reaches 33 ms, which is what the barrier fix above is worth a few per cent of.

       The measurement that settles it is the one already built: `sin pixels (vista de 1 pixel)` keeps every draw
       with the same pipeline, the same barriers and the same render area and shrinks the viewport to a single pixel.
       If the frame time barely moves, the 87.4 ms is the per-draw structure and the emulation path is the thing to
       replace, not to trim. If it collapses towards `sin nada`, the draws cost what their pixels cost and the
       question becomes whether the shading can be cut. `sin barrera de memoria compartida` separates the barrier
       command from the pass ends it used to cause, which is the same question asked from the other side;

    6. the guest busy-waits, once the GPU work fits;
    7. function ordering (done): `app/orden_funciones.ld` held 555 of the other game's addresses and matched nothing.
       It now holds Call of Duty 2's, regenerated with `tools/calientes.py` from `switch-logs/logs-old-1` — the
       session that reaches a level — against the ELF of the same build. Getting there needed two silent failures
       fixed: the linker matches by section name and ignores what it does not find, and a recompiled function's
       body is not always in the section named after the symbol the profile reports (a hook in `app/src` gives the
       bare name a body of its own). The tool now writes both names and checks each against the ELF's symbol table,
       and `tools/comprobar_orden.py` reads the link map to confirm the entries landed: 1009 of 1015 functions are
       in the hot region, against 388 of 1005 for the inherited list. It is the one change of the previous port's
       step 6 that needed no console session of its own.

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
