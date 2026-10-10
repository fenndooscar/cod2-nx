# The graphics driver (Mesa and NVK)

## In short

- The Switch has no Vulkan driver that homebrew can use, so this port carries its own inside the NRO: **NVK**, the
  open source Vulkan driver for NVIDIA GPUs, which is part of the **Mesa** project.
- It comes from [danfromtico/mesa-switch](https://github.com/danfromtico/mesa-switch), a version of Mesa 26.2.1 that
  runs on the Switch's operating system (called Horizon).
- This port adds its own changes on top. They are all in one file of this folder: `mesa-switch-cod2.patch`.
- Building Mesa gives a library, `libvulkan.a`, and the NRO includes it.

## What you have to install first

`build-unified.sh` refuses to start unless `MSYSTEM` is set (it has to be run from an MSYS2 shell, not from Git Bash or
PowerShell), and it runs `meson`, `ninja`, `python`, `sed` and `git` **by name**. On top of that, the cross files it
passes to Meson name the compilers and tools by absolute path, so they have to exist exactly there.

From `switch_cross_file_msys2.txt`, `switch_native_tools_msys2.txt` and `windows_x64_host_cross_file_msys2.txt` in the
mesa-switch tree. The **State** column is what was actually found on the machine this port is being prepared on, not
what the files assume. Every row is verified; the two that are not "ok" are unused by Mesa, which is explained below.

| Path | What it is | State |
|---|---|---|
| `C:/msys64/clang64/bin/clang.exe`, `clang++.exe`, `llvm-ar.exe`, `llvm-strip.exe`, `llvm-nm.exe`, `llvm-config.exe`, `pkg-config.exe` | The **host** compiler that builds Mesa's own build tools (the shader compiler `mesa_clc` and `vtn_bindgen2`). These are the CLANG64 environment's, not MINGW64's. | ok (clang 22.1.8) |
| `C:/msys64/clang64/bin/cmake.exe` | Declared in the host cross file. MSYS2 keeps `cmake` in its `msys` repo; `clang64/mingw-w64-clang-x86_64-cmake` does exist if it is ever needed. | missing — probably unused |
| `C:/msys64/clangarm64/bin/cbindgen.exe` | Declared in the native file, but no `rust.cbindgen()` call exists in Mesa's `meson.build`, so Meson never resolves it. CLANGARM64 is the **Windows on ARM** environment. | irrelevant on x86-64 — see below |
| `C:/msys64/usr/bin/bash.exe`, `sed.exe`, `flex.exe`, `bison.exe` | Run the wrapper scripts and the lexer/parser generators | ok |
| `C:/msys64/ucrt64/bin/gcc.exe` (or `mingw64`, or `clang64`) | A native compiler, only to link the small programs Meson uses to probe the Rust compiler. This port's patch tries all three; the original only tried the first. `rustc-native-msys2-wrapper.sh` also hardcodes the `ucrt64` one as the proc-macro linker. | ok (gcc 16.2.0) |
| a `bindgen` binary on the CLANG64 `PATH` | `bindgen-switch-wrapper.sh` looks for it in `MESA_SWITCH_BINDGEN`, then `/root/.cargo/bin/bindgen`, then `/clangarm64/bin/bindgen.exe`, then `command -v bindgen`. NVK's Rust needs it to read the C headers. | ok: `/clang64/bin/bindgen` |
| `SPIRV-Tools` and `LLVMSPIRVLib` on the CLANG64 `pkg-config` path | The host build passes `-Dmesa-clc=enabled`, which makes `with_clc` true and turns both into required dependencies (`LLVMSPIRVLib` `>= 22.1, < 22.2`; `SPIRV-Tools` `>= 2024.1`). | ok: 22.1.0.0 and 2026.3.1 |
| **devkitPro at `C:/msys64/opt/devkitpro`** | `switch_cross_file_msys2.txt` hardcodes the whole devkitA64 toolchain under `C:/msys64/opt/devkitpro/...`, and the bindgen wrapper's include paths too. A devkitPro installed at `C:/devkitPro` is **not** found on its own. | done via a junction |
| a Rust toolchain with the **`aarch64-unknown-linux-gnu`** target | NVK's shader compiler (NAK) is Rust, and it is cross-compiled for the Switch. `rustup` does not include that target by default. | ok on both toolchains |
| **no `clangarm64` packages** | That environment is ARM64 code; on an x86-64 host every binary in it fails with `Exec format error`, and the wrapper picks it by executable bit alone. | ok: 0 packages, directory gone — see below |

In the MSYS2 **CLANG64** shell, the host tools come from one group:

```sh
pacman -S --needed mingw-w64-clang-x86_64-toolchain \
  mingw-w64-clang-x86_64-llvm mingw-w64-clang-x86_64-meson \
  mingw-w64-clang-x86_64-pkgconf mingw-w64-clang-x86_64-ninja \
  mingw-w64-clang-x86_64-python mingw-w64-clang-x86_64-git \
  mingw-w64-clang-x86_64-rust-bindgen
```

Note the `x86_64` in every name. `mingw-w64-clang-x86_64-git` is not optional: a plain CLANG64 shell has no `git` on
`PATH` unless the package is installed or the shell was started with `-use-full-path`.

The `llvm` package matters for a second reason: this port's patch adds `-Dshared-llvm=enabled` to the host build, so it
links against MSYS2's shared LLVM instead of building LLVM from source (which takes hours and does not fit in 16 GB).
That is also why the patch adds `'demangle'` to the LLVM modules list — with a shared LLVM, `llvm::demangle` lives in
its own module and the link fails without it.

### The CLANGARM64 trap (read this first)

The mesa-switch MSYS2 files were written on **Windows on ARM**. `host-tool-msys2-wrapper.sh` opens with "Run an x64
Mesa host tool from an **ARM64 MSYS2 build**", and `switch_native_tools_msys2.txt` points `cbindgen` at
`C:/msys64/clangarm64/bin/`. On such a machine `clangarm64` is the native environment and `clang64` runs under
emulation, so both work.

On an **x86-64** machine they do not. `clangarm64` is ARM64 code, and Windows on x86-64 has no emulation for it: every
binary there fails with `cannot execute binary file: Exec format error`. The trouble is that
`bindgen-switch-wrapper.sh` chooses with `[ -x /clangarm64/bin/bindgen.exe ]` — the executable bit, **not** the
architecture:

```sh
if [ -x /clangarm64/bin/clang.exe ]; then
    CLANG_BIN=/clangarm64/bin/clang.exe
```

So *installing* the ARM64 packages silently breaks the build: the wrapper picks `/clangarm64/bin/bindgen.exe` and
`/clangarm64/bin/clang.exe`, and `"$CLANG_BIN" -print-resource-dir` fails before bindgen is ever called. If you ever
install them, remove them again:

```sh
pacman -Rns $(pacman -Qq | grep aarch64)
```

With `clangarm64` gone, both `if` tests fall through to their `else` branches, which are the correct x86-64 ones:
`CLANG_BIN="$(command -v clang)"` resolves to `/clang64/bin/clang` (resource dir `C:/msys64/clang64/lib/clang/22`), and
the include paths fall back to `${DEVKITPRO:-/opt/devkitpro}`, which reaches the junction. Nothing else needs the
ARM64 environment. **This is the state on this machine now**: `pacman -Qq | grep -c aarch64` answers 0 and
`/clangarm64` no longer exists.

The rule for this build: every package name must contain `x86_64`, never `aarch64`.

### What has been checked, and how

Every line below was verified by running something, not by reading a package list.

| Item | Check | Result |
|---|---|---|
| Host compilers | `/clang64/bin/{clang,clang++,llvm-ar,llvm-strip,llvm-nm,llvm-config,pkg-config}.exe` exist and run | clang/LLVM 22.1.8 |
| Host build tools | `command -v meson ninja python sed git` in a **plain** CLANG64 shell | all five resolve |
| devkitPro | `/opt/devkitpro/devkitA64/bin/aarch64-none-elf-{gcc,g++,gcc-ar,nm,strip}.exe`, `.../include`, `libnx/{include,lib}` | all present via the junction; g++ reports 16.1.0 |
| `bindgen` | `command -v bindgen` in a plain CLANG64 shell | `/clang64/bin/bindgen` |
| `bindgen` actually works | ran it on a header that `#include <switch.h>`, with the wrapper's injected `--target=aarch64-none-elf` and `-isystem` paths | emitted correct Rust bindings for devkitA64/libnx types |
| `cbindgen` | `command -v cbindgen` | `/clang64/bin/cbindgen` (declared but unused by Mesa) |
| SPIRV deps | `pkg-config --exists LLVMSPIRVLib SPIRV-Tools` | 22.1.0.0 and 2026.3.1, both inside Mesa's gates (`>= 22.1, < 22.2` and `>= 2024.1`) |
| SPIRV libraries | `libLLVMSPIRVLib.a`/`.dll.a`, `libSPIRV-Tools*.a` in `/clang64/lib` | present |
| `dep_clang` | `/clang64/lib/libclang-cpp.dll.a` | present, so `-Dshared-llvm=enabled` takes the shared branch |
| Native probe linker | `/ucrt64/bin/gcc.exe` | gcc 16.2.0 |
| Rust cross-compile | compiled an aarch64 `--emit=metadata` crate with both toolchains | both succeed |
| Rust host probe | linked a native program with `-C linker=C:/msys64/ucrt64/bin/gcc.exe` | GNU toolchain: yes. MSVC: no |
| `clangarm64` | `pacman -Qq \| grep -c aarch64`, `test -d /clangarm64` | 0 packages, directory gone |
| Wrapper branch selection | replayed `bindgen-switch-wrapper.sh`'s `if`/`elif` chain | picks `/clang64/bin/bindgen` + `/clang64/bin/clang`, resource dir `C:/msys64/clang64/lib/clang/22` |

### What is still missing

Nothing. **`build-unified.sh` has been run on this machine and produced a complete SDK**, and the NRO links against it.
The three items below are the ones that had to be solved to get there, kept because each one fails silently or with a
message that points somewhere else.

1. **`MESA_SWITCH_RUSTC`, pointing at the GNU toolchain's `rustc` directly.**

   `build-unified.sh` refuses to start unless one of three things exists: `$MESA_SWITCH_RUSTC`, the Docker path
   `/root/.cargo/bin/rustc`, or a cached toolchain at `build/deps/{cargo,rustup}`. Neither of the last two exists on
   Windows, so `MESA_SWITCH_RUSTC` has to be set. (The script itself only *checks* that the path is executable; the two
   wrapper scripts read the variable and do the real work.)

   The obvious value, the rustup proxy `~/.cargo/bin/rustc.exe`, is **not** enough on its own: the proxy follows the
   *default* toolchain, which is `stable-x86_64-pc-windows-msvc`, and MSVC rustc cannot link the small host program
   Meson uses to probe the compiler — it hands `/NOLOGO`, `/OUT:` and `kernel32.lib` to the `ucrt64` gcc that
   `rustc-switch-wrapper.sh` hardcodes as the fallback linker. Verified both ways: MSVC toolchain → link fails, GNU
   toolchain → link succeeds.

   So point the variable straight at the GNU toolchain's binary. That skips the proxy and its default-toolchain lookup
   entirely, which means `RUSTUP_TOOLCHAIN` is not needed at all:

   ```
   MESA_SWITCH_RUSTC = C:\Users\<you>\.rustup\toolchains\stable-x86_64-pc-windows-gnu\bin\rustc.exe
   ```

   Set it once as a Windows **user** environment variable — MSYS2 inherits the Windows environment, so both the MSYS2
   shell and anything launched from it see it. Both path spellings work in MSYS2 (`C:\...` and `/c/...`).

   Verified with no `RUSTUP_TOOLCHAIN` set anywhere: `[ -x "$MESA_SWITCH_RUSTC" ]` passes, an
   `--target=aarch64-unknown-linux-gnu` metadata build succeeds (the cross path), and a native link through
   `-C linker=C:/msys64/ucrt64/bin/gcc.exe` succeeds (the host-probe path).

   **Do not set `RUSTUP_TOOLCHAIN` globally instead.** It is a rustup-wide override — every `cargo` and `rustc`
   invocation on the machine would move to the GNU toolchain, including unrelated projects. `build_mesa_msys2.sh`
   already pins it internally for its own run (`export RUSTUP_TOOLCHAIN=${RUSTUP_TOOLCHAIN:-stable-x86_64-pc-windows-gnu}`),
   so only `build-unified.sh` ever needed it, and the direct path above covers that without the side effect.

   The alternative, if you prefer to keep the proxy: export both `MESA_SWITCH_RUSTC=/c/Users/<you>/.cargo/bin/rustc.exe`
   and `RUSTUP_TOOLCHAIN=stable-x86_64-pc-windows-gnu` in the shell you build from.

   Both toolchains also need the cross target, or the build stops at the first Rust object:

   ```sh
   rustup target add aarch64-unknown-linux-gnu
   rustup target add aarch64-unknown-linux-gnu --toolchain stable-x86_64-pc-windows-gnu
   ```

   Nothing else in the package list is required. `C:/msys64/clang64/bin/cmake.exe` is declared in the host cross file
   but nothing in the Mesa build calls it, and `C:/msys64/clangarm64/bin/cbindgen.exe` is declared in the native file
   but no `rust.cbindgen()` call exists in Mesa's `meson.build`, so Meson never resolves either.

2. **`MESA_SWITCH_RUST_TARGET` on every `ninja` run that is not `build-unified.sh`.**

   `rustc-switch-wrapper.sh` reads it (`RUST_TARGET=${MESA_SWITCH_RUST_TARGET:-}`) and **silently drops `--target`
   when it is empty**. `build-unified.sh` exports it (`export MESA_SWITCH_RUST_TARGET="${...:-aarch64-unknown-linux-gnu}"`),
   so a full run through that script is fine — but the fast iteration path, `ninja -C builddir-unified`, inherits
   nothing and compiles NAK for the **host**:

   ```
   error[E0461]: couldn't find crate `bitview` with expected target triple x86_64-pc-windows-gnu
   error[E0432]: unresolved import `RegLatencySM100`
   ```

   Those 200-odd errors are one missing variable, not a broken build. Export it before any `ninja`:

   ```sh
   export MESA_SWITCH_RUST_TARGET=aarch64-unknown-linux-gnu
   ```

3. **`switch-libdrm_nouveau`, which is not installable and has to be built from source.**

   The Switch build of Mesa's `nouveau` winsys needs the portlib's flat `nouveau.h`, which defines
   `struct nouveau_list` — the list type `nouveau_switch_libdrm.h` and `nouveau_switch_pushbuf.c` are written against,
   and which exists in **no** header of the mesa-switch tree. Without it:

   ```
   nouveau_switch_libdrm.h:200:8: error: invalid use of undefined type 'struct nouveau_list'
   ```

   `pacman -S switch-libdrm_nouveau` cannot fetch it here: `pkg.devkitpro.org` answers the MSYS2 and the Windows curl
   alike with a Cloudflare `403 Sorry, you have been blocked` page. Build it from source instead — the package is a
   plain Makefile over four `.c` files, and devkitPro ships its own MSYS2 with a working `make`:

   ```sh
   curl -sSkL -o ldn.tar.gz \
     https://github.com/devkitPro/libdrm_nouveau/archive/v1.0.1/libdrm_nouveau-src-1.0.1.tar.gz
   tar -xzf ldn.tar.gz
   cd libdrm_nouveau-1.0.1
   C:/devkitPro/msys2/usr/bin/bash.exe -lc \
     "cd '$PWD' && export DEVKITPRO=/opt/devkitpro && make install"
   ```

   That installs `include/nouveau.h`, `include/nouveau_drm.h`, `include/nvif/` and `lib/libdrm_nouveau.a` into
   `portlibs/switch`. Note that `C:/msys64/opt/devkitpro` is a junction to `C:/devkitPro`, so this is the same tree the
   MSYS2 build reads.

   Do **not** work around it by adding `include_directories('../drm')` to
   `src/gallium/winsys/nouveau/switch/meson.build` and pointing the header at `src/gallium/winsys/nouveau/drm/nouveau.h`.
   That resolves `struct nouveau_list` to a *different* struct with a *different* layout (upstream's `list_head`-based
   one), and the two list implementations would be mixed at runtime — the objects would be linked into the wrong list.
   It compiles; it is wrong.

### zstd is not linked any more

`sdk/cmake/rexglue_switch.cmake` used to name `zstd` in `target_link_libraries`, taken from Mesa's own
`VulkanConfig.cmake`. devkitPro does not package `switch-libzstd`, so the NRO link stopped at:

```
ld.exe: cannot find -lzstd
```

Nothing in the link needs it. `nm --undefined-only` over every archive of `sdk/out/switch-arm64` and over the Mesa
SDK's `libvulkan.a`, `libEGL.a`, `libnvk.a`, `libnil.a` and `libmesa_util.a` reports **zero** undefined `ZSTD_*`
symbols. The name is only there because Mesa's config lists it and this port does not use `find_package(Vulkan)` (see
the comment in `rexglue_switch.cmake`: the Mesa config wants `libelf`, which devkitPro also does not package).


