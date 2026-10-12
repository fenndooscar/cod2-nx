# Native renderer anchors in Call of Duty 2 — inventory

What exists in **this game's** generated code for each anchor a native renderer of the
[nfsmw-nx](https://github.com/StevensND/nfsmw-nx) shape hooks, how it was found, and how confident that is. This is a
reconnaissance report, not a porting plan.

Every address below was found in `app/generated/default/`. Nothing here is inherited from the fork; the fork's
addresses appear only as *what to look for*. Where a claim could not be checked, §5 says so.

## 0. What was searched, with what, and what was not

| Tool / source | What it was used for |
|---|---|
| `app/generated/default/` — 142 files, ~74 MB, **21,017** `DEFINE_REX_FUNC` bodies | The only source of addresses. Every line number below is in that directory. |
| `app/out/invitado.bin` — 36,700,160 bytes, an `MZ` image at file offset 0 | The guest data. Byte at file offset `o` is guest address `0x82000000 + o`. |
| `tools/refiere_datos.py` | Maps a *data* address to the function that builds it (`lis` + `addi`/`ori`). ~4.4 s per run over all 142 files. This is how "which function prints this D3D call" was answered. |
| `grep` over the generated code | For PM4 packet headers (`lis rX,-16383` → `0xC0010000`), for `cntlzd` (the register-mask walk), and for `lis rX,-31901` (the `dx` global). |
| `sdk/include/rex/graphics/register_table.inc` | The numeric Xenos register indices, used to turn a byte offset at the MMIO base into a register name. |

**The guest-image mapping is confirmed, not assumed.** Two independent paths agree. `tools/refiere_datos.py` says
`sub_82387150` builds `0x82050E74`; `app/out/invitado.bin` has `Creating Direct3D device...` at file offset
`0x50E74`. The same holds for `0x820531E8` (`dx.device->SetPixelShader( shader )`), `0x82053268`
(`DrawIndexedPrimitiveUP`), `0x82053794` (`RB_DrawSingleTechnique`) and every other string below.

**The register-index mapping is confirmed, not assumed.** `sdk/src/graphics/graphics_system.cpp:141` puts the GPU
registers at `0x7FC80000`. Five byte offsets used by CoD2's D3D divide by 4 into a register that exists in
`register_table.inc`, and no other interpretation fits:

| Offset in the `stw`/`lwz` | `÷ 4` | Register |
|---|---|---|
| `1812` | `0x1C5` | `CP_RB_WPTR` (`register_table.inc:3030`) |
| `25924` | `0x1951` | `D1MODE_VBLANK_VLINE_STATUS` (`:3603`) |
| `25904` | `0x194C` | `D1MODE_V_COUNTER` (`:3598`) |
| `12820` | `0x0C85` | `PA_CL_ENHANCE` (`:116`) |
| `15428` | `0x0F11` | `RB_ZFAIL_SAMPLES` (`:3504`) |

**Not searched.** No disassembler pass over `default.xex` — everything here is from the translated C++. No scan of
the `.ff` fastfiles (only the tool that would do it was read; §4). The game was not run, on PC or on the console.
`tools/calientes.py` and `switch-logs/` were not used: they name hot functions for a build of the *other* game, and
there is no CoD2 profile to read.

## 1. Anchor table

| # | Anchor group | Found? | CoD2 address(es) | Evidence | Confidence |
|---|---|---|---|---|---|
| 1a | D3D device, the `dx` state struct | yes | global `0x83630E58`, device slot `0x83630E60` | `lis -31901` + `addi 3672`, then `lwz r3,8(rN)` as the `device` argument; 3 sites read | confirmed |
| 1b | Draw submission (D3D level) | yes | `sub_82368310` | Logs both `DrawIndexedPrimitive` and `DrawIndexedPrimitiveUP`; **one** call site in the whole program | confirmed |
| 1c | Draw submission (PM4 ring level) | yes | `sub_820A6FE8`, `sub_820A7228`, `sub_820A6F30` | Build `0xC0012201` / `0xC0032201` = PM4 type-3 opcode `0x22` (`DRAW_INDX`) | confirmed |
| 1d | Draw-list loop | yes | `sub_82368F20`, per entry `sub_82368C40` | Prints `RB_DrawSingleTechnique`; walks a `u16` count at `[list+6]`, 28-byte entries from `list+8`; one call site | confirmed |
| 1e | Ring-full handler | yes | `sub_820A54B8` | Called when the write pointer has passed the end, from all three draw writers | probable |
| 1f | Changed-register-mask emitter | partial | `sub_820B8B38` | The only function in the game with two consecutive `cntlzd`; emits `((n-1)<<16)\|base` type-0 headers | guess (§3.1) |
| 2a | Shader creation (material path) | yes | `sub_82384180` → `sub_8209BD78` | Logs `CreateVertexShader` / `CreatePixelShader` | confirmed |
| 2b | Shader creation (other paths) | yes | `sub_82090110`, `sub_8247E8B0` → the same `sub_8209BD78` | The constructor has exactly **3** call sites | confirmed |
| 2c | Vertex declaration creation | yes | `sub_82383FD8` | Logs `CreateVertexDeclaration( elemTable, &decl )` | confirmed |
| 2d | Binding a shader / decl / FVF | yes | `sub_82098A88`, `sub_82098B10`, `sub_82098BC8`, `sub_82098E10` | Log `SetPixelShader` / `SetVertexShader` / `SetVertexDeclaration` / `SetFVF` | confirmed |
| 2e | Shader container reader | yes | `sub_8209BB00` | Follows `[shader+52] → +12` to the definition block and copies it into the device shadow | probable |
| 2f | Shader identity by name | yes | `sub_823B94B8` | `Shader name string is empty` | probable |
| 3a | **PM4 ring kickoff** | yes | `sub_820A4E48`, `sub_820A4E68` | Write `MMIO[0x7FC80000 + 1812]` = `CP_RB_WPTR` | confirmed |
| 3b | Swap / present | yes | `sub_82372D78`, `sub_82386F58`, `sub_822D1A30` → `sub_820A7E28` | `RB_SwapBuffers`; the three functions that log `dx.device->Swap(...)` | confirmed |
| 3c | Vblank / presentation interval | yes | `sub_820A5BB0`, `sub_820A92E0`, `sub_820A7D58` | Read `D1MODE_VBLANK_VLINE_STATUS` / `D1MODE_V_COUNTER` at the MMIO base | confirmed |
| 4a | Render target creation | yes | `sub_8237D0E8` | Logs `CreateRenderTarget` + `CreateDepthStencilSurface` for the main target | confirmed |
| 4b | Tiled targets | yes | `sub_8237CE18` (tileTop), `sub_8237CD08` (tileBot) | Log the tiled `CreateRenderTarget` pair, `D3DMULTISAMPLE_2_SAMPLES` | probable |
| 4c | Begin/EndTiling entry points | **no** | — | The tiled targets and the `r_tiling` cvar exist, but the D3D wrapper does not log these calls | absent |
| 4d | Front-buffer texture | yes | `sub_8237D818` | Logs `CreateTexture( …, &dx.frontBufferTexture, 0 )` | confirmed |
| 4e | SetRenderTarget / SetDepthStencilSurface | yes | `sub_82365828` | Logs both; 15 call sites | confirmed |
| 4f | Resolve | yes | `sub_8236D4B0`, `sub_8236D6F0` | Log `dx.device->Resolve( … )` | confirmed |
| 4g | Clear | yes | `sub_82379FA8`, `sub_8236D2C0` | Log `dx.device->Clear( … )` / `ClearF` | confirmed |
| 4h | BeginScene / EndScene | yes | `sub_8236D230`, `sub_8236D430`; `sub_8236D1A0` | Log `dx.device->BeginScene()` / `EndScene()` | confirmed |
| 5a | Per-draw front end (pass setup) | yes | `sub_82368C40` | Logs `RB_SetupPass`, the vertex/pixel shader name+version, the state map and the state bits | confirmed |
| 5b | Surface begin / end | yes | `sub_823675C0`, `sub_82369368` | Log `RB_BeginSurface( %s, %s )` / `RB_EndSurface` | confirmed |
| 5c | Material / sampler binder | yes | `sub_823686A8` | Logs the lightmap, `sampler.outdoor` and `sampler.sky` errors | probable |
| 5d | stateMap rule resolution | yes | `sub_82367960` | Logs `No rule in stateMap '%s' rule set %i matched…` | probable |
| 5e | State setters | yes | `sub_82364708`, `sub_82364158`, `sub_82365AE0`, `sub_8236DED8`, `sub_8236CFA8`, `sub_82364070` | One `SetRenderState`/`SetSamplerState` string each | confirmed |
| 5f | Streams, indices, textures | yes | `sub_82364DC8`, `sub_82364E88`, `sub_82364F68` | `SetStreamSource` / `SetIndices` / `SetTexture` + `D3DSAMP_MINFILTER`/`MAGFILTER` | confirmed |
| 5g | Shader constants | yes | `sub_82367CB0`, `sub_82367A60` | `SetVertexShaderConstantF` / `SetPixelShaderConstantF` | confirmed |
| 5h | Viewport | yes | `sub_82365660`, `sub_82365788` | `SetViewport( &dxState.viewport )` | probable |
| 5i | Per-model draw entries | yes | `sub_82361918`, `sub_82361788`, `sub_82361CF0`, `sub_82361A78` | `RB_TessXModelSkinnedCached`, `RB_TessXModelRigid`, `RB_TessStaticModelCached`, `RB_TessParticleCloud` | confirmed |
| 5j | IM_LOAD (microcode upload) | **no** | — | Not located; §3.6 | absent |

## 2. The method that made this cheap

**CoD2's Direct3D wrapper logs every call, and the log string is the source text of the call.** So the string at
address `A`, resolved with `tools/refiere_datos.py A`, names the wrapper function that performs that operation:

```
$ python tools/refiere_datos.py 0x82053328
0x82053328  sub_82368310 cod2_recomp.32.cpp:25088
```

The string is `dx.device->DrawIndexedPrimitive( D3DPT_TRIANGLELIST, args->u.buf.baseVertex, … )`. That gives the
address, the wrapper, the arguments and the meaning in one step, with no disassembly. All of §1's confirmed rows
come from this, cross-checked against `app/out/invitado.bin`.

The whole `dx.device->` string set is a map of the game's D3D front end. It is worth knowing that it is there:
dump the guest image's strings, take every line containing `dx.device->`, and each one resolves to a function.

## 3. Evidence per anchor

### 3.1 Group 1 — the Direct3D device and draw submission

**The device pointer.** Every logged D3D call site loads the device the same way: `lis rN,-31901` (`0x83630000`)
then `addi rN,rN,3672`, then `lwz r3,8(rN)`. So `0x83630E58` holds the `dx` struct and `0x83630E60` holds
`dx.device`. Three independent reads agree:

- `sub_82098A88` (`cod2_recomp.1.cpp:298`) — `addi r31,r11,3672` at `:327`, `lwz r3,8(r31)` at `:360`, passed to
  `sub_8209BB00` at `:361`.
- `sub_82368310` (`cod2_recomp.32.cpp:24895`) — `addi r29,r11,3672` at `:25009`, `lwz r3,8(r29)` at `:25055`,
  passed to `sub_820A6F30` at `:25066`.
- `sub_82386F58` (`cod2_recomp.2.cpp:26295`).

The device object's own layout, read out of `sub_820A6FE8` and `sub_820A4E68`:

| Offset | What it is |
|---|---|
| `+0` | ring write pointer (guest byte address) |
| `+8` | ring end |
| `+16 … +48` | the changed-register masks (64-bit words) |
| `+1152` | shader-constant shadow, `reg * 4` |
| `+6916` | the ring / queue object (every draw path re-reads `[[device+6916]+8]`) |
| `+10432`, `+10433` | flags (`0x04` of `+10433` = "GPU declared hung") |
| `+10444` | cached copy of `CP_RB_WPTR` |
| `+11540` | last `VGT_INDX_OFFSET` written |
| `+12944` | currently bound shader object |

**`sub_82368310`** (`cod2_recomp.32.cpp:24895`) is the game's `dx.device->DrawIndexedPrimitive`. It references both
draw strings — `0x82053268` (`DrawIndexedPrimitiveUP`) at `:25015` and `0x82053328` (`DrawIndexedPrimitive`) at
`:25088` — and calls the ring-level writer for each: `sub_820A6F30` at `:25066` and `sub_820A7228` at
`:25133`. Both are guarded by the same loop over `[r28+31228]` (a "stop" flag) and both finish by calling
`sub_8236D0D0` (`:25069`, `:25138`). It is the **only** function in the program that references those two strings,
and it has exactly **one** call site: `cod2_recomp.56.cpp:26136`. A single call site is the best possible anchor
for a per-draw hook.

**The ring-level writers build real PM4.** The header is built as `lis r11,-16383` (`0xC0010000`) `ori …,8705`
(`0x2201`):

- `sub_820A6FE8` (`cod2_recomp.17.cpp:695`). `0xC0012201` at `:764-765`: type 3, opcode `0x22` (`PM4_DRAW_INDX`),
  count field 1 → two dwords follow. Chunks of at most 65535 (`:779`, with a `divwu` to pick the divisor). It
  caches the index offset at `[device+11540]` and then writes a **type-0 packet for `8450` = `0x2102` =
  `VGT_INDX_OFFSET`** — `li r11,8450` at `:1025-1026`, `stw r23,11540(r29)` at `:1027-1028`, then two `stwu` at
  `:1029-1035`. That is exactly the shape of a one-dword type-0 packet.
- `sub_820A7228` (`cod2_recomp.2.cpp:730`). Same shape with `lis r11,-16381` (`0xC0030000`) `ori …,8705` at
  `:791-794` → `0xC0032201`: type 3, count 3 → four dwords. The indexed path.
- `sub_820A6F30` (`cod2_recomp.45.cpp:770`). The user-pointer path: writes the packet through `sub_820A6C18`
  (`cod2_recomp.46.cpp:752`) and then copies the vertex data into the ring.
- `sub_820A54B8` (`cod2_recomp.34.cpp:695`) is called from all three when the write pointer has passed the end.
  This is the analogue of the fork's `sub_825A29E8`.

**The draw-list loop.** `sub_82368F20` (`cod2_recomp.56.cpp:25981`) prints
`0x82053794 ---------- RB_DrawSingleTechnique( %s ) ----------` at `:26034`. Its argument is an index: `r10 = r3+1`
(`:26008`), `r4 = (r3+1)*4` (`:26016`), and the entry list is `*(ptr_at_0x83D088A0) + 32` indexed by that
(`:26010-26011`, `:26020-26021`, `:26026-26027`). Per entry:

| Step | Line |
|---|---|
| count = `u16 [list+6]` | `:26110` / `:26137` |
| entries start at `list+8` | `:26118` |
| `sub_82368C40(entry, param2, 0)` | `:26127` |
| `sub_82368310(param3, list)` — the draw | `:26134` |
| advance by **28 bytes**, compare against the count | `:26141-26145` |

It has exactly one call site: `cod2_recomp.11.cpp:26659`, inside `sub_82369070`
(`cod2_recomp.11.cpp:26579`). That function has seven call sites, one of them `sub_82361918` =
`RB_TessXModelSkinnedCached`.

That is the same shape as the fork's `sub_82454B50` (walk a sorted list, call per-entry glue then the draw), with
different offsets: 28-byte entries and a `u16` count at `+6`, against the fork's 8-byte entries and count at `+0`.

**Changed-register-mask emitter — a guess.** The fork's `sub_825A2AA0` (its `cod2_d3d_registros_nativo.cpp`) walks
a 64-bit mask, emits one type-0 packet per run of set bits with header `((n-1)<<16) | first_register`, and copies
the values. `sub_820B8B38` (`cod2_recomp.63.cpp:1278`) is the only function in the game with two consecutive
`cntlzd` (`:1303`, `:1321`) and it does exactly that: `cntlzd` for the run start, `cntlzd` of the inverted mask for
the run length, header `((n-1)<<16) | r28` at `:1367-1379`, then a copy loop at `:1381-1405`, and the write pointer
stored back at `:1411`. Two things do not fit, and are why this is a guess and not a finding: the run base is
`18432 + 6*z`, which is not a Xenos register index, and the copy is 24 bytes per set bit where PM4 carries 4.
**What would confirm or kill it:** a reader who can explain a 24-byte-per-register record, or a run of the game
showing this buffer being consumed into the ring.

### 3.2 Group 2 — shader creation and identification

**`sub_82384180`** (`cod2_recomp.7.cpp:27056`) is the material system's shader constructor. It branches on the byte
at `[r30+10]` and logs `0x82051438 dx.device->CreateVertexShader( mtlShader->program, &shader->vs )` (`:27090`) or
`0x82051480 dx.device->CreatePixelShader( mtlShader->program, &shader->ps )` (`:27134`), then calls
`sub_8209BD78(device, program)` at `:27115` and stores the result at `[out+0]`. `r30` comes from the global at
`0x82975244`, and the device from `[r30+4]`.

**The constructor is `sub_8209BD78`** (`cod2_recomp.36.cpp:366`). It has exactly **three** call sites, so three
creation paths exist and a hook on the material path alone would miss two:

| Call site | Enclosing function | Reading |
|---|---|---|
| `cod2_recomp.7.cpp:27117` | `sub_82384180` | the material path (`CreateVertexShader`/`CreatePixelShader`) |
| `cod2_recomp.5.cpp:22` | `sub_82090110` (`cod2_recomp.5.cpp:3`) | a thin XDK-side wrapper that stores the object at `[r5]` |
| `cod2_recomp.10.cpp:35417` | `sub_8247E8B0` (`cod2_recomp.10.cpp:35174`) | in the game's own address range (`0x8247xxxx`), not the XDK's |

`sub_8247E8B0` is the interesting one: it is CoD2's own code, not the XDK's, and it constructs a shader directly.
`sub_823B94B8` (`cod2_recomp.44.cpp:28547`) is the name-based lookup that goes with it — it prints
`0x8204B714 Shader name string is empty` at `:28636`. **Hook `sub_8209BD78`, not the three wrappers**, and the
three paths collapse into one.

**Vertex declarations** are separate: `sub_82383FD8` (`cod2_recomp.51.cpp:26883`) logs
`0x820514C4 dx.device->CreateVertexDeclaration( elemTable, &decl )` at `:27057`.

**Binding, and the container reader.** `sub_82098A88` (`cod2_recomp.1.cpp:298`) is `SetPixelShader`: it skips the
call if the shader is unchanged (`[0x83D0...] + 8424`, `:316-321`), prints `0x820531E8` at `:332`, and calls
`sub_8209BB00(device, shader)` at `:361`. `sub_82098B10` (`SetVertexShader`, `0x82053210` at
`cod2_recomp.34.cpp:318`), `sub_82098BC8` (`SetVertexDeclaration`, `0x82053238` at `cod2_recomp.10.cpp:345`) and
`sub_82098E10` (`SetFVF`, `0x8205383C` at `cod2_recomp.52.cpp:343`) have the same shape.

`sub_8209BB00` (`cod2_recomp.0.cpp:424`) is where the shader becomes ring state, and it is the **container reader**:
it bumps the refcount at `[shader+4]` (`:444-449`), stores the object at `[device+12944]` (`:463`), sets bits in
the changed-register masks at `device+16` (`:467-476`), then follows `[shader+52] → +12` to the definitions block
(`:479-488`) and walks `(u16 register, u16 count)` pairs followed by `count` dwords (`:522-543`), copying each into
the device shadow at `device + 1152 + register*4`. A renderer hooking here sees, for every shader the game binds,
the shader object and its container — which is exactly the pairing the ring needs.

### 3.3 Group 3 — the ring kickoff and Swap

**This is the cleanest anchor in the report.**

```
DEFINE_REX_FUNC(sub_820A4E48) {                    // cod2_recomp.46.cpp:682
  REX_STORE_U32(ctx.r3.u32 + 10444, ctx.r4.u32);   // cached write pointer
  // sync
  REX_MM_STORE_U32(0x7FC80000 + 1812, ctx.r4.u32); // CP_RB_WPTR
  // eieio / sync
}
```

`1812 = 0x714 = 0x1C5 * 4`, and `0x1C5` is `XE_GPU_REG_CP_RB_WPTR` (`register_table.inc:3030`). The MMIO base
`0x7FC80000` is the one `sdk/src/graphics/graphics_system.cpp:141-142` registers. `sub_820A4E48` is the whole
kickoff, with no other work in it. **Caveat:** it has no direct caller in the generated code — it appears only in
the two generated tables, `cod2_init.cpp:1146` and `cod2_register.cpp:1128`, so it is reached indirectly (through a
function pointer or the device vtable). The companion `sub_820A4E68` (`cod2_recomp.10.cpp:725`) performs the same
write inside the flush path at `:940-946` and has **eight** direct call sites, so the pair is still solid even if
`sub_820A4E48` is never reached.

**Swap.** `sub_82372D78` (`cod2_recomp.3.cpp:25948`) prints
`0x82052AC8 ***************** RB_SwapBuffers *****************` at `:25980` — it is the game's `RB_SwapBuffers`.
Three functions log the `Swap` string `0x82051010`: `sub_82372D78` (`:25995`), `sub_82386F58`
(`cod2_recomp.2.cpp:26517`) and `sub_822D1A30` (`cod2_recomp.51.cpp:20449`). The XDK swap itself is `sub_820A7E28`
(`cod2_recomp.25.cpp:749`), called from `sub_82386F58` at `cod2_recomp.2.cpp:26445`, from `sub_823744E8` at
`cod2_recomp.61.cpp:25674` and from `sub_822D1A30` at `cod2_recomp.51.cpp:20637`. `sub_823744E8`
(`cod2_recomp.61.cpp:25236`) is the per-frame flush: it references `SynchronizeToPresentationInterval`
(`0x82050F8C` at `:25647`), `Resolve` (`0x82050FC0` at `:25683`), `Swap` (`0x82051010`), `Clear` (`0x82052B00` at
`:25376`) and `EndScene` (`0x820525E8` at `:25544`).

**Vblank.** `sub_820A5BB0` (`cod2_recomp.27.cpp:677`) and `sub_820A92E0` (`cod2_recomp.57.cpp:796`) read
`MMIO + 25924` = register `0x1951` = `D1MODE_VBLANK_VLINE_STATUS`; `sub_820A7D58` (`cod2_recomp.52.cpp:751`) reads
`+25904` = `0x194C` = `D1MODE_V_COUNTER`. Those are the display-controller registers behind the wrapper's
`SynchronizeToPresentationInterval` (`0x82050F8C`, referenced by `sub_82386F58` at `cod2_recomp.2.cpp:26418` and by
`sub_822D1A30` at `cod2_recomp.51.cpp:20468`).

### 3.4 Group 4 — render targets and resolve

`sub_8237D0E8` (`cod2_recomp.3.cpp:26413`) logs `0x820517B8 CreateDepthStencilSurface( vidConfig.width, … )` at
`:26510` and `0x82051878 CreateRenderTarget( vidConfig.width, …, D3DFMT_A8R8G8B8, dx.multiSampleType, … )` at
`:26600` — the main colour and depth target. It calls `sub_820A36C0(device, w, h, format…)` at `:26562` and stores
the result at `[obj+36]` (`:26568`). It is called from `sub_8237D6D8` (`cod2_recomp.33.cpp:26889`, call at
`:26922`) and one other site.

The tiled targets are separate: `sub_8237CE18` (`cod2_recomp.51.cpp:26550`) creates `tileTop` (`0x82051A90` at
`:26584`, `0x82051B30` at `:26644`) and `sub_8237CD08` (`cod2_recomp.12.cpp:27269`) creates `tileBot` (`0x82051BE0`
at `:27303`, `0x82051C80` at `:27363`), both with `D3DMULTISAMPLE_2_SAMPLES`. `sub_8237CF20`
(`cod2_recomp.6.cpp:26245`) makes the single-sample pair. `sub_8237D818` (`cod2_recomp.48.cpp:25842`) creates
`dx.frontBufferTexture` (`0x82051E40` at `:26037`), which is what `Swap` and the resolve read and write. There is
also a 128×128 depth target at `sub_8237D2A0` (`cod2_recomp.32.cpp`, string `0x82051730` at `:25788`).

Binding is `sub_82365828` (`cod2_recomp.46.cpp:24988`), which logs both `SetRenderTarget` (`0x820543E8` at
`:25095`) and `SetDepthStencilSurface` (`0x82054438` at `:25203`); **15** call sites, so this is the shared point
for every target change. `sub_8236D4B0` (`cod2_recomp.61.cpp:25061`, string `0x82052558` at `:25137`) and
`sub_8236D6F0` (`cod2_recomp.60.cpp:26829`, string `0x82052398` at `:26957`) are `Resolve`. Clears are
`sub_82379FA8` (`cod2_recomp.63.cpp:26599`, string `0x82052030` at `:26645`) and `sub_8236D2C0`
(`cod2_recomp.39.cpp:26051`, `ClearF`, string `0x82052510` at `:26171`). `BeginScene` is `sub_8236D230`
(`cod2_recomp.65.cpp:26182`, string `0x820525CC` at `:26231`) and also `sub_8236D430`
(`cod2_recomp.28.cpp`, string at `:26508`); `EndScene` is `sub_8236D1A0` (`cod2_recomp.64.cpp:25540`, string
`0x820525E8` at `:25560`).

**The `r_tiling` cvar exists but is not an anchor.** `sub_82389D30` (`cod2_recomp.58.cpp:27285`) is a 3,091-line
function that references **122** strings built from one base (`0x82050000`), including `r_overbrightBits`,
`r_gamma`, `r_textureMode`, `r_tiling` (`0x82050430` at `:28532`) and `r_skipBackEnd`. It is the **cvar
registration** function, not a tiling entry point. It tells you the cvar is real; it does not give you a bracket to
hook. That is why row 4c is `absent`.

### 3.5 Group 5 — the per-draw front end

`sub_82368C40` (`cod2_recomp.15.cpp:25524`) is the function the draw-list loop calls immediately before the draw.
It prints the whole pass:

| Line | String |
|---|---|
| `:25564` | `0x82053678 Vertex type %i doesn't have the information used by shader %s in material %s` |
| `:25635` | `0x820536C8 ---------- RB_SetupPass` |
| `:25647` | `0x820536E4 ---------- vertex shader: %s version %i.%i` |
| `:25711` | `0x82053710 ---------- pixel shader: %s version %i.%i` |

(and the state map and state-bits lines follow the same pattern). **This is the anchor that knows the material, the
technique and the shader name for the draw that follows** — the CoD2 counterpart of the fork's
`nfsmw_material_nativo.cpp` / `nfsmw_eview_nativo.cpp`.

Around it: `sub_823675C0` (`cod2_recomp.20.cpp:26843`) is `RB_BeginSurface` (string `0x820537CC` at `:26882`),
`sub_82369368` (`cod2_recomp.48.cpp:24973`) is `RB_EndSurface` (string `0x820537F4` at `:25098`). `sub_823686A8`
(`cod2_recomp.43.cpp:25897`) binds materials and samplers — `0x82053408` lightmap at `:26153`, `0x820534FC`
`sampler.outdoor` at `:26235`, `0x82053530` `sampler.sky` at `:26459`. `sub_82367960`
(`cod2_recomp.12.cpp:26398`) resolves the stateMap rule (`0x82053618` at `:26430`).

State, streams and constants, each identified by its own log string:

| Function | Definition | Logs (string address → line) |
|---|---|---|
| `sub_82364708` | `cod2_recomp.55.cpp:25119` | `D3DRS_ALPHATESTENABLE` (`0x82053890` → `:25196`), `D3DRS_CULLMODE` (`0x82053998` → `:25469`) |
| `sub_82364158` | `cod2_recomp.13.cpp:25649` | `D3DRS_ZWRITEENABLE` (`0x82053D48` → `:25710`), `D3DRS_ZENABLE` (`0x82053DA8` → `:25770`) |
| `sub_82365AE0` | `cod2_recomp.60.cpp:26454` | `D3DRS_HALFPIXELOFFSET, 1` (`0x82053858` → `:26644`) |
| `sub_8236DED8` | `cod2_recomp.16.cpp:25847` | `D3DRS_STENCILREF, cmd->refValue` (`0x82052430` → `:25914`) |
| `sub_8236CFA8` | `cod2_recomp.63.cpp:26163` | `D3DRS_CLIPPLANEENABLE` (`0x82052470` → `:26205`), `SetClipPlane` (`0x820524C8` → `:26266`) |
| `sub_82364070` | `cod2_recomp.43.cpp:25763` | `SetSamplerState( samplerIndex, D3DSAMP_MAXANISOTROPY, anisotropy )` (`0x820540A8` → `:25837`) |
| `sub_82364DC8` | `cod2_recomp.48.cpp:24805` | `SetStreamSource( streamIndex, vb, vertexOffset, vertexStride )` (`0x820544B0` → `:24863`) |
| `sub_82364E88` | `cod2_recomp.31.cpp:24963` | `SetIndices( ib )` (`0x82054490` → `:25001`) |
| `sub_82364F68` | `cod2_recomp.30.cpp:25242` | `SetTexture` (`0x82054114` → `:25334`, `0x82054154` → `:25381`), `D3DSAMP_MINFILTER` (`0x82054180` → `:25541`), `D3DSAMP_MAGFILTER` (`0x820541D0` → `:25590`) |
| `sub_82367CB0` | `cod2_recomp.62.cpp:25690` | `SetVertexShaderConstantF( destIndex, data, rowCount )` (`0x82053588` → `:25920`) |
| `sub_82367A60` | `cod2_recomp.24.cpp:25722` | `SetPixelShaderConstantF( destIndex, data, rowCount )` (`0x820535D0` → `:25954`) |
| `sub_82365660` | `cod2_recomp.42.cpp:25201` | `SetViewport( &dxState.viewport )` (`0x82054348` → `:25324`) |
| `sub_82365788` | `cod2_recomp.12.cpp:26297` | the same viewport string (`0x82054348` → `:26352`) |

Note that `sub_82364F68` is a single function covering `SetTexture` and both sampler filters — the fork splits
those across separate hooks, and here they are one address.

The per-model entry points, which are the natural place to hook a whole model instead of a draw, print their own
names:

| Function | Definition | Prints |
|---|---|---|
| `sub_82361918` | `cod2_recomp.17.cpp:26666` | `RB_TessXModelSkinnedCached` (`0x82054520` → `:26728`) |
| `sub_82361788` | `cod2_recomp.38.cpp:25362` | `RB_TessXModelRigid` (`0x8205454C` → `:25423`) |
| `sub_82361CF0` | `cod2_recomp.20.cpp:26648` | `RB_TessStaticModelCached` (`0x82054570` → `:26732`) |
| `sub_82361A78` | `cod2_recomp.49.cpp:26217` | `RB_TessParticleCloud` (`0x820544FC` → `:26278`) |

### 3.6 IM_LOAD — not found

The fork's renderer identifies a shader by hashing the microcode the ring carries in an `IM_LOAD` packet, and the
live `app/src/cod2_nativo_shaders.cpp` does the same. The function that emits those packets in CoD2 was **not
located**. What is known: the packet is PM4 type 3 with opcode `0x27`
(`sdk/include/rex/graphics/xenos.h:1595-1596`), so its header word has `0x27` in bits 8-15 and `0xC0` in the top
byte. The search for that shape was not run here, and the fork's own addresses (`sub_825A37D8`, the copy into the
ring, and `sub_825A2FB8`, the fetch patcher) are meaningless for this game.

