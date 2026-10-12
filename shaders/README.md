# Shaders

This folder has the shader translator and the tools around it. Look here to rebuild the shader library, or to find
the code of one step of the translation. How the translation works, and why, is in
[docs/shaders.md](../docs/shaders.md). The command to rebuild the library is in step 7 of
[docs/building.md](../docs/building.md).

The game's [shaders](../docs/glossary.md#shader) (the small programs the GPU runs) are Xbox 360
([Xenos](../docs/glossary.md#xenos)) [microcode](../docs/glossary.md#microcode). They are stored in containers (the
files that hold them) — for this game, assets inside the `.ff` [fastfiles](../docs/glossary.md#fastfile), not loose
files. They are translated ahead of time into a
[SPIR-V](../docs/glossary.md#spir-v) library, `cod2_shaders.nfsp` (the
[shader library](../docs/glossary.md#shader-library)), which the renderer loads at startup. The steps are: find the
containers, translate the microcode to [HLSL](../docs/glossary.md#hlsl), compile the HLSL to SPIR-V with
[DXC](../docs/glossary.md#dxc), and pack the result into the library.

| File | What it does |
|---|---|
| `XenosRecomp/` | [XenosRecomp](https://github.com/hedge-dev/XenosRecomp) (by hedge-dev), with this port's changes under `COD2_RECOMP`: it translates microcode to HLSL. |
| `shader_common.h` | The HLSL helpers every translated shader includes (texture fetches, specialization constants). |
| `cod2_contenedor.h` ("container") | Reads the game's shader containers (the 2005 layout of this game). |
| `cod2_buscar_contenedores.cpp` ("find containers") | Finds shader containers in loose disc files. **Not the path this game uses**: see the section below. |
| `cod2_hlsl.cpp` | Translates every container of a folder to HLSL. |
| `cod2_empaquetar.cpp` ("pack") | Packs the SPIR-V into the library, keyed by a fingerprint of each container. |
| `cod2_lzx.cpp` | LZX decompression of the executable image (with libmspack), done the same way as the runtime does it. |
| `cod2_probar_contenedor.cpp`, `cod2_probar_biblioteca.cpp` ("test container", "test library") | Regression checks of the container reader and of a built library. |
| `cod2_regenerar_biblioteca.ps1` ("regenerate library") | **The whole pipeline on Windows**, from the `.ff` fastfiles to `cod2_shaders.nfsp`. |
| `cod2_regenerar_biblioteca_pcf.sh` ("regenerate library") | The same pipeline on bash, for the other game: it starts from a folder of containers and needs MinGW g++ and DXC. |
| `pch_min.h` | Precompiled header of the translator build. |
| `wasm/` | The WebAssembly builds (programs compiled to run in a browser) used by the installer page: `build_wasm_tools.bat` and `link_dxc_wasm.bat` build them, and `dxc_web.cpp` is the entry point of DXC in the browser. |

## The shaders are inside the fastfiles

Call of Duty 2 ships **no loose shader files**. The containers are assets inside the `.ff`
[fastfiles](../docs/glossary.md#fastfile), which are a 20-byte big-endian header followed by a single zlib stream
starting at `+0x14`. The second header dword is the inflated size, and that is what makes the layout checkable
rather than assumed: read the stream from the wrong offset and it does not inflate to the size the header claims.

So `cod2_buscar_contenedores.cpp`, which walks loose files, cannot see them. The missing step was inflating first,
and it is `tools/extraer_shaders.py`:

```powershell
python tools/extraer_shaders.py assets/shaders_ff          # containers + procedencia.tsv
python tools/comprobar_shaders_ff.py                       # the check
powershell -ExecutionPolicy Bypass -File shaders\cod2_regenerar_biblioteca.ps1
```

The script runs the whole pipeline: extract, translate to HLSL, compile to SPIR-V with DXC, validate with
spirv-val, pack, check, and copy the library next to the NRO (`app/out/sw8/cod2_shaders.nfsp`). `-SoloEmpaquetar`
re-runs only the last three steps over an extraction that is already there.

### What was verified

- **All 111 fastfiles in `assets/game_root/` inflate**, each to the size its own header declares, with no trailing
  bytes after the zlib stream. **None was skipped or rejected** — the extractor counts what it could not read and
  prints it, and the count is zero. 109 of the 111 hold at least one container; the two that hold none are `snd.ff`
  and `mp_snd.ff`, which are 58 KB of sound.
- **7,508 container occurrences, 1,124 distinct** (653 pixel, 471 vertex; 4,535 of the occurrences are pixel and
  2,973 vertex). The duplicates are the same shader in several fastfiles, and `code.ff` and `mp_code.ff` are the
  same file twice. `procedencia.tsv` records which fastfile and which inflated offset every occurrence came from,
  so the total can be re-derived without re-running the scan.
- **All 1,124 distinct containers translate** with `cod2_hlsl`, which is the real validation of the microcode:
  `cod2::Convertir2005` checks the tables and every `EXEC` address, and rejects the container otherwise.
- **All 1,124 compile to SPIR-V with DXC and pass `spirv-val`**, so the HLSL is not just syntactically plausible.
- **The packed library is 22,456,836 bytes and `cod2_probar_biblioteca` recovers all 1,124 originals from it**,
  rejects 78 deliberately corrupted packages, and exercises the module cache against a simulated Vulkan device.
- **The library is deterministic.** Two full runs and a third `-SoloEmpaquetar` run produced the same file,
  `SHA-256 879F950455EFC386EBFA82B684E192EEB34F5DFDC30B771E3229C215967CE65C`. That matters because the installer
  page compares the hash of the library a build was tested with, and because `EmpaquetarShaders` sorts the entries
  (by fingerprint, then by bytes) and `cod2_nativo_shaders.cpp` numbers them in that order — so the numbering
  `PorNumero()` looks up is a property of the library, not of the order the extractor happened to walk the
  fastfiles in.
- **The stage letter in a file name matches the stage the translator emitted.** This is not a formality: the low
  bit of the container's signature is 0 for a pixel shader and 1 for a vertex one, it is easy to write backwards,
  and it *was* — every file came out with the other stage's letter. Nothing caught it except `dxc -T ps_6_6`
  refusing an HLSL body declaring `[shader("vertex")]`. `tools/comprobar_shaders_ff.py` checks it directly now,
  along with the header-size invariant and the container filter.

### What was not verified

- **That the renderer finds these shaders.** The library is keyed by the container's own bytes, which is what
  `ShadersNativos::IdentificarContenedor` hands to `Buscar`, and the extractor copies the bytes verbatim out of
  the inflated fastfile — but nothing here has run the native renderer against a live draw, so "the game asks for
  a container that is in the library" is an argument, not a measurement. The tool to settle it is the library's
  own miss counter once the renderer is running.
- **Pixel shaders should be found exactly; vertex shaders may not be.** The game rewrites a vertex shader's
  microcode when Direct3D creates it (see [docs/shaders.md](../docs/shaders.md)), so the container the guest holds
  is not always the container on the disc and an exact-byte lookup can miss. `cod2_nativo_shaders.cpp` already has
  the fuzzy path for that — candidates of the same stage and length, compared with the fetch instructions masked
  out — and it is inherited code, not something this extraction was built against. If a CoD2 vertex shader turns
  out to be patched, that path is what covers it, and the library still holds the unpatched original it is
  compared against.
- **The three HLSL rewrites the `.sh` does** (shadow-map samples to `tfetch2DSombra` / `tfetch2DSombraMin`, and
  the radial blur of the composition shader) are **not** in the `.ps1`. They are the other game's performance
  work, keyed by its container names and its sampler defines. None of them is needed for a library that loads and
  draws; add them the same way once CoD2's own shaders are identified.
- **`README.md`'s "108 valid Xenos shader containers"** is not reproducible with this reader. No single fastfile
  has 108, `common.ff` — the largest single source of shared shaders — has 100 distinct, and the real totals are
  the ones above. The figure looks like a partial scan; it should be corrected where it is quoted.

The installer page runs the same steps in the browser (`lib/shaders.js` in the
[installer's repository](https://github.com/fenndooscar/cod2-nx-installer)). It checks that the result has the
SHA-256 (a fingerprint of the whole file) of the library each NRO was tested with.
