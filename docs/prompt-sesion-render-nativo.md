# New session: native Vulkan renderer for Call of Duty 2 (Xbox 360) — continue

Paste the block below into a fresh session. It is written to be self-contained: it carries the
state, the measurements, the mistakes already ruled out, and the next step.

---

## Prompt

You are continuing work on `C:\Users\igor2\Documents\Projects\cod2-nx`: an Xbox 360 port of **Call of Duty 2**
made by static recompilation with ReXGlue (v0.10.0), running on Windows PC, with a **hand-written native
Vulkan renderer** (`app/src/cod2_nativo_*.cpp`, ~35k lines) that replaces ReXGlue's emulated Xenos GPU. Read
`AGENTS.md` first, then `README.md`. The inspiration repository is `C:\Users\igor2\Documents\Projects\nfsmw-nx`.

**Scope right now: the native Vulkan renderer on PC only.** Forget the Switch, forget the 30 fps target and
forget console measurements; those come after the PC image is correct. Never run the PC build fullscreen
(`tools/pc/jugar.ps1` already passes `--fullscreen=false`).

### How to build and run

```powershell
# build (Release, Ninja, clang++; the PC build needs REXGLUE_USE_VULKAN=ON, already configured in app/out/pc)
& 'C:\Program Files\CMake\bin\cmake.exe' --build app/out/pc --target cod2 -- -j 8

# run into a mission without hands: -Partida drives the menus, -Ventana forces windowed
& .\tools\pc\jugar.ps1 -Partida -Ventana 1280x720 -Extra '--cod2_renderizador=nativo,--cod2_video_saltar=true'
```

- `--cod2_renderizador=nativo` selects the native renderer. Without it you get ReXGlue's emulated Xenos.
- `--cod2_video_saltar=true` skips the four intro movies (4,712 frames). With it a run reaches the mission in
  **26 seconds**; without it, three minutes. Always use it while working on the renderer.
- `-Partida` presses RETURN/SPACE in a loop until the renderer's own report shows a real mission
  (`escena ... (N dibujos)` with N >= 30). It is **flaky**: roughly one run in three ends up in the
  attract-mode demo (`cod2_attract.bik`) or in the menu instead. If the log shows `escena ... (3 dibujos)`
  for the whole run, the run measured a menu — throw it away and run again.
- `tools/pc/abaliza.ps1` runs an A/B of two or more cvar sets, measures the mosaic cell of each capture and
  prints the scene statistics for both. Use it instead of hand-running cases.