**How to find it:** grep the generated code for `lis rX,-16383` (`0xC0010000`) combined with an `ori …,9983`
(`0x27FF`-family) header, or run with the live `cod2_nativo_sistema.cpp` and read its
`[nativo] C5a IM_LOAD …` log line (`app/src/cod2_nativo_sistema.cpp:2815`), which already counts what arrives.

## 4. The shader library — what is still missing, and it is not an anchor

CoD2's shaders are **inside the `.ff` fastfiles**, and the pipeline that turns them into the `.nfsp` library the
renderer loads does not run for this game yet.

State, measured in this repository:

- **No `.nfsp` exists anywhere in the tree.** A recursive search for `*.nfsp` returns nothing. The renderer's
  loader (`app/src/cod2_shader_library.cpp`) will report the library missing, which is what
  `app/src/cod2_nativo_shaders.h` says it does.
- **The scanner cannot see the shaders.** `shaders/cod2_buscar_contenedores.cpp:29` accepts only `.BIN`, `.bin`,
  `.xex` and `.exe`; it walks a directory with `recursive_directory_iterator` and never opens a `.ff`. CoD2's
  shaders are all inside `.ff`, so the scanner currently finds zero of them.
- **Everything downstream of the scan already fits.** `shaders/cod2_contenedor.h` reads the 2005 container layout
  — the `0x102A0E00` / `0x102A0E01` signature pair — which is what this game uses (`AGENTS.md` §7). The translation
  (`shaders/cod2_hlsl.cpp` → XenosRecomp → DXC) and the packing (`shaders/cod2_empaquetar.cpp`) are the fork's and
  are format-driven, not game-driven.
