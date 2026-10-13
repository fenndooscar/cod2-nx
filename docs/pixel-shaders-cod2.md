# Pixel shaders in Call of Duty 2: who creates them, and how the renderer should name the bound one

> **Corrects `docs/gancho-dibujos-cod2.md` §3.2 and §3.3.** That document says the device slots
> `*(device + 0x4FD8)` / `*(device + 0x3290)` are the bound pair and that "reading the device slots is therefore
> sufficient; no second hook is required". The offsets are right and the argument holds for the **vertex** stage.
> It does not hold for the **pixel** stage: the port hooks only the vertex constructor, so no pixel-shader object
> ever reaches the port's object→library map, and the run's `C5b … PS=0 (conocidos 0)` is that gap, not a game
> without pixel shaders. The library built from this game's own fastfiles holds 653 pixel containers.

Why the port's native renderer sees `VS=61` shaders created and `PS=0`, why `device + 0x3290` reads 0 at draw
time, and which of the three candidate identity paths can actually name the pixel shader bound for a draw.

This document specifies nothing and changes no file.

**Scope.** Everything below is read out of this game's own translated code in `app/generated/default/` and out of
its own string table inside `app/out/invitado.bin`. Nothing is inherited from NFS: Most Wanted.

**Claim labels.** Every statement is marked **confirmed** (read directly in this game's code or data, with
`file:line`), **probable** (derived by inference from confirmed facts), or **guess** (consistent with the evidence
but not demonstrated).

**Guest-address convention.** `app/out/invitado.bin` is a PE container holding the Xbox 360 image: byte at file
offset `o` is guest address `0x82000000 + o`. ReXGlue hooks receive `uint8_t* base` such that guest address `a` is
at host `base + a`.

---

## 1. The answers, in one page

| Question | Answer | Confidence |
|---|---|---|
| Does the game create pixel shaders through something other than `sub_8209BD78`? | Yes. `sub_8209B9D0` (`app/generated/default/cod2_recomp.22.cpp:403`) | confirmed |
| Is `sub_8209BD78` the vertex-shader constructor? | Yes — it is the worker behind the game's own `dx.device->CreateVertexShader( mtlShader->program, &shader->vs )` | confirmed |
| Which function is the pixel one, and where? | `sub_8209B9D0`, guest `0x8209B9D0`, `cod2_recomp.22.cpp:403`; behind `dx.device->CreatePixelShader( mtlShader->program, &shader->ps )` | confirmed |
| What does the pixel constructor receive? | `r3` = the contiguous 2005 container (first word `0x102A0E00`, whose low 7 bits are 0, `shaders/cod2_contenedor.h:63-66`); returns the shader object in `r3`, or 0 | confirmed |
| Its call sites | 4: `cod2_recomp.7.cpp:22`, `cod2_recomp.7.cpp:27161`, `cod2_recomp.10.cpp:35256`, `cod2_recomp.10.cpp:35341` | confirmed |
| Where the 653 library pixel containers are created | only through those 4 sites; none of them is hooked by the port | confirmed |
| Who writes `device + 0x3290` | `sub_8209BB00` (`cod2_recomp.0.cpp:463-464`) — the **only** writer in the image | confirmed |
| Who writes `device + 0x4FD8` | `sub_82098B10` (`cod2_recomp.34.cpp:368`), `sub_8209BFE8` (`cod2_recomp.41.cpp:474`), `sub_822D1648` (`cod2_recomp.47.cpp:20312`), `sub_8247E278` (`cod2_recomp.63.cpp:36596`) | confirmed |
| Callers of `sub_82098A88` | 3: `sub_82364F60` and `sub_82401610` pass **0** (unbind/reset); `sub_82368C40` passes a real object | confirmed |
| Why `device + 0x3290` reads 0 | the premise is wrong (the writer is `sub_8209BB00`, which has 5 call sites, 3 passing real objects); a 0 means the last write was an **unbind** — see §3.4 | probable |
| What the renderer should use for the pixel shader | the ring's own `IM_LOAD` / `IM_LOAD_IMMEDIATE` packets, which the port already parses | probable |
| Globals `0x83D31568` / `0x83D3156C` | the *setter caches* of `sub_82098A88` / `sub_82098B10`, not the bound shaders, and blind to the device-vtable path | confirmed |
| `device + 0x3290` as an identity source | right object, unusable today: the port has no pixel-shader creation hook, so no PS object is in its map | confirmed |