## Getting the same driver as the released NROs

1. Download mesa-switch and go to the version this port started from (commit `1a8c1a66d6f`):

   ```sh
   git clone https://github.com/danfromtico/mesa-switch.git
   cd mesa-switch
   git checkout 1a8c1a66d6f
   ```

2. Apply this port's changes. After this you have exactly the driver source the released NROs were built with
   (35 changed files):

   ```sh
   git apply ../cod2-nx/mesa/mesa-switch-cod2.patch
   ```

   If it answers `patch does not apply` for every file at once, the patch arrived with Windows line endings
   (`\r\n`) instead of Unix ones. `git apply` compares context lines byte by byte, so a trailing `\r` makes
   every hunk miss, and the target checkout has LF. `.gitattributes` marks `*.patch` as `-text` so a
   checkout no longer rewrites it; if yours was fetched before that line existed, convert it first:

   ```sh
   python -c "p='mesa-switch-cod2.patch'; d=open(p,'rb').read(); open(p,'wb').write(d.replace(b'\r\n', b'\n'))"
   ```

   A single `git apply` is all or nothing: it checks every hunk before writing any of them, so a failure
   leaves the checkout untouched and there is nothing to undo.

3. Build it. The first time, use mesa-switch's own script, `build-unified.sh`. **Set `ALLOW_DIRTY=1`**, or it refuses to
   start: right after `git apply` the checkout is dirty, and the script exits 2 with "Refusing to create a release SDK
   from a dirty checkout". (Committing the patch instead works too.)

   ```sh
   export MESA_SWITCH_RUSTC="$USERPROFILE/.rustup/toolchains/stable-x86_64-pc-windows-gnu/bin/rustc.exe"
   ALLOW_DIRTY=1 ./build-unified.sh
   ```

   From PowerShell, with the same environment the MSYS2 shell needs:

   ```powershell
   $env:MSYSTEM = 'CLANG64'
   $env:MESA_SWITCH_RUSTC = "$env:USERPROFILE\.rustup\toolchains\stable-x86_64-pc-windows-gnu\bin\rustc.exe"
   $env:MESA_SWITCH_RUST_TARGET = 'aarch64-unknown-linux-gnu'
   & C:\msys64\usr\bin\bash.exe -lc "cd /c/.../mesa-switch && ALLOW_DIRTY=1 ./build-unified.sh"
   ```

   Later rebuilds are quicker with `ninja -C builddir-unified` (remember `MESA_SWITCH_RUST_TARGET`), and the SDK is
   staged with:

   ```sh
   meson install -C builddir-unified --destdir "$PWD/mesa-unified-install"
   ```

   `--destdir` must be an absolute POSIX path. A relative one is taken as relative to the **build directory**, so
   `--destdir $PWD/mesa-unified-install` from a Windows shell that has already mangled `$PWD` lands the tree at
   `builddir-unified/Users…/mesa-unified-install`. Quote it and let MSYS2 expand it.

   Verified end to end on this machine: the build reaches `[143/143]`, the SDK stages with all of `libEGL.a`,
   `libGL.a`, `libGLESv1_CM.a`, `libGLESv2.a`, `libglapi.a` and `libvulkan.a` (87 MB), and `tools/build.ps1` then
   links `app/out/sw8/cod2.nro` (44 MB).

