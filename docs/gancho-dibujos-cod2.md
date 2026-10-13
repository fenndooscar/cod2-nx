# Hooking Call of Duty 2's draw submission

> **Corrected after a run: hook the two ring writers, not `sub_82368310`.** Everything below about
> `sub_82368310` was read correctly out of this game's code, and hooking it is still the wrong seam. A build with
> that hook ran 45 s of a cutscene and logged `gancho de dibujo: llamadas 0` while the ring counted 2,813 draws,
> and a second run past the intro logged 0 again: the cutscenes and the menu go through a pair of Direct3D-level
> shims, `sub_820987E8` (indexed) and `sub_82098820` (user pointers), that write the ring themselves and never
> touch `sub_82368310`. Its retry loop also means one call can emit more than one packet.
>
> The seam that works is the two functions every draw of every layer ends in - `sub_820A7228` (buffers) and
> `sub_820A6F30` (user pointers), both of which take the primitive in `r4` and the index count in `r7` and build
> `VGT_DRAW_INITIATOR` as `(count << 16) | 4`. They are hooked in `app/src/cod2_nativo_ganchos.cpp`, and the
> run past the intro paired 8,800 draws with 8,812 hook calls.
>
> Read the rest of this document as the description of the layer *above* the seam, which is still what it says it
> is; §2's recommendation and §3's argument map are the parts that the run disproved.

How to make every draw this game submits leave a record that the native renderer's ring can pair with the
`DRAW_INDX` packet it reads, so that `EmparejarDibujo` (`app/src/cod2_nativo_sistema.cpp:3096`) can identify the
draw it is looking at.

This document specifies the hook only. It writes no code and changes no file.

**Scope.** Everything below is derived from this game's own translated code in `app/generated/default/` and from
its own string table inside `app/out/invitado.bin`. Nothing is inherited from NFS: Most Wanted. Where the previous
game's answer is the only thing available, it is named as such and its unreliability is stated.

**Claim labels.** Every statement is marked **confirmed** (read directly in this game's code or data, with
`file:line`), **probable** (derived by inference from confirmed facts), or **guess** (consistent with the evidence
but not demonstrated).

**Guest-address convention.** `app/out/invitado.bin` is a PE container holding the Xbox 360 image: byte at file
offset `o` is guest address `0x82000000 + o`. ReXGlue hooks receive `uint8_t* base` such that guest address `a` is
at host `base + a`.

---

## 1. The answer, in one page

| Question | Answer | Confidence |
|---|---|---|
| Function to hook | `sub_82368310` (`app/generated/default/cod2_recomp.32.cpp:24895`) | confirmed |
| Call sites | exactly one: `cod2_recomp.56.cpp:26136`, inside `sub_82368F20`'s per-entry loop | confirmed |
| ABI | `r3` = pointer to the draw-args struct; `r4` = pointer to the technique list, **never read** by the callee | confirmed |
| Where the record goes | **before** the original runs | confirmed |
| Primitive | `4` (`D3DPT_TRIANGLELIST` = `kTriangleList`); the game hardcodes it | confirmed |
| Count in `VGT_DRAW_INITIATOR >> 16` | `*(args + 12) * 3`, i.e. `primCount * 3` | confirmed |
| `FuncionDibujo` produced | `kIndexados` when `*(args + 0) != 0`, `kIndexadosUP` when `*(args + 0) == 0` | confirmed |
| `vs` / `ps` | `*(device + 0x4FD8)` / `*(device + 0x3290)`, device = `*(guest 0x83630E60)` | confirmed |
| Shaders current at hook time | yes — `sub_82368C40` binds them immediately before each draw | confirmed |

The one-line argument map:

```
r3 -> struct { +0 variant, +4 firstVertexFromBase, +8 vertexCount, +12 primCount,
               +16 u.buf.baseVertex | u.tess.vertexStride, +20 u.buf.baseIndex }
```

---

## 2. Which function to hook (question 1)

### 2.1 The candidate set, and what is actually reachable

The ring writers are the functions that emit the PM4 type-3 packet with opcode `0x22` (`PM4_DRAW_INDX`). A scan of
all 142 generated translation units for a `lis`/`ori` pair that builds a `0x22xx` type-3 header finds exactly nine
builders. Their direct call sites in the generated code:

| Function | Definition | `DRAW_INDX` header | Direct call sites |
|---|---|---|---|
| `sub_820A6C18` | `cod2_recomp.46.cpp:752` | `:1000-1003`, `:1075-1084` | 1 — `cod2_recomp.45.cpp:834` (inside `sub_820A6F30`) |
| `sub_820A6F30` | `cod2_recomp.45.cpp:770` | via `sub_820A6C18` | 2 — `cod2_recomp.13.cpp:294` (`sub_82098820`), `cod2_recomp.32.cpp:25068` (`sub_82368310`) |
| `sub_820A6FE8` | `cod2_recomp.17.cpp:695` | `:762-765` | **0** |
| `sub_820A7228` | `cod2_recomp.2.cpp:730` | `:791-796` | 2 — `cod2_recomp.65.cpp:306` (`sub_820987E8`), `cod2_recomp.32.cpp:25135` (`sub_82368310`) |
| `sub_820A6908` | `cod2_recomp.9.cpp:783` | `:998-1003`, `:1069-1084` | 2 — `cod2_recomp.36.cpp:773` (`sub_820A6BA8`), `cod2_recomp.63.cpp:36820` (`sub_8247E278`) |
| `sub_820A7478` | `cod2_recomp.13.cpp:781` | `:916-919` | **0** |
| `sub_820A7738` | `cod2_recomp.19.cpp:639` | `:873-875` | **0** |
| `sub_820BD958` | `cod2_recomp.44.cpp:1465` | `:1657-1658` | **0** |
| `sub_820BC2C0` | `cod2_recomp.65.cpp:1461` | `:1633-1634` | **0** |