The one-line picture:

```
sub_82384180  stage byte *(*(0x82975264)+10)   container *(*(0x82975264)+4)
     == 0  ->  sub_8209BD78   "CreateVertexShader( mtlShader->program, &shader->vs )"
     != 0  ->  sub_8209B9D0   "CreatePixelShader( mtlShader->program, &shader->ps )"
```

---

## 2. Where a shader object comes from: the two constructors

### 2.1 `sub_8209BD78` is the vertex one, and the game's own strings say so

`sub_82384180` (`cod2_recomp.7.cpp:27056`) is the game's shader-creation entry point. It loads one global object
and branches on a byte inside it:

```
cod2_recomp.7.cpp:27068-27069   lis r11,-32105                  ; 0x82970000
cod2_recomp.7.cpp:27080-27081   lwz r30,21076(r11)              ; r30 = *(0x82975264), the material-shader object
cod2_recomp.7.cpp:27082-27086   lbz r11,10(r30) / cmplwi / bne  ; the stage selector, *(r30+10)
```

- `*(r30+10) == 0` → the branch that logs `0x82051438` and calls `sub_8209BD78`:
  `cod2_recomp.7.cpp:27088-27091` loads `0x82051438`, `:27113-27114` loads `r3 = *(r30+4)`, `:27115-27117` calls,
  `:27120-27121` stores the result at `*(r29+0)`.
- otherwise → the branch that logs `0x82051480` and calls `sub_8209B9D0`:
  `cod2_recomp.7.cpp:27132-27135`, `:27157-27158`, `:27159-27161`, `:27164-27165`.

Those two addresses are the game's own source lines, still in the image as strings:

| Guest address | File offset | String |
|---|---|---|
| `0x82051438` | `0x51438` | `dx.device->CreateVertexShader( mtlShader->program, &shader->vs )\n` |
| `0x82051480` | `0x51480` | `dx.device->CreatePixelShader( mtlShader->program, &shader->ps )\n` |

**confirmed** — byte-read out of `app/out/invitado.bin`; the third string in the same block,
`dx.device->CreateVertexDeclaration( elemTable, &decl )` at file offset `0x514C4` (guest `0x820514C4`), is the
neighbour that fixes the block's identity. The `bl 0x822897e8` that consumes them (`:27109-27111`, `:27153-27155`) is the same
"log this source line" call the game makes everywhere else, and it sits inside a retry loop.

So: **`sub_8209BD78` is `CreateVertexShader`'s worker and `sub_8209B9D0` is `CreatePixelShader`'s.**
**confirmed.**

### 2.2 The two constructors are twins, differing only in the object layout

Both take the container in `r3` and return the object in `r3`.

| | `sub_8209BD78` (vertex) | `sub_8209B9D0` (pixel) |
|---|---|---|
| Definition | `cod2_recomp.36.cpp:366` | `cod2_recomp.22.cpp:403` |
| `r31 = r3` (container) | `:379-380` | `:416-417` |
| `*(c+0) & 0x7F` → offset 52 or 592 | `:383-400` | `:420-437` |
| allocator `sub_822E12E8(*(c+4)+offset, 0x24800000)` | `:401-409` | `:438-446` |
| zero the header | `memset(obj, 0, **592**)` `:421-431` | `13` dwords at `:462-475`, again at `:514-529` |
| copy the container to | `obj + **592**` `:432-440` | `obj + **52**` `:476-482` |
| second allocator (the physical half, `*(c+8)`) | tag `0xA0800000` `:441-447` | tag `0xC0A00000` `:483-489` |
| copy the physical half | `:462-471` | `:504-513` |
| finish | `sub_820B39E8`, `sub_8209BCA8`, `*(obj+588) \|= 0x6000` `:472-493` | `*(obj+12) = buf`, `*(obj+4) = 1`, `*(obj+28) \|= 0xC0000000` `:530-543` |
| return | `r3 = obj`, or 0 on failure `:410-419` | `r3 = obj`, or 0 on failure `:447-456` |