- The in-port capture writes PNGs to `%LOCALAPPDATA%\cod2-nx\pc\capturas\` when `cod2_captura_cada_s > 0`.
  Logs are in `%LOCALAPPDATA%\cod2-nx\pc\logs\cod2_NNN.log`, one per run.

### The problem being solved

The 3D scene renders **black**; the 2D HUD, the menus and the sky colour are correct. Pipeline statistics, in
a mission: the scene pass shades **0.00 M fragments** while the passes with no depth attachment shade 0.70 M.

### What is measured and settled — do not re-litigate

1. **The guest never clears the scene's depth.** In a whole run the only copy packet is the scene colour
   resolve (`CONTROL 00100000`, no clear bits) and the only depth clear the game requests is the shadow map's
   (base 0x010, pitch 160). The scene's depth is EDRAM base **0x530**, pitch 1280.
2. **The depth test's verdict is the gate.** Forcing the comparison, same mission frame:

   | depthCompareOp forced | scene fragments | image |
   |---|---|---|
   | LESS_OR_EQUAL (the game's own) | 0.00 M | black |
   | GREATER | 0.00 M | black |
   | **NOT_EQUAL** | 8.83 M | real scene |
   | ALWAYS | 3.96 M | real scene |

3. **The depth buffer is 1.0 before the frame's first scene pass and 0.0 over exactly the 1280x720 render
   area right after it.** Diagnostic `cod2_nativo_volcar_profundidad_s` (dump the depth image, count the
   distinct 32-bit words; D24S8: `0x00FFFFFF` = depth 1.0, `0x00000000` = 0.0):

   ```
   C6 volcado: fotograma 19 ANTES del pase 1 (categoria 2, 1280x1280, 0 dibujos antes, borra prof 1)
   C6 volcado de profundidad 1280x1280 (formato 129): 1 valores distintos, menor 00FFFFFF mayor 00FFFFFF
   C6 volcado: DESPUES del pase 1 con 2 dibujos dentro
   C6 volcado de profundidad 1280x1280 (formato 129): 2 valores distintos, menor 00000000 mayor 00FFFFFF;
     los mas frecuentes: 00000000x921600; 00FFFFFFx716800
   ```

4. **The only depth writer the port accepts is a full-screen quad.** Instrumenting every accepted draw with
   z_write in a pass of pitch >= 1280 finds exactly one per frame, always the same:

   ```
   C6 dibujo que escribe Z: VS n353 PS n620 tipo 4 cuenta 6 indices 1 viewport 0.0,0.0 1280.0x720.0
     z 0.000-1.000 funcion 3 mascara F en el pase 0 del fotograma 4731
   ```

5. **The draws that DO write the scene's depth are being rejected by the port.** Censused at the
   format-rejection site:

   ```
   C6 rechazo con profundidad 1: DEPTHCONTROL 00700736 (prueba 1 escribe 1 funcion 3) DEPTH_INFO 00000530
     base 530 formato color 14 VS n497 PS n285 tipo 4 cuenta 141 pitch 1280
   C6 rechazo con profundidad 2: DEPTHCONTROL 00700732 (prueba 1 escribe 0 funcion 3) DEPTH_INFO 00000530
     base 530 formato color 14 VS n578 PS n707 tipo 4 cuenta 30 pitch 1280
   ```

   `00700736` is z_enable 1, **z_write 1**, zfunc 3, at the scene's own depth base **530**. Format 14 is
   `k_32_FLOAT`, at the **same EDRAM base 0x260** as the scene's colour target: on the console one address is
   both, and the game writes linear depth into it while the real depth goes to 0x530. The port rejects
   **636,168** such draws in one run (cause 214 = `200 + formato`, format 14 = `k_32_FLOAT`, and it is the
   only rejection cause that ever fires).

### The diagnosis (confirmed, not a hypothesis)

The port throws away the game's own depth writes with the format-14 draws. The only depth writer left is the
full-screen quad, which fills the buffer with 0.0 over the render area. From then on the game's own
`LESS_OR_EQUAL` rejects every fragment, and a rejected fragment writes no depth either, so it never recovers.
Proof: `cod2_nativo_sin_z_quad_escena=true` (drop only that one draw's depth write, leave everything else
including the game's comparison) takes the scene pass from **0.00 M to 1.36 M fragments** — the 3D scene's own
z is perfectly usable.

The NaN reading was **wrong**, and the measurements said so: a NaN z would have failed `LESS_OR_EQUAL` against
the 1.0 the buffer held, the quad would have been rejected, and the buffer would have stayed at 1.0. It became
0.0, so the quad's z is a finite 0.0, which is normal for a screen-space quad.

### Next step

**The black scene is closed.** Two changes, both in the tree and both measured in a mission:

1. `cod2_nativo_formatos_extra` (new, default **true**): the loop over the four colour targets in `Dibujar`
   accepts `k_32_FLOAT` (format 14) as well. That is the colour target of the game's own linear-depth
   pre-pass (636,168 draws per run, `RB_DEPTHCONTROL 0x00700736`, writing the real depth at base 0x530), and
   rejecting it left the scene's depth with no writers. The dump after the pre-pass goes from **1 distinct
   value** (all 1.0) to **132,884**.
2. `cod2_nativo_profundidad_por_fotograma` now defaults to **true**: the scene's depth is cleared to 1.0
   once per frame instead of once per image. Accepting the pre-pass alone is *not* enough — the image stays a
   flat plane of sky colour and the diagnostic mosaic is identical with the poison quad on and off. With the
   per-frame clear the raw capture goes from **95.9 % of its pixels being exactly (155,152,152)** to a real
   3D scene, and the scene pass from 0.67-0.87 M fragments per frame to 2.72-6.52 M.

The old note in the cvar's comment ("a per-frame clear makes you see through the walls") was measured while
the pre-pass was still being rejected, so it is now wrong and was rewritten.

### What is still open

- **Correctness of the depth order** is not proven yet, only that the image is no longer flat: no test has
  looked for see-through walls, flicker or z-fighting yet. The evidence in favour is indirect and real: with
  `cod2_nativo_prueba_z_siempre` (depthCompareOp = ALWAYS everywhere) the capture differs from the normal one
  in 84.3 % of its pixels, so the depth test is doing work; and the mission's depth area holds 105,000-499,000
  distinct values spread over 0.92-1.0, which is a fine-grained projection, not a degenerate one.
- **The linear depth has no reader in the port.** Nothing samples base 0x260 as a 32-bit float, and the
  game's resolve of that address is still rejected with cause 114 (the copy's source format is 14). Which
  effects need it is unknown, so implementing the real R32F is deferred until one is identified.

### The 2D effects that write z = 0.0 (settled)

The draws that were called "the poison quad" are **VS n353 / PS n620, one or two per pass and many per
frame**, and their geometry is a **small screen-space rectangle** (measured 679..694 x 334..353 px and
302..319 x 593..606 px in the same frame), not a full-screen quad. Tracing one
(`cod2_nativo_diag_fotograma_s` + `cod2_nativo_diag_vertices_ps=620`) gives their vertex constants:

```
c0 = (1/640, 0, 0, -1)   c1 = (0, -1/360, 0, 1)   c2 = (0, 0, 0, 0)   c3 = (0, 0, 0, 1)
```

Rows 0 and 1 map a pixel coordinate to NDC and reproduce the raw vertices exactly on screen, so the port's
viewport/NDC conversion is right. **Row 2 is all zeros**, so `oPos.z` is 0 for every vertex and the window
depth is 0.0 whatever the viewport range says: the game asks for it. `RB_DEPTHCONTROL 0x00700736` (z_write 1,
zfunc 3) makes them write it. Against a persistent depth buffer those 0.0s reject the next frame's geometry,
which is exactly what the per-frame clear undoes.

### Ground rules

- **Measure, do not reason.** Every wrong turn in this port's history was a plausible argument nobody tested.
  `tools/pc/casos_render_nativo.ps1`, `cod2_nativo_volcar_profundidad_s` and the pipeline-statistics cvars
  exist for that.
- **Do not commit game data** (no `.xex`, `.iso`, `.ff`, `.bik`, `.nfsp`, no `assets/`, no `generated/`), never
  `git add -f` under an ignored path, and do not add dependencies.
- Every `app/src` file starts with `// cod2 - <one line>`, and when it exists to work around a specific
  failure the header explains the failure, the evidence, what it does instead, and the cvar that turns it off.