**confirmed** for every definition line, header line and count above (direct `__imp__sub_…(ctx, base)` occurrences
in `app/generated/default/`).

One of the writers the previous report listed as live, `sub_820A6FE8`, is reached from **no call site at all**
(**confirmed**). The prior report's claim that `sub_820A6FE8` is a live ring writer is therefore **not confirmed**;
it is a sibling of the two that are.

**A caveat that applies to the whole table.** These counts are direct call sites only. A function reached through
a computed pointer (`lwzx r3, rBase, rOffset`, the shape used at `cod2_recomp.56.cpp:26010-26027`) leaves no address
constant in the code, and `tools/refiere_datos.py` finds only address constants. The `(function pointer, metadata)`
tables that a raw byte search turns up — e.g. `0x82069B48` → `sub_820A6FE8`, `0x82069540` → `sub_820987E8` — are
referenced by **no code at all** (`python tools/refiere_datos.py 0x82069B48` → *sin referencias*; same for
`0x82069540` and `0x82069018`), so they are not evidence of reachability either. Indirect reachability cannot be
excluded; it also cannot be assumed.

### 2.2 The choice: `sub_82368310`

`sub_82368310` is the game's D3D-level draw submission for the technique-list path, and it is the right hook for
three reasons, each confirmed:

1. **It is the only place that carries both draw variants.** It prints, as its own static format strings, both D3D
   calls it is translating:
   - `dx.device->DrawIndexedPrimitiveUP( D3DPT_TRIANGLELIST, args->firstVertexFromBase, args->vertexCount, args->primCount, tess.indices, D3DFMT_INDEX16, &tess.verts, args->u.tess.vertexStride )` — guest `0x82053268`, referenced at `cod2_recomp.32.cpp:25015`;
   - `dx.device->DrawIndexedPrimitive( D3DPT_TRIANGLELIST, args->u.buf.baseVertex, args->firstVertexFromBase, args->vertexCount, args->u.buf.baseIndex, args->primCount )` — guest `0x82053328`, referenced at `cod2_recomp.32.cpp:25088`.

   Both strings are **confirmed** by reading `app/out/invitado.bin` at those addresses.

2. **It is the single confirmed entry into the ring's draw writers.** It is the only confirmed caller of both
   `sub_820A6F30` (`:25068`) and `sub_820A7228` (`:25135`) — the two writers whose other callers
   (`sub_82098820`, `sub_820987E8`) have zero call sites themselves.

3. **The ring writer's count is derived from its argument, not from ambient state.** The `×3` that turns the D3D
   primitive count into the index count lives in `sub_82368310` itself (§5), so the hook reads the same number the
   packet will carry, before the packet is built.

### 2.3 Where it sits in the draw loop

`sub_82368310` has exactly one call site, inside the per-entry loop of `sub_82368F20`
(`cod2_recomp.56.cpp:25981`, one call site of its own at `cod2_recomp.11.cpp:26659`, inside `sub_82369070`):

```
cod2_recomp.56.cpp:26110   r7  = u16 at [list + 6]        ; entry count
cod2_recomp.56.cpp:26118   r31 = list + 8                 ; first entry
cod2_recomp.56.cpp:26120   loc_8236900C:
cod2_recomp.56.cpp:26127   sub_82368C40(entry, r27, 0)    ; bind shaders, state, textures
cod2_recomp.56.cpp:26134   sub_82368310(r26, list)        ; the draw
cod2_recomp.56.cpp:26141   r31 += 28                      ; next entry
cod2_recomp.56.cpp:26145   loop while r30 < entry count
```

**confirmed.** Two consequences that matter for pairing:

- The args struct (`r26` = `sub_82368F20`'s `r5`) is **constant across the whole loop**. One technique list is *N*
  draws with identical geometry and identical counts, differing only in the bound shaders. Pairing on
  (primitive, count) alone is ambiguous; the shader pointers are what disambiguates.
- `sub_82368C40` runs immediately before each draw and is where the shaders are bound (§3.2), so the shader state
  the hook reads belongs to that specific draw.

### 2.4 Why not the D3D-level hooks that already exist

`app/src/aparcado/cod2_d3d_trace.cpp` hooks `sub_82593A10`, `sub_82593C50`, `sub_82593588`, `sub_82593940`
(`:631`, `:635`, `:640`, `:644`). Those are the **inherited NFS:MW** D3D entry points. No `DrawVertices` /
`DrawIndexedVertices` string exists in this game's string table — the only draw strings it has are the two indexed
D3D shim strings above plus `SetFVF` (`0x8205383C`), `SetIndices` (`0x82054490`), `SetStreamSource`
(`0x820544B0`), `SetVertexShader` (`0x82053210`), `SetPixelShader` (`0x820531E8`) — so those four hooked addresses
are not this game's draw entry points. The directory name `aparcado` ("parked") matches that.

---

## 3. The argument map (question 2)

### 3.1 The args struct passed in `r3`

`sub_82368310` sets `ctx.r31 = ctx.r3` at `cod2_recomp.32.cpp:24909-24910` and then reads only these fields:

| Offset | Read at | Meaning | Confidence |
|---|---|---|---|
| `+0` | `:24946-24947` | variant flag; `0` → user-pointer path, non-zero → vertex-buffer path | confirmed (flag value and behaviour) |
| `+4` | `:25052-25053`, `:25129-25130` | `args->firstVertexFromBase` (D3D `MinVertexIndex`) | confirmed |
| `+8` | `:25048-25049`, `:25127-25128` | `args->vertexCount` (D3D `NumVertices`) | confirmed |
| `+12` | `:24921-24922`, `:25042-25043`, `:25119-25120` | `args->primCount` (D3D `PrimitiveCount`) | confirmed |
| `+16` | `:25038-25039`, `:25129-25130` | union: `u.tess.vertexStride` (UP) / `u.buf.baseVertex` | confirmed |
| `+20` | `:25127-25128` | `u.buf.baseIndex` (D3D `StartIndex`) | confirmed |

No other field is read. `+0` is only ever compared against zero (`cmpwi cr6,r8,0` at `:24948-24949`, `bne` to the
non-UP branch at `:24950-25151`); the field is never dereferenced or stored. **Confirmed** that it is a boolean
variant flag, from the producer side: the two callers that build this struct both write it as a literal —
`sub_82369100` writes `0` (`cod2_recomp.47.cpp:26324-26325`, `:26332-26333`) and `sub_82361788` writes `1`
(`cod2_recomp.38.cpp:25462-25463`, `:25482-25483`). Its *name* is not in this image: the D3D format strings name
every other field they use and do not name this one, so the natural reading — the presence of a bound vertex buffer,
matching the D3D9 pairing of `DrawIndexedPrimitive` (buffers) with `DrawIndexedPrimitiveUP` (pointers) — is
**probable**, not confirmed.

Corroboration that the flag and the union field agree: `sub_82369100` writes `+16` = `44` or `64`
(`cod2_recomp.47.cpp:26348-26358`) and never writes `+20` — a vertex stride with no base index, the UP shape.
`sub_82361788` writes `+16` = `0` and `+20` = `0` (`cod2_recomp.38.cpp:25464-25469`) — the buffer shape.
**confirmed.**

Evidence per field:

- **`+4` = `firstVertexFromBase`** — UP branch: `lwz r5,4(r31)` at `:25052-25053` → `sub_820A6F30`'s `r5`; inside
  `sub_820A6F30` it becomes `r29` (`cod2_recomp.45.cpp:784-785`) and indexes the source vertices:
  `mullw r11,r29,r30` / `add r4,r11,r25` (`:839-844`) → `memcpy(dst, verts + firstVertexFromBase * stride, …)`.
  Non-UP branch: `lwz r5,16(r31)` at `:25129-25130` → `sub_820A7228`'s `r5`, compared against and stored into
  `11540(ring)` (`cod2_recomp.2.cpp:749-758`) — the base-vertex slot. **confirmed.**
- **`+8` = `vertexCount`** — `lwz r6,8(r31)` at `:25048-25049` → `sub_820A6F30`'s `r6` → `r28`
  (`cod2_recomp.45.cpp:786-787`), used as the `memcpy` length `mullw r5,r28,r30` (`:845-846`).
  **confirmed.**
- **`+12` = `primCount`** — see §5. **confirmed.**
- **`+16`** — UP: `lwz r7,16(r31)` at `:25038-25039`, stored to the outgoing stack slot `84(r1)` at `:25056-25057`,
  which `sub_820A6F30` reads back as `260(r1)` (`cod2_recomp.45.cpp:810-811`) and forwards as its stride `r9`
  (`:816-817`, `:824-825`) → `sub_820A6C18`'s `r9`. Non-UP: `lwz r5,16(r31)` at `:25129-25130` → `baseVertex`.
  The two readings overlap at the same offset, which is why the game's own string calls this a union (`u.tess` vs
  `u.buf`). **confirmed.**
- **`+20`** — `lwz r6,20(r31)` at `:25127-25128` → `sub_820A7228`'s `r6` → `r21` (`cod2_recomp.2.cpp:745-746`),
  then `rlwinm r10,r21,1,0,30` / `add r10,r10,r9` with `r9 = lwz 12(r20)` (`:825-834`): the index-buffer byte
  offset is `baseIndex * 2 + <index buffer base>`. **confirmed.**

### 3.2 `r4` is dead, and the device object

`sub_82368310` never reads its incoming `r4`: the first mention is the assignment `li r4,0` at
`cod2_recomp.32.cpp:24979-24980`, and the only other writes are `ori r4,r5,20` at `:25046-25047` and `li r4,4` at
`:25064-25065` and `:25121-25122`. **confirmed.** The prior report's open question about "how per-entry data
reaches `sub_82368310` (its second argument)" has no content: the caller passes the list pointer
(`cod2_recomp.56.cpp:26130-26131`, `mr r4,r29` where `r29` is the list) and the callee discards it. The per-entry
data reaches the draw through `sub_82368C40`'s side effects on device state, not through `r4`.