**confirmed** for every row.

The layout the rest of the game expects follows from those two offsets, and the game's own state builder proves
it: `sub_820B9AC8` (`cod2_recomp.7.cpp:1343`) loads **both** slots and derives each program from its own base —

```
cod2_recomp.7.cpp:1360-1361   r30 = *(device+12948)   ; vertex object
cod2_recomp.7.cpp:1368-1373   r11 = *(r30+612); ... addi r8,r11,592   ; vertex program at obj + *(obj+612) + 592
cod2_recomp.7.cpp:1362-1363   r25 = *(device+12944)   ; pixel object
cod2_recomp.7.cpp:1376-1381   r11 = *(r25+72);  ... addi r24,r11,52    ; pixel program  at obj + *(obj+72)  + 52
```

**confirmed.** `+612` is `592 + 20` and `+72` is `52 + 20`: both are the container copy's own `+20` field, read
relative to the copy's base. This is the cleanest single proof that `12944` is the pixel slot and `12948` the
vertex one, and it is independent of the setter code.

`sub_8209B9D0` is also the **only** function in the image that allocates with tag `0xC0A00000`: the immediate
`-19072` (as `lis`) occurs in a `lis r4` position in exactly one place, `cod2_recomp.22.cpp:483-484`.
**confirmed.** The vertex constructor's second-alloc tag `0xA0800000` (`-23168`) occurs in two places,
`cod2_recomp.36.cpp:441` (the constructor) and `cod2_recomp.12.cpp:848` (a ring-buffer setup for sizes 96 and 32,
not a shader) — so that tag alone is not unique, and the string in §2.1 is what settles the vertex side.
**confirmed.**

### 2.3 The stage is chosen by the caller, not by the container

`sub_8209BD78` reads the container's first word and masks it to 7 bits
(`cod2_recomp.36.cpp:383-388`), turns that into `0` or `-1` (`:389-395`) and then into `52` or `592`
(`:396-400`). `sub_8209B9D0` does the same (`cod2_recomp.22.cpp:420-437`). **Neither branches on the result** —
there is no conditional jump after the mask in either function, and the only use of the value is the allocation
size. **confirmed.**

Two consequences worth stating:

1. `sub_8209BD78` is vertex-only **in its use, not in its code**. Handed a pixel container it would allocate
   `*(c+4)+52` and then zero 592 bytes at `:421-431`; those two are only safe together while `*(c+4) >= 540`.
   Nothing in the image guards it. **confirmed** as arithmetic, **probable** as a hazard (no run has been made
   with a pixel container reaching it).
2. No code in the image compares anything against the literal `0x102A0E00` / `0x102A0E01`: a grep for those two
   words and their decimal forms (`268640768` / `268640769`) over `app/generated/default/*.cpp` and `*.h` returns
   nothing. The stage decision is data-driven (`*(r30+10)`, §2.1), so grepping for the signature can never find
   it. **confirmed.**

The port's own hook does read the same low bit to *label* the stage
(`app/src/cod2_nativo_ganchos.cpp:1082`, `shaders/cod2_contenedor.h:64-66`), and for a container that
actually reaches `sub_8209BD78` that label is right. It is not the reason pixel shaders are missed.

### 2.4 Every call site, and what `r3` holds

`sub_8209BD78` — three call sites, **confirmed**:

| Site | Enclosing function | `r3` | Result goes to |
|---|---|---|---|
| `cod2_recomp.5.cpp:20-22` | `sub_82090110` (`:3-56`) | `r4`, the container | `*(r5)`, and returns `0` or `0x8007000E` (`:25-26`, `:41-45`) |
| `cod2_recomp.7.cpp:27113-27117` | `sub_82384180` (`:27056`) | `*(r30+4)`, `r30 = *(0x82975264)` | `*(r29+0)` (`:27120-27121`) |
| `cod2_recomp.10.cpp:35415-35419` | `sub_8247E8B0` (`:35174`) | the return of the indirect call at `:35412-35414` | `*(r31+0)` (`:35418-35419`) |

`sub_8209B9D0` — four call sites, **confirmed**:

| Site | Enclosing function | `r3` | Result goes to |
|---|---|---|---|
| `cod2_recomp.7.cpp:20-22` | `sub_82090198` (`:3-56`) | `r4`, the container | `*(r5)`, and returns `0` or `0x8007000E` (`:25-26`, `:41-45`) |
| `cod2_recomp.7.cpp:27157-27161` | `sub_82384180` (`:27056`) | `*(r30+4)` | `*(r29+0)` (`:27164-27165`) |
| `cod2_recomp.10.cpp:35254-35258` | `sub_8247E8B0` (`:35174`) | the return of the indirect call at `:35251-35253` | `*(r31-8)` (`:35257-35258`) |
| `cod2_recomp.10.cpp:35339-35343` | `sub_8247E8B0` | the return of the indirect call at `:35336-35338` | `*(r31-4)` (`:35342-35343`) |

`sub_82090110` / `sub_82090198` are the device's `CreateVertexShader` / `CreatePixelShader`: `r3` is the COM
`this`, `r4` the container and `r5` the output slot, exactly as the two strings say, and they return an HRESULT.
**confirmed.**

**None of the seven sites is hooked.** The port hooks `sub_8209BD78` alone
(`app/src/cod2_nativo_ganchos.cpp:1078-1086`), so `RecordarCreacion` is only ever called with
`vertices == true`, `g_creados_ps` stays 0, and `g_objetos` never receives a pixel-shader object. That is the
whole of `C5b: shaders creados VS=61 (conocidos 57) PS=0 (conocidos 0)`. **confirmed.**

### 2.5 The four device methods, in one cluster

Address order in the game's own code, all four with **zero direct call sites** (so all four are reached only
through the D3D9 device vtable the game builds at runtime) — **confirmed**:

```
cod2_recomp.5.cpp:3    sub_82090110  CreateVertexShader  -> sub_8209BD78
cod2_recomp.6.cpp:3    sub_82090170  SetVertexShader     -> sub_8209BFE8
cod2_recomp.7.cpp:3    sub_82090198  CreatePixelShader   -> sub_8209B9D0
cod2_recomp.8.cpp:3    sub_820901F8  SetPixelShader      -> sub_8209BB00
```

The two setter thunks are three instructions: call, `li r3,0`, return (`cod2_recomp.8.cpp:14-18`,
`cod2_recomp.6.cpp:14-18`). Their argument registers pass straight through, so the device pointer is their
caller's `this`. **confirmed.**

---

## 3. Why `device + 0x3290` reads 0 at draw time

### 3.1 Who writes the slot — and the premise that is wrong

`device + 0x3290` is `device + 12944`. Every occurrence of `12944(r…)` in the whole translated program is:

```
cod2_recomp.0.cpp:451   lwz r3,12944(r30)     ; read the old object
cod2_recomp.0.cpp:463   stw r31,12944(r30)    ; THE ONLY WRITE
cod2_recomp.28.cpp:20892 lwz r11,12944(r3)    ; read + AddRef into a copy
cod2_recomp.37.cpp:327  lwz r11,12944(r3)     ; getter sub_82099EF8: read + AddRef + store to *(r4)
cod2_recomp.38.cpp:381  lwz r11,12944(r3)     ; getter, same shape
cod2_recomp.5.cpp:1308  lwz r3,12944(r28)     ; release the object (teardown)
cod2_recomp.7.cpp:1362  lwz r25,12944(r31)    ; sub_820B9AC8, §2.2
```