- Mark deliberate shortcuts with a `ponytail:` comment naming the ceiling and the upgrade path.
- **Commit and push on your own** (`git push`); the user does not want to be asked.
- The advisor is `agent(prompt, {provider: 'a6api-openai', model: 'gpt-6.1-sol'})` through `workflow`, with
  `glm-5.3` at `a6api` as the fallback. The model id `""` and `Gemini 4 Argon` do not answer in this profile:
  they return a canned line about loading skills.
- Leave one runnable check behind for non-trivial logic, and say explicitly what was and was not verified.

### Temporary instrumentation that is still in the tree (remove once the image is correct)

`cod2_nativo_sin_z_quad_escena` + `quad_escena_sin_z_`, the `C6 rechazo con profundidad` census +
`rechazos_con_profundidad_`, `C6 dibujo que escribe Z` + `dibujos_z_volcados_` + `quad_en_pase_` (the
`*** CON EL QUAD ***` mark on the "DESPUES del pase" dump), the pass-by-pass depth dump
(`cod2_nativo_volcar_profundidad_s`, `VolcarProfundidad`, `imagen_del_volcado_`, `fotograma_volcado_`,
`pases_volcados_`, `volcados_del_fotograma_`, `volcados_profundidad_`), the `C6 borrado de profundidad en pase`
probe, the `C6 tipo de copia` / `C6 peticion de borrado` / `C6 escritura de RB_DEPTH_CLEAR` probes and their
three `unordered_set` members, the cause-214 log + `formato_temporal_`, `dibujos_con_shaders_` /
`dibujos_sin_shaders_`, the `C5b`/`C6` temporal counters, and `tools/pc/instrumentar_dibujos.py`. The
`cod2_nativo_prueba_*` measurement cvars (`sin_z`, `sin_cull`, `giro_inverso`, `z_siempre`, `z_mayor`,
`z_no_igual`) stay until the image is verified correct.