The device object is the guest global at `0x83630E60`:

- `lis r11,-31901` / `addi r29,r11,3672` at `cod2_recomp.32.cpp:25005-25010` → `0x83630E58`;
- `lwz r3,8(r29)` at `:25054-25055` → `*(0x83630E60)`, passed to the ring writer as its first argument.
  **confirmed** by the arithmetic on the generated code.

The same object is both the D3D device and the ring. `sub_82098A88` (the pixel-shader setter,
`cod2_recomp.1.cpp:298`) computes the same `0x83630E58` (`:322-327`), loads `8(r31)` (`:359-360`) and passes it as
the device to `sub_8209BB00` (`:361-363`), which stores the shader object at `device + 12944` = `device + 0x3290`
(`cod2_recomp.0.cpp:463-464`) and reads the previous one back from the same offset (`:451-452`).
`sub_82098B10` (the vertex-shader setter, `cod2_recomp.34.cpp:284`) does the same and stores at
`device + 20440` = `device + 0x4FD8` (`:366-369`). **confirmed.**

The inherited `0x4FE8` is not the vertex shader slot: the only read of `20456` in the whole program is
`cod2_recomp.5.cpp:1536-1537`, inside `VdShutdownEngines` (`:1534`). **confirmed.**

### 3.3 The shader pair is current at hook time

`sub_82368C40` (`cod2_recomp.15.cpp:25524`, `r29 = r3` = the entry at `:25536-25537`) binds the pair:

```
cod2_recomp.15.cpp:25879   r5 = lwz 12(r29)     ; the entry's pixel-shader binding record
cod2_recomp.15.cpp:25881   r3 = lwz 12(r5)      ; the shader object
cod2_recomp.15.cpp:25883   bl sub_82098A88      ; SetPixelShader  -> device + 0x3290
cod2_recomp.15.cpp:25886   r4 = lwz 8(r29)      ; the entry's vertex-shader binding record
cod2_recomp.15.cpp:25888   r3 = lwz 12(r4)      ; the shader object
cod2_recomp.15.cpp:25890   bl sub_82098B10      ; SetVertexShader -> device + 0x4FD8
```

**confirmed, with one correction.** Because `sub_82368C40` is called one instruction-block before `sub_82368310` in
the same loop iteration (`cod2_recomp.56.cpp:26127` then `:26134`), the values at `device + 0x3290` and
`device + 0x4FD8` when the hook runs are exactly the pair bound for that draw. Reading the device slots is
therefore sufficient *as far as the binding goes*.

**The correction: "no second hook is required" is wrong for the pixel stage.**
`docs/pixel-shaders-cod2.md` traces it. The device slot holds the right object, but the port can only turn that
object into a library entry if it saw the object being created, and it only hooks the vertex constructor:
`sub_8209BD78` is the worker behind the game's own `dx.device->CreateVertexShader( mtlShader->program, &shader->vs )`
and the pixel stage goes through a separate `sub_8209B9D0`, behind `CreatePixelShader`. A run with only the vertex
hook created 61 shaders, every one of them a vertex one, and every draw record came out with `ps = 0`. Both
constructors are now hooked. Also: `sub_82098A88` is *not* what writes `device + 0x3290` — it calls
`sub_8209BB00` (`cod2_recomp.0.cpp:463-464`), the only writer of that slot in the image, and two of its three
callers pass 0 to unbind.

*Upgrade path, if the device slots ever prove unreliable:* hook `sub_82368C40` as well and remember the pair it
binds at `:25881-25892`, then read the remembered pair in the draw hook. This costs a second hook and buys nothing
today — mention it only because it is the cheap fallback if the slots turn out to be clobbered by a path not
translated here.

### 3.4 What the hook must write

The contract is `args[0] == VGT_DRAW_INITIATOR & 0x3F` and `CuentaDelRegistro(registro) == VGT_DRAW_INITIATOR >> 16`.
With the map above, and reading `RegistroDibujo`'s four free words
(`app/src/cod2_nativo_ganchos.h:28-35`) as the hook's own to define:

| Field | Value | Source |
|---|---|---|
| `vs` | `LeerBE(base + device + 0x4FD8)` | device slot, confirmed |
| `ps` | `LeerBE(base + device + 0x3290)` | device slot, confirmed |
| `args[0]` | `4` | the primitive, §4 |
| `args[1]` | `LeerBE(base + r3 + 12)` = `primCount` | raw struct field |
| `args[2]` | `args[1] * 3` = the index count | §5 |
| `args[3]` | `LeerBE(base + r3 + 8)` = `vertexCount` | diagnostics only |
| `funcion` | `kIndexadosUP` if `LeerBE(base + r3 + 0) == 0`, else `kIndexados` | §6 |

with `device = LeerBE(base + 0x83630E60)`, guarded against zero.

`CuentaDelRegistro` (`app/src/cod2_nativo_sistema.cpp:3380-3392`) must return `args[2]` for **both** indexed
variants. Its current `kIndexados → args[3]` and `kVerticesUP → args[1]` arms are the inherited NFS:MW field
meanings and are wrong for this mapping; `kVertices` and `kVerticesUP` are never produced by this hook (§6), so
their arms are dead but should be made explicit rather than left to look meaningful.