**confirmed.** One writer: `sub_8209BB00` (`cod2_recomp.0.cpp:424`), which stores its `r4` at `:463-464` after
releasing the previous object at `:451-459`.

The port's working premise — that the pixel slot is written by `sub_82098A88` and that `sub_82098A88`'s callers
pass 0 — is **half wrong**, and the wrong half is the half that matters:

- `sub_82098A88` does not write the slot; it calls `sub_8209BB00` (`cod2_recomp.1.cpp:361-363`) with the device
  `*(0x83630E60)` (`:359-360`) and the shader in `r4`.
- `sub_8209BB00` has **five** call sites, and **three of them pass a real shader object** (§3.3).

### 3.2 Every caller of `sub_82098A88`, and what it passes

`sub_82098A88` (`cod2_recomp.1.cpp:298-378`) is the pixel-shader *setter with a cache*: `r3` is the **shader
object** (`r29 = r3` at `:312-313`), the cache is the global `0x83D31568` (`:316-321`, written at `:370-371`), and
the body loops over `sub_8209BB00(*(0x83630E60), shader)` while the ring is busy (`:361-369`).

Three callers — **confirmed**:

| Caller | Site | `r3` passed | What it is |
|---|---|---|---|
| `sub_82364F60` (`cod2_recomp.22.cpp:25838-25845`) | `:25840-25843` | `li r3,0` → **0** | a 4-instruction unbind thunk: `li r3,0; b sub_82098A88`. Zero direct call sites → vtable/dispatch only |
| `sub_82368C40` (`cod2_recomp.15.cpp:25524`) | `:25879-25885` | `*(*(entry+12)+12)` → a **real PS object** | the scene bind, immediately before `sub_82098B10` at `:25886-25892` |
| `sub_82401610` (`cod2_recomp.42.cpp:31204`) | `:31266-31270` | `li r3,0` → **0** | a full state reset: `sub_82098A88(0)`, then `sub_82098B10(0)` (`:31271-31275`), then `sub_82098E10(0)` (`:31276-31280`) |

So the two NULL callers are unbind/reset paths, not the normal bind. Its symmetric partner `sub_82365490`
(`cod2_recomp.12.cpp:26288-26295`) is the vertex unbind, `li r3,0; b sub_82098B10`, and it too has zero direct
call sites. **confirmed.**

### 3.3 The other callers of `sub_8209BB00` — the live pixel path

| Site | Enclosing function | `r3` (device) | `r4` (shader) |
|---|---|---|---|
| `cod2_recomp.1.cpp:361-363` | `sub_82098A88` | `*(0x83630E60)` (`:359-360`) | the caller's `r3` |
| `cod2_recomp.8.cpp:14-16` | `sub_820901F8` | its own `r3` (the COM `this`) | its own `r4` — **the device's `SetPixelShader`** |
| `cod2_recomp.47.cpp:20364-20370` | `sub_822D1648` (`:20234`) | `*(0x83630E60)` (`:20256-20257`, `:20366-20367`) | `*(r29+32)` (`:20364-20365`), a saved object |
| `cod2_recomp.63.cpp:36622-36628` | `sub_8247E278` (`:36298`) | `r31` (`:36622-36623`) | `*(r29+4)` (`:36624-36625`) |
| `cod2_recomp.63.cpp:36982-36986` | `sub_8247E278` | the device in `r3` | `*(r29+0)` (`:36982-36983`) |

**confirmed.** The same function `sub_8247E278` also writes the vertex slot and calls the vertex installer in the
same block (`:36584-36602`), i.e. it is a "restore both stages" path. **confirmed.**

So the slot is written on the live path with real objects. A read of 0 therefore does **not** mean "nobody ever
writes it".

### 3.4 Ranked causes, and the most likely one

