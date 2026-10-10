// cod2 - the game's own console, in the log.
//
// The failure this exists for
//     "The save goes back to the first mission after the second one" is a claim about what the game
//     *decided*, and the port has no window on that. The runtime's log says which files were opened
//     and what the SDK answered; it cannot say why the game then chose not to write a save. The game
//     says it itself, on its console: "no save device", "couldn't write", "unknown dvar", script
//     errors, and the campaign state it thinks it is in. On the console that text goes to a sink the
//     port never installed, so it is dropped on the floor.
//
// The evidence
//     The sink is sub_824316B0. Two of its call sites are visible in the recompiled code and both
//     hand it a buffer that was just built by a printf-style helper and a severity:
//       sub_8233B218: sub_8229A580(r1+96, 4096, fmt, args) then sub_824316B0(0, r1+96, len, 0, 7, 0)
//       sub_82444C50: sub_8229DD00(r1+96, ...)             then sub_824316B0(1, r1+96, ...)
//     r4 is the finished, NUL-terminated message in both.
//
// What this does
//     Replaces the sink: prints r4 to the log with a [consola] tag and then calls the original, so the
//     game's behaviour is unchanged. Off by default - a level load prints thousands of lines - and
//     capped, because a runaway script can print every frame.
//
// How to turn it off without a rebuild
//     --no-cod2_consola (default). --cod2_consola=true to get the text, --cod2_consola_lineas=N to
//     change the cap.

#include <atomic>
#include <cstdint>
#include <cstring>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/xmemory.h>

REXCVAR_DEFINE_BOOL(cod2_consola, false, "COD2",
                    "Escribe en el log las lineas de la consola del propio juego (sub_824316B0). "
                    "Apagado por defecto: una carga de nivel imprime miles.");
REXCVAR_DEFINE_INT32(cod2_consola_lineas, 20000, "COD2",
                     "COD2: maximo de lineas de consola que se escriben en el log")
    .range(1, 2000000);

// The generated code declares these as weak aliases of the __imp__ entry points; a hook is a strong
// definition of the bare name that calls the original. The declarations live in cod2_funcs.h, which
// this target does not include, so they are repeated here.
extern "C" void __imp__sub_824316B0(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_822CA8A8(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_82292AD0(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_822CAAE8(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_822CB260(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_823B5048(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_823B6F10(PPCContext& __restrict ctx, uint8_t* base);

namespace {

std::atomic<uint32_t> g_lineas{0};
std::atomic<bool> g_avisado{false};

// A guest string, only when it really looks like one: a message that is not printable text is a
// misidentified call, and printing garbage into the log is worse than printing nothing.
bool TextoInvitado(uint8_t* base, uint32_t direccion, char* fuera, size_t tamano) {
  if (direccion < 0x1000 || tamano < 2) {
    return false;
  }
  auto* origen = rex::memory::GuestPtr<const char*>(base, direccion);
  size_t n = 0;
  for (; n + 1 < tamano; ++n) {
    const char c = origen[n];
    if (c == '\0') {
      break;
    }
    if (static_cast<unsigned char>(c) < 0x20 && c != '\n' && c != '\t') {
      return false;
    }
    fuera[n] = c;
  }
  fuera[n] = '\0';
  return n > 0;
}

}  // namespace

REX_HOOK_RAW(sub_824316B0) {
  if (REXCVAR_GET(cod2_consola)) {
    const uint32_t limite = static_cast<uint32_t>(REXCVAR_GET(cod2_consola_lineas));
    if (g_lineas.fetch_add(1, std::memory_order_relaxed) < limite) {
      char texto[1024];
      if (TextoInvitado(base, ctx.r4.u32, texto, sizeof(texto))) {
        REXKRNL_INFO("[consola] {}", texto);
      }
    } else if (!g_avisado.exchange(true, std::memory_order_relaxed)) {
      REXKRNL_INFO("[consola] se alcanzo el tope de {} linea(s); sube cod2_consola_lineas", limite);
    }
  }
  __imp__sub_824316B0(ctx, base);
}

// sub_822CA8A8 is the helper that ends in XamUserWriteProfileSettings (sub_82292AD0), so this line
// marks the exact moment the game decides to persist the profile. Its absence is as informative as
// its presence: it says the game never got as far as wanting to save.
REX_HOOK_RAW(sub_822CA8A8) {
  if (REXCVAR_GET(cod2_consola)) {
    REXKRNL_INFO("[perfil] el juego guarda el perfil (sub_822CA8A8)");
  }
  __imp__sub_822CA8A8(ctx, base);
}

// The three links of that chain, one line each, so a run says where it stops instead of only that it
// stopped. sub_82292AD0 is the funnel: every profile write in the game goes through it, and its r3 is
// the user index - the one argument the runtime refuses when it is not zero. sub_822CAAE8 and
// sub_822CB260 are its only callers.
REX_HOOK_RAW(sub_82292AD0) {
  if (REXCVAR_GET(cod2_consola)) {
    REXKRNL_INFO("[perfil] sub_82292AD0 indice {} ajustes {} datos 0x{:08X} solapado 0x{:08X}",
                 ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32);
  }
  __imp__sub_82292AD0(ctx, base);
}
REX_HOOK_RAW(sub_822CAAE8) {
  if (REXCVAR_GET(cod2_consola)) {
    REXKRNL_INFO("[perfil] sub_822CAAE8 (llamador de sub_822CA8A8)");
  }
  __imp__sub_822CAAE8(ctx, base);
}
REX_HOOK_RAW(sub_822CB260) {
  if (REXCVAR_GET(cod2_consola)) {
    REXKRNL_INFO("[perfil] sub_822CB260 indice {} modo {}", ctx.r3.u32, ctx.r4.u32);
  }
  __imp__sub_822CB260(ctx, base);
}

// setMissionCvar and setSavedCvar are the two script builtins that write campaign progress. They are
// the only writers of mis_01..mis_10 (setMissionCvar walks exactly that table, at 0x824D1858) and of
// the saved-cvar block that becomes the profile. Counting them separates "the game never computed the
// progress" from "it computed it and never wrote it down", which are different bugs with different
// fixes. The arguments are script values, so only the raw registers are logged.
REX_HOOK_RAW(sub_823B5048) {
  if (REXCVAR_GET(cod2_consola)) {
    REXKRNL_INFO("[perfil] setMissionCvar r3 {} r4 {} r5 {}", ctx.r3.u32, ctx.r4.u32, ctx.r5.u32);
  }
  __imp__sub_823B5048(ctx, base);
}
REX_HOOK_RAW(sub_823B6F10) {
  if (REXCVAR_GET(cod2_consola)) {
    REXKRNL_INFO("[perfil] setSavedCvar r3 {} r4 {} r5 {}", ctx.r3.u32, ctx.r4.u32, ctx.r5.u32);
  }
  __imp__sub_823B6F10(ctx, base);
}