### Also true and useful

- **`app/out/pc` is a Release Ninja build** with `-DREXGLUE_USE_VULKAN=ON`. The runtime is staged at
  `%LOCALAPPDATA%\cod2-nx\pc\` (`cod2.exe`, `rexruntime.dll`, `rexgpu-xenos.dll`, `cod2.toml`,
  `cod2_shaders.nfsp`, `datos/`, `logs/`, `capturas/`, `cache/`). `jugar.ps1` copies them; kill a leftover
  `cod2.exe` first or `Copy-Item` fails with "being used by another process".
- **The recompiled vertex shaders apply a Y flip** (`DXC -fvk-invert-y`) and the port builds a
  negative-height viewport in the normal path. Do not "fix" one without the other; they cancel.
- **The render passes had no Vulkan dependency at all** (`initialLayout = finalLayout = GENERAL` means Vulkan
  inserts no implicit one). `CrearPase` now declares the `VK_SUBPASS_EXTERNAL` dependency between a depth write
  and a depth test. It did not change the symptom but it was a real bug.
- The guest's own depth `RB_DEPTHCONTROL` is `0x00700732` (z_enable 1, z_write 0, zfunc 3) and `0x00700736`
  (z_enable 1, z_write 1, zfunc 3). `xenos::CompareFunction`: 0 never, 1 less, 2 equal, 3 less-equal,
  4 greater, 5 not-equal, 6 greater-equal, 7 always — the port's `VkCompareOp((d >> 4) & 0x7)` is correct.
- Shader numbers in the logs are indices into the packed library, which is **sorted by fingerprint**
  (`app/src/cod2_shader_library.cpp`), not by container name, so `n353` cannot be mapped to a `.bin` file by
  sorting the directory. `tools/comprobar_biblioteca_shaders.py <n>...` does it: it reads the library, checks
  the sort order its binary search depends on, and names the `.bin` and `.hlsl` of each index. Verified:
  `VS n353` = `v_000004.bin`, `PS n620` = `p_000064.bin`.
- **A capture is only comparable with another from the same run.** The mission start is not deterministic
  (the player's position and the scripted intro differ run to run), so two runs of the same cvar set differ in
  94 % of their pixels. Compare captures inside one run, or compare categorical things (flat plane vs scene).
  `tools/pc/casos_render_nativo.ps1` keeps the raw PNG per case instead of the brightened one `abaliza.ps1`
  writes.
- **Shaders live inside `.ff` fastfiles**, extracted to `assets/shaders_ff/` (`contenedores/`, `hlsl/`,
  `spirv/`, `cod2_shaders.nfsp`). `shaders/cod2_regenerar_biblioteca.ps1` regenerates the whole library;
  `shaders/cod2_hlsl.exe <contenedores> <hlsl> <shader_common.h>` translates one set of containers.
- Do not trust `docs/` for addresses: it was written for the previous game (nfsmw-nx). Only what this
  session measured is in this prompt.