**(a) probable — the pixel shader really is unbound for those draws.** `0x3290` is live D3D state, and
`sub_8209BB00(device, 0)` is the only way it becomes 0. That call exists on three paths: `sub_82364F60`
(`cod2_recomp.22.cpp:25840-25843`), `sub_82401610` (`cod2_recomp.42.cpp:31266-31270`), and the device-vtable
`SetPixelShader` with `r4 = 0` (`cod2_recomp.8.cpp:14-16`). The port already documents this happening for some
draws — "D3D leaves the PS object at 0 in some of the race shadow draws"
(`app/src/cod2_nativo_sistema.cpp:2160-2161`). Note that the ring's `ps_actual_` is a **latch** set on every
pixel `IM_LOAD` and never cleared, so the run's `sin PS=0` ("no draw lacked a PS") is not evidence against this:
the latch and the live slot answer different questions.

**Where the observed 0 was printed matters.** The line that shows it is the temporary diagnostic in
`AnotarEscrituraDeAnillo`, and it only fires for the **first four** ring-writer calls of the session
(`app/src/cod2_nativo_ganchos.cpp:1147-1155`). A 0 there is trivially explained by "no pixel shader has been
bound yet" — the device is created and cleared before any material is drawn. So the 0 in that line may not be
representative of the draws that matter at all, and the robust evidence for the pixel gap is the **creation**
counter, `C5b … PS=0 (conocidos 0)`, which is the missing `sub_8209B9D0` hook and nothing else. **probable.**

**(b) guess — two device objects.** `sub_820901F8` writes `0x3290` on *its own* `r3` (the COM `this`), while
`sub_82098A88` writes it on `*(0x83630E60)`. If those are not the same object, the cutscene/menu path writes a
different object from the one the port reads. This is not demonstrated and the evidence leans against it: the
port's device global is exactly the one both setters compute (`cod2_recomp.1.cpp:322-327` and
`cod2_recomp.34.cpp:308-313` both resolve `lis -31901` + `addi 3672` + `lwz 8` = `*(0x83630E60)`), and the game's
own getters read `12944` off whatever object they are handed. **confirmed** that the global is the same; the
two-object hypothesis is a **guess** and needs a runtime pointer comparison to settle.

**(c) confirmed, and independent of (a) and (b) — a non-zero read would be useless anyway.** The port maps a
shader *object* to a library entry only through `RecordarCreacion`
(`app/src/cod2_nativo_ganchos.cpp:251-273`), which is called only from the `sub_8209BD78` hook (`:1085`). Since
`sub_8209B9D0` is not hooked, `g_objetos` holds vertex objects only, and `ShaderDeObjetoCacheado(registro.ps)`
returns null for every pixel object. `C5b … PS=0 (conocidos 0)` is the direct measurement of this.

**The single most likely reason `device + 0x3290` reads 0 at draw time: the last write to that slot was an
unbind** — `sub_8209BB00(device, 0)`, reached from `sub_82364F60` / `sub_82401610` / the `SetPixelShader` thunk
with `r4 = 0`; and in the line that was actually observed the reading is taken from the session's first four
ring-writer calls only, where a 0 needs no explanation beyond "nothing has been bound yet". The port's belief
that the slot is only ever written by `sub_82098A88`'s NULL callers is wrong in any case: `sub_8209BB00` has
five call sites and three of them pass real objects, so a 0 is a *state*, not a missing writer. **probable**,
not confirmed: the log line that shows the 0 is not in this repository and this document could not re-measure
it.

---

## 4. The right way for the renderer to know the bound pixel shader

### 4.1 The ring's `IM_LOAD` packets — the source to use

The Xenos ring carries the microcode the GPU will run, and the stage is the low two bits of the packet's first
payload word (`kVertex = 0`, `kPixel = 1`). The port already parses both packet forms and already keys the
identity on it:

- `app/src/cod2_nativo_sistema.cpp:2169-2185` — `PM4_IM_LOAD` → `CargarShaderCacheado(direccion_tipo, …)`;
- `app/src/cod2_nativo_sistema.cpp:2186-2194` — `PM4_IM_LOAD_IMMEDIATE` → `CargarInmediato(datos, tipo, …)`;
- the state is assigned at `:2748` and `:3007`, both as `(tipo == 0 ? vs_actual_ : ps_actual_) = …`.