---

## 4. Primitive-type translation (question 3)

**The game never varies it, and the translation is the identity.**

1. Both D3D shim strings hardcode the literal `D3DPT_TRIANGLELIST` (guest `0x82053268`, `0x82053328`) with no
   conversion specifier — the primitive is a compile-time constant of this shim, not a parameter. **confirmed.**
2. The ring writer that `sub_82368310` reaches builds `VGT_DRAW_INITIATOR` with the primitive hardcoded to 4:
   - `sub_820A7228` (non-UP): `rlwinm r8,r22,16,0,15` at `cod2_recomp.2.cpp:821-822`, then `ori r30,r8,4` at
     `:827-828`. `r22` is the index count. **confirmed.**
   - `sub_820A6C18` (UP, via `sub_820A6F30`): `rlwinm r30,r29,16,0,15` at `cod2_recomp.46.cpp:916-917`, then
     `ori r28,r30,4` at `:954-955`. **confirmed.**
3. `4` is `xenos::PrimitiveType::kTriangleList` (`sdk/include/rex/graphics/xenos.h:49`), and `D3DPT_TRIANGLELIST`
   is `4` in D3D9. **confirmed** for the Xenos side; the D3D9 side is the platform definition, not something this
   image can show.

So `args[0] = 4` is not a guess about a mapping table: it is the value the ring will carry, read out of the
writer's own `ori`.

**Where a translation table would be needed, and why it is not.** The only place in the whole image where a
caller-supplied primitive reaches `VGT_DRAW_INITIATOR` is `sub_820A6FE8`:
`clrlwi r9,r24,26` (= `r24 & 0x3F`) / `or r11,r10,r9` / `ori r10,r11,128` at `cod2_recomp.17.cpp:830-839`, with
`0x80` setting `source_select = 2` (`kAutoIndex`). That function has **zero** call sites and no data reference
(§2.1), so no draw this hook can see goes through it. **confirmed.**

Two side notes the record should not lose:

- `D3DPT_*` and `PrimitiveType` agree on 1–6 (`kPointList` 1, `kLineList` 2, `kLineStrip` 3, `kTriangleList` 4,
  `kTriangleFan` 5, `kTriangleStrip` 6) and disagree above that (`kRectangleList = 0x08`, `kLineLoop = 0x0C`,
  `kQuadList = 0x0D`, `kQuadStrip = 0x0E` have no D3D9 counterpart). A pass-through is correct *here* and would not
  be correct for a hypothetical draw using one of those. **confirmed** for the Xenos values
  (`sdk/include/rex/graphics/xenos.h:44-56`).
- `args[0]` carries only the primitive, because that is all the contract asks. The initiator's other bits are
  `source_select = 0` (`kDMA`), `major_mode = 0`, `index_size = 0` (`kInt16`) — the two writers set the whole word
  as `(count << 16) | 4`, so those three are zero by construction. If the renderer later needs `index_size`, it
  must read it from the ring, not from the record. **confirmed**
  (`sdk/include/rex/graphics/registers.h:311-331`).

---

## 5. Which `args[1..3]` holds the count (question 4)

`VGT_DRAW_INITIATOR >> 16` is `num_indices`, and `num_indices` is `*(args + 12) * 3` in both branches.
**confirmed**, by tracing each branch end to end:

**Non-UP branch** (`loc_82368464`, `cod2_recomp.32.cpp:25083`):

```
:25119   r11 = lwz 12(r31)          ; primCount
:25125   r10 = r11 << 1             ; rlwinm r10,r11,1,0,30
:25131   r7  = r11 + r10            ; = primCount * 3
:25121   r4  = 4
:25127   r6  = lwz 20(r31)          ; baseIndex
:25129   r5  = lwz 16(r31)          ; baseVertex
:25133   bl  sub_820A7228(ring, 4, r5, r6, r7)
```

then `sub_820A7228`: `r18 = r7` (`cod2_recomp.2.cpp:747-748`), `r22 = r18` clamped to 65532 when it exceeds 65535
(`:808-817`), `r8 = r22 << 16` (`:821-822`), `r30 = r8 | 4` (`:827-828`) → the DRAW_INITIATOR payload dword.

**UP branch** (`loc_823683EC`, `cod2_recomp.32.cpp:25017`):

```
:25042   r11 = lwz 12(r31)          ; primCount
:25058   r7  = r11 << 1
:25062   r7  = r11 + r7             ; = primCount * 3
:25064   r4  = 4
:25038   r7' = lwz 16(r31)          ; u.tess.vertexStride -> stack 84(r1)
:25066   bl  sub_820A6F30(ring, 4, 4(r31), 8(r31), r7, idx, …)
```

then `sub_820A6F30`: `mr r27,r7` (`cod2_recomp.45.cpp:788-789`), `mr r7,r27` (`:820-821`) →
`sub_820A6C18`: `mr r29,r7` (`cod2_recomp.46.cpp:769`), `rlwinm r30,r29,16,0,15` (`:916-917`),
`ori r28,r30,4` (`:954-955`) → the DRAW_INITIATOR payload dword.