- **What it would take** is one function: inflate the `.ff` stream and scan the decompressed bytes. The format is
  known — zlib, stream starting at `+0x14` after a 20-byte big-endian header (`AGENTS.md` §7). The scanner's inner
  loop (`cod2_buscar_contenedores.cpp:36-50`) already does everything after that; it needs to be handed decompressed
  buffers instead of file contents.
- **A number to check, not to trust.** The repository's `README.md` says a pass over the retail disc found **108**
  valid Xenos shader containers inside the fastfiles. That figure came from a manual pass, not from the tool above,
  and was not re-measured here. Re-run the scan and compare before believing it.

Two properties of the library matter for the anchors above and are already handled by the live code, so a port
should keep them:

1. **Identity is a fingerprint.** `app/src/cod2_shader_library.cpp:78` keys each entry by `XXH3_64bits` of the
   original container; `app/src/cod2_nativo_shaders.cpp:155` keys the microcode the ring carries by `XXH3_64bits`
   of the masked microcode. Both ends must agree on the mask, so the masking rules live with the packer.
2. **Vertex shaders are not identified by their microcode.** D3D reorders the fetches, changes their swizzles and
   nulls the outputs the pixel shader does not read before uploading a VS (`app/src/cod2_nativo_shaders.h`, header
   comment). Their identity has to come from the device objects — which is why anchors 2a/2b (`sub_82384180` /
   `sub_8209BD78`) and 2d (`sub_82098B10`) are not optional for a VS.