Why this is the right source, and better than either device-side candidate:

1. It is the microcode itself, which is what the library is keyed on; the device slot holds an *object*, which
   needs a separate object→entry map that only the creation hooks can build. **confirmed.**
2. It needs no device offsets, so it is immune to (a) and (b) in §3.4. **confirmed.**
3. It already works for the pixel stage: the run reports `pares VS/PS=4` and `sin PS=0`, i.e. the port names a
   pixel shader for every draw and distinguishes four of them. **probable** — this is the run's measurement, read
   from the report of the run and not re-measured here.

The one caveat, which is a caveat of the *implementation* and not of the source: `ps_actual_` is a latch. It says
"the last pixel microcode loaded", not "the pixel microcode of this draw", and a draw that changes nothing keeps
the previous value. That is the correct answer whenever the GPU state is unchanged, which is the normal case, and
it is why pairing it with `EmparejarDibujo` (`app/src/cod2_nativo_sistema.cpp:3096`) is the right shape.
**probable.**

### 4.2 The globals `0x83D31568` / `0x83D3156C` are not the bound shader

Both addresses resolve from `lis r11,-31789` → `0x83D30000`, `addi r11,r11,-2944` → `0x83D2F480`, `+8424` /
`+8428` → `0x83D31568` / `0x83D3156C`. **confirmed.**

They are the caches of the two public setters:

- `0x83D31568`: read at `cod2_recomp.1.cpp:316-321`, written at `:370-371` — the last shader object passed to
  `sub_82098A88`, used only to skip a redundant re-bind. The only write in the image. **confirmed.**
- `0x83D3156C`: read at `cod2_recomp.34.cpp:302-307`, written at `:381-382` — the same for `sub_82098B10`.
  **confirmed.**

They are the wrong source for three reasons: they hold *objects*, not microcode; they are written only by those
two functions and are **not** updated by the device-vtable path (`sub_820901F8`, `sub_82090170`, both with zero
direct call sites), so they go stale as soon as the game sets a shader through the vtable; and they are globals,
not per-device, so a second device would share them. **confirmed** for the first two; the second-device point is
**probable**.

### 4.3 The device slot is right but unusable today

`device + 0x3290` holds the right object — the game's own getters (`cod2_recomp.37.cpp:327`,
`cod2_recomp.38.cpp:381`, `cod2_recomp.28.cpp:20892`) and its own state builder (`cod2_recomp.7.cpp:1362`) read
it as the pixel shader. **confirmed.** But the port cannot turn it into a library entry until it also hooks
`sub_8209B9D0` (§2.4, §3.4c). Reading a slot whose contents the port cannot resolve is what the run shows:
`dibujos con VS y PS=0` is fully explained by the missing pixel hook alone, whatever `0x3290` reads.

So the recommendation is the cheap one:

- **Use the ring identity for the pixel shader.** It is already implemented, already measured to produce pixel
  pairs, and needs no new hook. **probable.**
- **Add a `sub_8209B9D0` hook only if something needs the pixel shader as an object** — for instance to record a
  creation-time container, or to distinguish two library entries that share microcode. The hook would be the
  exact mirror of the existing one: read `r3` before, `r3` after, and record with `vertices = false`
  (`app/src/cod2_nativo_ganchos.cpp:1078-1086`). **probable.**

---

## 5. What I verified and what I did not

### Verified programmatically