Independent corroboration that this number is an **index** count and not a vertex count: the UP path copies the
index array with `r5 = ((r27 * 2) + 3) & ~3` bytes (`cod2_recomp.45.cpp:852-859`) — `r27` 16-bit indices rounded up
to a word. **confirmed.**

So the answer is: **`args[2]`, and it must be `*(args + 12) * 3`.** `*(args + 12)` is the D3D primitive count; the
index count is three times it, and `*(args + 8)` (`vertexCount`) is *not* the number the ring reads.

The SDK side agrees on which field of the initiator this is: `sdk/src/graphics/command_processor.cpp:1404-1405`
assigns `index_buffer_info.count = vgt_draw_initiator.num_indices`, and `sdk/src/graphics/packet_disassembler.cpp:220-221`
reads the same bits from the packet (`index_count = dword1 >> 16`, `prim_type = dword1 & 0x3F`). **confirmed.**

---

## 6. Which `FuncionDibujo` values CoD2 produces (question 5)

**Only `kIndexados` and `kIndexadosUP`.** `kVertices` and `kVerticesUP` are not produced by this hook.
**confirmed**, from both ends of the call.

*Consumer side.* The selector is `*(args + 0)`, compared against zero at `cod2_recomp.32.cpp:24946-24951`. Zero
falls through to the user-pointer branch (`loc_823683EC`, `:25017`, ending at `sub_820A6F30` `:25068`); non-zero
branches to `loc_82368464` (`:25083`, ending at `sub_820A7228` `:25135`). Both branches are *indexed* draws: both
format strings say `DrawIndexed*`, both pass an index count (§5), and both writers emit a DRAW_INDX with
`source_select = 0` (`kDMA`, `ori …,4`). There is no non-indexed path out of `sub_82368310`. **confirmed.**

*Producer side.* Two of the six callers of `sub_82369070` build this struct on their own frame, and each writes a
different literal into `+0`:

| Producer | `+0` | `+16` / `+20` | Produces |
|---|---|---|---|
| `sub_82369100` (`cod2_recomp.47.cpp:26287`) | `0` (`:26324-26325`, `:26332-26333`) | `44` or `64` / not written (`:26348-26358`) | `kIndexadosUP` |
| `sub_82361788` (`cod2_recomp.38.cpp:25362`) | `1` (`:25462-25463`, `:25482-25483`) | `0` / `0` (`:25464-25469`) | `kIndexados` |

**confirmed.** So both variants really occur, and the flag is a literal `0`/`1`, not a value that has to be
interpreted.

The mapping onto the header's enum (`app/src/cod2_nativo_ganchos.h:21-26`) is by *shape*: `kIndexadosUP` for the
user-pointer branch (stride in the union, no vertex buffer), `kIndexados` for the buffer branch. The header's
argument orders (`DrawVerticesUP(dispositivo, tipo, cuenta, datos, zancada)`) are the previous game's and do not
describe this call — see §3.4. **confirmed** as a shape match; the enum's own comments are **not** evidence.

**What would produce `kVertices`/`kVerticesUP`, and why it is not here.** The only non-indexed draw shape in the
image is `sub_820A6908`'s auto-index path: `li r10,134` then `rlwimi r10,r27,16,0,15` at
`cod2_recomp.9.cpp:1000-1005` builds `(r27 << 16) | 0x86`, i.e. `prim_type = 6` (`kTriangleStrip`) with
`source_select = 2` (`kAutoIndex`) and the count taken from the **vertex** count of a 20-byte-stride array. Its only
confirmed caller chain is `sub_820A6BA8` (`cod2_recomp.36.cpp:729`: count in `r5` → `r30` at `:743-744`, data
pointer in `r6` → `r29` at `:745-746`, `li r6,20` at `:761-762`, `li r4,6` at `:767-768`, call at `:771-773`,
`memcpy` of `count * 20` bytes at `:778-784`) ← `sub_82090098` (`cod2_recomp.3.cpp:3`, which passes `r4 = 6`,
`r5 = 4`, `r7 = 20` at `:14-19`), and `sub_82090098` has **zero** call sites. It is not reachable from
`sub_82368310` in any case. **confirmed** for the counts and the encoding; **not determined** whether it runs,
because indirect dispatch cannot be excluded.

---

## 7. Before or after the original, and what the hook may touch (question 6)

**Before.** `sub_82368310` destroys `r3` before its first call: `li r3,0` at `cod2_recomp.32.cpp:24981-24982`,
immediately before `bl sub_82364DC8` at `:24983-24985`. By the time the function returns, `r3` holds the return
value of `sub_8236D0D0` (`:25075-25077`) and `r4` is long gone. An after-hook could not recover the args struct.
**confirmed.**

The record must also be in the marker *before* the packets are written, because the ring thread can consume a
`DRAW_INDX` as soon as it appears; that is the whole point of the marker fast path in `EmparejarDibujo`
(`app/src/cod2_nativo_sistema.cpp:3112-3113`). **confirmed** by construction of the consumer.

**Registers.** The function's own prologue is `__savegprlr_25` at `cod2_recomp.32.cpp:24902`, so it saves
`r25`–`r31` and does **not** save `r3` or `r4` — those are caller-owned, and a hook may read them freely.
**confirmed.** What the hook must not do:

- **Do not write `ctx.r3`.** It is the args-struct pointer the original dereferences (`mr r31,r3` at `:24909-24910`,
  then every field read). Writing it changes what the original draws. **confirmed.**
- **Do not write any other `ctx.rN`.** The original reads `r1` (its frame), `r3`, `r28`, `r29`, `r30`, `r31`
  throughout; there is no register it can spare. Reading `ctx.r3` and dereferencing `base + ctx.r3.u32` is safe and
  is all that is needed. **confirmed.**
- **Do not touch `ctx.r1` (the guest stack).** The original does `stwu r1,-160(r1)` at `:24903-24906` and restores
  with `addi r1,r1,160` + `__restgprlr_25` on both exits (`:25078-25081`, `:25144-25147`). It is balanced relative
  to its entry, so a hook that left `r1` moved would still unwind — but the ReXGlue hook trampoline owns that
  decision; the hook itself should not allocate guest stack. **confirmed** for the frame arithmetic.
- **Do not rely on `ctx.lr`.** The original assigns `ctx.lr` before every call (`:24984`, `:25034`, `:25076`, …), so
  any value the hook reads there is stale. **confirmed.**
- **Do not call guest code from the hook.** The generated code is not reentrant with respect to `ctx`, and the
  original is mid-frame. **probable** (nothing in the evidence forbids it, but there is no case for doing it).

The pattern this implies is the one `app/src/aparcado/cod2_d3d_trace.cpp:602-629` already uses — read the
arguments, note the record, then `original(ctx, base)` — with `DecidirVegetacion` (`app/src/cod2_nativo_ganchos.cpp:784`)
called before the original and `TerminarDibujo` (`:377`) after it.

---

## 8. What can go wrong silently (question 7)

Ordered by how quiet the failure is.

1. **The ring writer clamps or wraps the count, so the record and the packet disagree.** `sub_820A7228` clamps
   `num_indices` to 65532 when it exceeds 65535 (`cod2_recomp.2.cpp:810-817`); `sub_820A6C18` does not clamp at all
   — `rlwinm r30,r29,16,0,15` (`cod2_recomp.46.cpp:916-917`) simply masks to 16 bits, so a count above 65535
   **wraps**. A record built as `primCount * 3` therefore stops matching the ring for any draw with more than
   21,844 triangles (clamped path) or 21,845 (wrapped path). Nothing logs; `EmparejarDibujo` just falls through to
   `UsarIdentidadDelAnillo()` (`app/src/cod2_nativo_sistema.cpp:3169`). **confirmed** for both mechanisms.
2. **One technique list is *N* identical-count draws.** The args struct is constant across `sub_82368F20`'s loop
   (`cod2_recomp.56.cpp:26120-26145`), so *N* consecutive records can carry the same `args[0]` and `args[2]`. Any
   pairing rule that keys on (primitive, count) alone is ambiguous by construction; `ShadersCoherentes`
   (`app/src/cod2_nativo_sistema.cpp:3334`) is load-bearing, not a refinement. **confirmed.**
3. **The retry loop can re-emit.** Both branches of `sub_82368310` re-run their whole body while the flag at
   `31228(r28)` is non-zero (`cod2_recomp.32.cpp:25069-25074` and `:25136-25141`). One hook call can therefore
   correspond to more than one emitted packet, or to a packet emitted after a ring wrap. The 1 record : 1 packet
   assumption is **not determined** — it needs a run with a trace on both sides.
4. **A stale shader pair if a draw ever arrives from outside `sub_82368F20`.** The device slots are current only
   because `sub_82368C40` binds them one block earlier (`cod2_recomp.15.cpp:25879-25892`). Today there is no other
   confirmed caller of `sub_82368310`, but the retry loop in item 3 is a second execution of the same call, and
   any future indirect entry would record whatever was last bound. **probable.**
5. **Reading `0x4FE8` instead of `0x4FD8` is unrecoverable and silent.** `0x4FE8` is non-null for the whole session
   (it is read by `VdShutdownEngines`, `cod2_recomp.5.cpp:1536-1537`), so every record would carry a garbage
   non-null `vs`; `ShadersCoherentes` would reject every candidate and every draw would fall back to the ring's own
   identity — no error, no log line, no pairing. **confirmed** for the offset; **probable** for the consequence.
6. **A null device pointer.** `*(0x83630E60)` is guest bss and is zero until the device exists. If the hook runs
   before that, `base + device + 0x4FD8` reads low memory and produces garbage shader objects. The existing
   `AnotarDibujo` already guards (`if (!dispositivo || …) return;`, `app/src/cod2_nativo_ganchos.cpp:270-272`);
   the guard must be kept. **confirmed.**
7. **`+0` is only ever compared with zero, so any third value would be silently mis-classified.** The field is
   never dereferenced (`cod2_recomp.32.cpp:24946-24951`), and the two producers that build the struct write the
   literals `0` and `1` (`cod2_recomp.47.cpp:26332-26333`, `cod2_recomp.38.cpp:25482-25483`). A future `2` would
   be treated as the buffer path with `+20` never written, and nothing would report it. **confirmed** as
   behaviour; the field's *name* is **probable**.