4. Tell the app where that SDK folder is when you configure it:
   `-DREXGLUE_SWITCH_NVK_SDK=<sdk>/opt/devkitpro/portlibs/switch`. The whole process is in
   [docs/building.md](../docs/building.md).

## What the patch changes

One line per change. Why each one was needed, and how much it gained, is in [docs/mesa.md](../docs/mesa.md).

| Change | What it does | Files |
|---|---|---|
| ZCULL | The GPU skips pixels hidden behind others before painting them. A big saving of GPU time. | `nouveau_horizon*.c/h`, `nvkmd_switch_pdev.c`, `nvkmd_switch_dev.c`, `nvk_image.c` |
| Faster draws | Less CPU work inside the driver for each draw the game makes. | `nvk_cmd_draw.c`, `nvk_cmd_buffer.c/h`, `vk_pipeline.c/h` |
| Set 4 by differences | For each draw, only the game constants that changed since the previous draw are sent (the game keeps them in descriptor set 4). | `nvk_cmd_buffer.c/h`, `nvk_cmd_draw.c` |
| Memory without CPU cache | Memory the CPU only writes to (commands, uploads) skips the CPU cache, which is cheaper on the Switch. | `nvk_device.c/h`, `nvk_cmd_pool.c`, `nvk_mem_stream.c` |
| Copy engine | Buffer copies use the GPU's copy unit instead of a small shader. Off unless you turn it on. | `nvk_cmd_meta.c` |
| Uniform buffers | Shaders read their uniform buffers through the GPU's constant memory. The upstream way hung the GPU on some shaders; this way they work and come out smaller. | `nvk_nir_lower_descriptors.c` |
| Shader compiler (NAK) | Instructions are ordered for how slow the Switch GPU's memory is, small branches become straight code, and a wrong result of one instruction on this GPU generation is fixed (it made the sea black). | `opt_instr_sched_common.rs`, `nak_nir.c`, `sm50.rs` |
| Shader cache | Shaders compiled before these compiler changes are not reused. | `nvk_shader.c` |
| Newer mesa-switch fixes | Fixes taken from later commits of danfromtico/mesa-switch. | `nvk_mem_arena.*`, `nvk_descriptor_table.*`, `nvk_descriptor_set.c`, `nvk_query_pool.c`, `nvk_heap.h`, `nvk_queue.c` |
| Build | Makes it build on Windows, in MSYS2. | `build-*.sh`, `rustc-*-wrapper.sh`, `bindgen-switch-wrapper.sh`, `meson.build` |