| Check | Method | Result |
|---|---|---|
| `sub_8209BD78` = vertex, `sub_8209B9D0` = pixel | byte-read the two source-line strings out of `app/out/invitado.bin` at file offsets `0x51438` / `0x51480`, then matched each to the branch of `sub_82384180` that loads its address | `CreateVertexShader` / `CreatePixelShader`, each in its own branch |
| `0x3290` has exactly one writer | every occurrence of `12944(r…)` in `app/generated/default/` | 7 hits: one `stw` (`cod2_recomp.0.cpp:463`) and 6 reads (one of them the getter `sub_82099EF8`) |
| `0x4FD8` writers | every occurrence of `20440(r…)` | 4 `stw`: `cod2_recomp.34.cpp:368`, `cod2_recomp.41.cpp:474`, `cod2_recomp.47.cpp:20312`, `cod2_recomp.63.cpp:36596` |
| `0x3294` (the vertex twin of the pixel slot) | every occurrence of `12948(r…)` | one `stw`: `cod2_recomp.58.cpp:461` |
| Call-site counts of both constructors | grep for `sub_8209B9D0(ctx` / `sub_8209BD78(ctx` | 4 and 3, listed in §2.4 |
| Enclosing function of every cited call site | walk back to the nearest `DEFINE_REX_FUNC` | §2.4, §3.2, §3.3 |
| The four device methods are vtable-only | grep for each of `sub_82090110`, `sub_82090170`, `sub_82090198`, `sub_820901F8` | one hit each: the `DEFINE_REX_FUNC` line |
| `0x83630E60` is the device global both setters use | `lis`/`addi`/`lwz` arithmetic on `cod2_recomp.1.cpp:322-327`, `cod2_recomp.34.cpp:308-313`, `cod2_recomp.32.cpp:25005-25010` | `0x83630E58` + 8 |
| The two setter caches | same arithmetic on `lis -31789` + `addi -2944` + `8424`/`8428` | `0x83D31568` / `0x83D3156C`; one `stw` each |
| `sub_8209B9D0` is the only `0xC0A00000` allocator | every occurrence of `-19072` in a `lis r4` position | 1: `cod2_recomp.22.cpp:483-484` |
| No literal container-signature comparison exists | grep for `102A0E00`, `102A0E01`, `268640768`, `268640769` in `app/generated/default/*.cpp`, `*.h` | 0 hits |
| `sub_820B9AC8` reads both slots and uses `+52` / `+592` | read `cod2_recomp.7.cpp:1343-1381` | §2.2 |
| `sub_8209BB00`'s five callers and their arguments | grep for `sub_8209BB00(ctx` and read each window | §3.3 |
| `sub_82098A88`'s three callers and their arguments | grep for `sub_82098A88(ctx` and read each window | §3.2 |

### Not determined, with the method that would settle it

| Item | Why it is open | How to settle it |
|---|---|---|
| **Why the observed 0** | the log line that shows `PS 00000000` is not in this repository; this document could not re-measure it, and static analysis says the slot must be non-zero whenever a pixel shader is bound | log the value in `sub_8209BB00` right after `:463-464` and at every `AnotarDibujo`, with the draw index; a 0 preceded by a bind of the same draw falsifies §3.4(a) |
| Whether `*(0x83630E60)` and the `SetPixelShader` thunk's `this` are the same object | `sub_820901F8` has no direct call site, so its caller's `r3` cannot be read statically | one log of both pointers on a PC run |
| Whether the ring's pixel microcode is byte-identical to the container | the library identification for pixel shaders was not measured per entry; the run reports 5 of 8 distinct microcodes unidentified, but not their stage | `cod2_nativo_shaders.cpp:335`'s per-load log, or the `C5a … sin identificar` line, split by stage |
| Whether `ps_actual_` is the *right* pixel shader for every draw | it is a latch and was not compared against the device slot on a run | the comparison already exists in the other direction (`CompararDibujoMarcador`, `app/src/cod2_nativo_sistema.cpp:3238`); extend it to the slot |
| The pixel-container hazard of §2.3 | no run has been made with a pixel container reaching `sub_8209BD78` | an assert on the container's low bit inside a `sub_8209BD78` hook |
| Nothing at all about the game running | the game was not started and no ring was observed | PC run with ReXGlue's emulated GPU, then the console |

**Not verified here, and not claimed:** that any of these paths is taken on a given run. Every statement above is
about what the code does; the run's own numbers were given to me and are cited as such, not re-measured.