8. **A future draw that uses a primitive other than 4 would silently mis-pair.** The record's `args[0]` is a
   constant in the specification, while the ring's `prim_type` is a constant in the writer
   (`cod2_recomp.2.cpp:827-828`, `cod2_recomp.46.cpp:954-955`). Both being constants today is why the equality
   holds; if either side ever becomes a variable, the check in `EmparejarDibujo` stops matching without any signal.
   **probable.**
9. **`sub_820A6FE8` and the other six zero-call-site writers are unaccounted for.** If any of them runs via an
   indirect dispatch this analysis cannot see, it emits a `DRAW_INDX` that no record covers — including one with a
   caller-supplied primitive (`cod2_recomp.17.cpp:830-831`). The pairing would simply not find a record.
   **not determined.**

---

## 9. What I verified and what I did not

### Verified programmatically

Every citation in this document was re-read from disk after writing, at the line numbers given. The mechanical
checks, and their results:

| Check | Method | Result |
|---|---|---|
| `sub_82368310` has exactly one call site | grep for `sub_82368310` across `app/generated/default/` | 1 hit: `cod2_recomp.56.cpp:26136` |
| Call-site counts for all nine `DRAW_INDX` builders | grep for `__imp__sub_…(ctx, base)` per function | table in §2.1 |
| Which function contains each call site | walk back to the nearest `DEFINE_REX_FUNC` | `sub_82368310` → `sub_82368F20`; `sub_820A7228` → `sub_82368310`, `sub_820987E8`; `sub_820A6F30` → `sub_82368310`, `sub_82098820` |
| The nine `0x22xx` headers and their payload counts | scripted `lis`/`ori` pairing over all 142 files | 9 builders, listed in §2.1 |
| The two D3D format strings and their addresses | byte search in `app/out/invitado.bin` | `0x82053268` (UP), `0x82053328` (indexed) |
| `r4` is dead in `sub_82368310` | all `r4` occurrences between `:24895` and `:25160` | first write `li r4,0` at `:24979-24980`; no read |
| `r4` is dead in `sub_820A6F30`, `sub_820A6C18`, `sub_820A7228` | same, per function | first write precedes any read in all three |
| `0x4FD8` = VS slot, `0x3290` = PS slot, `0x4FE8` = decoy | all occurrences of `20440`, `12944`, `20456` across the generated code | 20 hits, classified in §3.2 |
| The device global | arithmetic on `lis -31901` + `addi 3672` + `lwz 8` | `0x83630E58` → `*(0x83630E60)` |
| `sub_820A6FE8` is unreachable | 0 call sites **and** `tools/refiere_datos.py 0x82069B48` | *sin referencias* |
| The `(pointer, metadata)` tables are not evidence | `tools/refiere_datos.py` on `0x82069B40`, `0x82069018`, `0x82069540` | *sin referencias* for all three |
| The SDK side of the initiator layout | read `sdk/src/graphics/command_processor.cpp:1359-1362`, `:1404-1405`; `sdk/src/graphics/packet_disassembler.cpp:213-238`; `sdk/include/rex/graphics/registers.h:311-331`; `sdk/include/rex/graphics/register_table.inc:539`; `sdk/include/rex/graphics/xenos.h:44-56`, `:735-738` | matches §4, §5 |
| The consumer's current field meanings | read `app/src/cod2_nativo_sistema.cpp:3106-3108`, `:3380-3392`; `app/src/cod2_nativo_ganchos.h:21-35`, `:205`; `app/src/cod2_nativo_ganchos.cpp:56-65`, `:268-282` | §3.4 |

### Not determined, with the method that would settle it

| Item | Why it is open | How to settle it |
|---|---|---|
| Whether one `sub_82368310` call emits one `DRAW_INDX` | the retry loops at `:25069-25074` / `:25136-25141` re-run the body; the flag's meaning was not traced | trace both `sub_82368310` and the ring writer on a PC run and compare counts |
| What `*(args + 0)` actually is | only ever compared with zero; not named by the game's strings | read the struct's producer: the callers of `sub_82369070` (`cod2_recomp.11.cpp:26659` and the five others) write this struct; the field's writer would name it |
| Whether the indirect-only functions (`sub_820987E8`, `sub_82098820`, `sub_82090098`, `sub_820A6FE8`, `sub_820A7478`, `sub_820A7738`) run | they have no direct call site and no data reference, but computed-pointer dispatch leaves no address constant | runtime trace on the ring writers, or a breakpoint on each on a PC run |
| Whether `VGT_DRAW_INITIATOR >> 16` equals `primCount * 3` at consumption time | derived from the writer's own arithmetic, not observed | a PC run comparing `EmparejarDibujo`'s `cuenta` against the record |
| Whether the ring writer's clamp/wrap ever triggers | needs a real draw with > 21,844 triangles | instrument the count in the hook and log maxima |
| Whether the shader objects at `device + 0x4FD8` / `+0x3290` are the same objects the native renderer keys on | the renderer's shader identity path was not read for this document | compare against `sub_82098A88`/`sub_82098B10`'s callers and the renderer's shader lookup |
| Nothing at all about the game running | the game was not started; no ring was observed | PC run with ReXGlue's emulated GPU, then the console |

**Not verified here, and not claimed:** that the record produced by this hook ever matches a real ring entry. Every
statement above is about what the code will do; the pairing itself has not been observed.