## 5. What was verified, what was not, and how to check

**Verified here.**

- The guest-image ↔ address mapping, by two independent paths agreeing on six strings (§0).
- The register-index ↔ MMIO-offset mapping, by five exact hits (§0).
- Every string → function mapping, with `tools/refiere_datos.py`, which reports file and line.
- Every `DEFINE_REX_FUNC` line number in §1, §3 and §4, by building the full symbol → file:line map over all 142
  files and comparing.
- Every call-site count, by grepping the generated code for `__imp__sub_X(ctx, base)`.
- The PM4 headers `0xC0012201`, `0xC0032201` and the type-0 `8450` packet, by reading the instruction pairs.
- That the live native-renderer files in `app/src/` (`cod2_nativo_sistema.cpp`, `cod2_nativo_destinos.cpp`,
  `cod2_nativo_dibujos.cpp`, `cod2_nativo_shaders.cpp`, `cod2_nativo_texturas_pool.cpp`,
  `cod2_shader_library.cpp`) contain **no real guest-address reference**: no `REX_EXTERN(`, no `REX_HOOK`, no
  `__imp__sub_`. The `sub_XXXXXXXX` occurrences in them are comments and log strings (37 of them). The CMakeLists
  comment that claims "four references to a guest address, and three of those are in comments" is about right in
  spirit, but there are 37 textual occurrences, not four.
