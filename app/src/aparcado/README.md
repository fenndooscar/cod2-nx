# src/aparcado - code inherited from nfsmw-nx, parked until Call of Duty 2 boots

"aparcado" means "parked". Nothing in this folder is compiled: it is not in `app/CMakeLists.txt`, and
nothing in `app/src/` includes it. It is kept because most of it is the *shape* of the work this game will
need, and because it documents what the previous game's port learned.

**It is not Call of Duty 2 code.** Every address in it (`sub_82593A10`, `0x826BE1B0`, …), every hook, every
structure offset and every measured number belongs to Need for Speed: Most Wanted. Not one of them has been
checked against this game's `default.xex`.

Measured on 2026-02: of the 205 distinct `sub_XXXXXXXX` addresses that appear in these files and in
`overrides.toml` / `huecos.toml`, **203 do not exist in Call of Duty 2's generated code** (the codegen
produced 21,173 functions from `0x82090000`–`0x82495E14`). Compiling any of these files would either fail to
link on `__imp__sub_XXXXXXXX` or, worse, define a strong symbol for an address the game never calls.

| File | What it did in the fork | What has to happen for CoD2 |
|---|---|---|
| `cod2_d3d_trace.cpp` | Traced the game's Direct3D calls to `rex_d3d.log`; the base of the native renderer | Re-derive: find CoD2's D3D device and its draw functions |
| `cod2_nativo_*.cpp` | The whole native renderer (graphics system, draws, textures, pipelines, shaders) | Later; only after the game runs with the emulated GPU |
| `cod2_render_targets.cpp`, `cod2_recortes_carrera.cpp` | Tiling/MSAA and race-scene reductions | CoD2 has no car cubemap or road reflection; the tiling part still applies |
| `cod2_audio_*.cpp` | Audio server rescue, engine-sound probe, resamplers, filter, sum | The shape applies (XAudio voice ring), the addresses do not |
| `cod2_video_wmv3.cpp`, `cod2_video_nativo.cpp`, `cod2_video_bridge.cpp` | WMV3 cutscenes through the SDK's FFmpeg | **Wrong codec**: CoD2's cutscenes are Bink (`.bik`). The Bink path now exists and is live (`app/src/cod2_video_bink.cpp` and `cod2_video_bink_nativo.cpp`, cvar `cod2_video_bink_nativo`), so what is left of value here is the *bridge* - handing a decoded frame to the renderer as the port's own texture instead of letting the game upload it. See the last paragraph of [docs/audio-and-video.md](../../../docs/audio-and-video.md) |
| `cod2_video_diag.cpp` | Cutscene frame diagnostics | Same, once the Bink path exists |
| `cod2_escenario_nativo.cpp`, `cod2_material_nativo.cpp`, `cod2_matrices_nativo.cpp`, `cod2_eview_nativo.cpp`, `cod2_pegamento_nativo.cpp`, `cod2_efecto_pasada_nativo.cpp`, `cod2_d3d_registros_nativo.cpp` | Native replacements of hot renderer functions, each checked against a literal copy | All of them are the *other* game's functions |
| `cod2_sombras_lod.cpp`, `cod2_recorte_sombras.cpp`, `cod2_escenario_lod.cpp` | Shadow and LOD reductions | CoD2-specific; nothing carried over |
| `cod2_streaming_anticipacion.cpp` | Hook on `TrackStreamer::GetPredictedZone` | CoD2 uses fastfiles (`.ff`), a different streamer |
| `cod2_espera_anillo.cpp`, `cod2_espera_fotograma.cpp` | Blocking waits on the PM4 ring and the frame handoff | The PM4 ring exists on CoD2 too; addresses and structures are different |
| `cod2_shader_library.cpp`, `cod2_nativo_shaders.cpp`, `cod2_shader_hooks.cpp` | Reads `cod2_shaders.nfsp` and matches shaders to draws | The library format is reusable; CoD2's shaders are inside `.ff` fastfiles |
| `cod2_entorno_mesa.cpp` | Mesa/NVK environment variables and the libnx nvmap area | Mostly generic; re-enable when there is something to run on the console |
| `cod2_ajustes_graficos.cpp` | The F4 settings menu | Tied to the other game's menu; rewrite from CoD2's own |
| `cod2_prueba_entrada.cpp` | Virtual gamepad driven by a script, for PC tests | Useful, generic enough to re-enable once there is a build |
| `cod2_crt_nativo.cpp` | Native `memset` of the game's CRT | Re-derive `memset`/`memcpy` addresses from CoD2 (see `overrides.toml` `[rexcrt]`) |
| `cod2_hilos_switch.cpp` | Thread priority and CPU time on Horizon | Generic; re-enable when something on the console needs it |
| `cod2_microcodigo_versiones.h`, `cod2_esperas_tiron.h`, `cod2_informe_diferido.h`, `cod2_reflejo_demanda.h`, `cod2_nativo_vertices_dedupe.h`, `cod2_audio_nativo.h` | Headers of the above | Same as their .cpp |
| `cod2_video_*`, `cod2_shader_vulkan.*` | The experimental shader/video library, off by default (`COD2_NATIVE_SHADER_LIBRARY`) | Later, if ever |

## How to bring one back

1. Find the equivalent function in CoD2's generated code (`app/generated/default/`), not by address but by
   reading what it does. `tools/lee_antes.py`, `tools/huecos.py` and `tools/calientes.py` are the tools.
2. Declare the address in `app/overrides.toml` with its explanation, one entry at a time.
3. Move the file back into `app/src/`, fix its addresses, add it to `app/CMakeLists.txt`.
4. Run `python tools/llamadas_directas.py` again (it reads `app/src` to decide which functions have hooks).

Do not move a file back "to see if it compiles". A file that compiles but hooks the wrong address is worse
than a file that is missing.