## Which changes only affect the Switch

If you build this source for another platform, for example to compare with a PC:

- **Only on the Switch, because the files only exist there:** `src/nouveau/horizon/` and
  `src/nouveau/vulkan/nvkmd/switch/`. ZCULL is here.
- **Only on the Switch, because the code is inside `#ifdef HAVE_SWITCH_PLATFORM` blocks:** the faster draws, the set 4
  by differences and a structure the driver shares with the app to measure them. They are in `nvk_cmd_buffer.c/h`,
  `nvk_cmd_draw.c` and `vk_pipeline.c/h`.
- **On every platform:** the shader compiler changes, the newer mesa-switch fixes and the build changes.

### Where does `HAVE_SWITCH_PLATFORM` come from?

You will not find it written in the patch or in any header, because Mesa's build file, `meson.build`, creates it:

1. Every build has a list of platforms. When the build is for the Switch's system (`horizon`), the list is `switch`
   ([lines 478-479](https://github.com/danfromtico/mesa-switch/blob/1a8c1a66d6f/meson.build#L478-L479)).
2. For each platform in the list, it adds a flag called `HAVE_<PLATFORM>_PLATFORM`
   ([lines 620-622](https://github.com/danfromtico/mesa-switch/blob/1a8c1a66d6f/meson.build#L620-L622)). It is the
   same way Mesa gets `HAVE_X11_PLATFORM` or `HAVE_WAYLAND_PLATFORM` on Linux.
3. So a Switch build compiles every file with `HAVE_SWITCH_PLATFORM`, and the code inside those blocks is included.
   A PC build does not have the flag, and leaves that code out.

## Turning changes on and off

There are two ways:

- **The port's own settings.** ZCULL, the faster draws and the set 4 by differences are requested by the port, with
  settings in `cod2.toml` (also in the Debug Menu), all on: `cod2_nativo_zcull`, `cod2_nativo_set4_diferencias`
  and, for the faster draws, `cod2_nativo_nvk_emision`, `cod2_nativo_nvk_cbufs`, `cod2_nativo_nvk_dinamico` and
  `cod2_nativo_nvk_precarga`. The faster draws and the set 4 by differences also check themselves: on some draws they
  compare their result with the normal way, and if they ever see a difference, they turn off.
- **Environment variables for the driver.** You do not set them in Windows: write them in `cod2.toml`, in the setting
  `cod2_mesa_entorno`, separated by `;`, for example `cod2_mesa_entorno = "NVK_COPY_ENGINE=1;NVK_SHADER_STATS=1"`.
  Empty, as released, means the driver's normal behavior.

| Variable | Normally | What it does |
|---|---|---|
| `NVK_COPY_ENGINE` | off | `1` makes buffer copies use the copy unit. |
| `NVK_SWITCH_CPU_WRITE_MEM_UNCACHED` | on | `0` puts the CPU cache back on the memory the CPU only writes to, to compare. |
| `NVK_SWITCH_DYN_UBO_DELTA` | on | `false` turns off the set 4 by differences. |
| `NVK_SWITCH_NO_UBO_CBUF` | off | `1` goes back to the upstream way of reading uniform buffers. |
| `NVK_SHADER_STATS` | off | `1` writes to the log how many registers each shader uses. |
| `NVK_SUBTILING_KNOB` | the Switch GPU's value | A number, to try other values of this GPU tuning setting without rebuilding. Only for experiments. |

## After changing the driver

1. Rebuild and install it with `build_mesa_msys2.sh`, from an MSYS2 MINGW64 window.
2. Copy `libvulkan.a` (the shader compiler, NAK, is inside it), `libnvk.a` and `libnak_rs.a` to the SDK folder the app
   uses.
3. Delete `app/out/sw8/cod2` and `app/out/sw8/cod2.nro` before building the app. Otherwise the build does not notice
   that the driver changed, and the NRO keeps the old one.
4. If you changed the shader compiler (NAK), raise the revision number in `nvk_shader.c`. On the Switch that number is
   what tells old compiled shaders from new ones: without it, the game would keep using shaders compiled with the old
   compiler.