- That `cod2_buscar_contenedores.cpp` never opens a `.ff` (read in full, 59 lines).

**Not verified.**

| Claim | Why not | How to check |
|---|---|---|
| How per-entry data reaches `sub_82368310` (its second argument) | `sub_82368C40`'s side effects on the argument were not traced | Read `sub_82368C40` (`cod2_recomp.15.cpp:25524`) for stores through its arguments; or hook it and log the argument |
| `sub_820B8B38` is the changed-register emitter | Its run base (`18432 + 6*z`) is not a register index and its stride is 24 bytes per register, not 4 | Explain the 24-byte record, or find its consumer; a run of the game with a hook on it and on the ring would settle it |
| The IM_LOAD function (§3.6) | Not searched by shape | Grep for a type-3 header with opcode `0x27`, or read the live `[nativo] C5a IM_LOAD` log |
| The three shader-creation sites cover every shader | Only call sites of `sub_8209BD78` were counted; a fourth path that builds the object elsewhere would not appear | Hook `sub_8209BD78` and count creations against the number of distinct shaders the game loads; or look for other readers of the container offset `[shader+52]` |
| Begin/EndTiling entry points | The D3D wrapper does not log them, so there is no string to resolve | Find the callers of `sub_8237CE18`/`sub_8237CD08` (tileTop/tileBot); or set `r_tiling` and watch `sub_82365828` |
| That `sub_820A4E48` is ever reached | It has no direct caller — only the init and register tables name it | Set a breakpoint or a hook on it and see whether a frame reaches it; `sub_820A4E68` is the reachable twin if not |
| The 108-container figure | No `.ff` was unpacked in this task | Run the scan over `assets/game_root/*.ff` once the unpack step exists |
| Anything about the game actually running | The game was not started | PC run with a trace hook on `sub_82368310` and `sub_820A4E48`, then a console run |

**Nothing here is confirmed against a live ring.** The strongest claims are the ones where a string, a register
number and a call-site count agree — `sub_820A4E48` (`CP_RB_WPTR`), `sub_82368310` (one call site, both draw
strings), `sub_82384180` (`CreateVertexShader`/`CreatePixelShader`), `sub_820A6FE8` (`0xC0012201` + the `0x2102`
type-0 packet) — and those are still read off the translated code, not off a running process.
