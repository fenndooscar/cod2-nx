// cod2 - the settings that decide the frame rate and that the game does not ask for itself.
//
// THE FAILURE
//   Every profiler report of an in-game session carries the same line:
//       relojes: CPU 1020.0 MHz, GPU 307.2 MHz, memoria 2131.2 MHz
//   and the whole session at 1280x720 was between 3.2 and 10.8 FPS. 307.2 MHz is what Horizon hands a
//   game that asks for nothing, and the GPU is the wall: "libnx por segundo" reports 834 ms of every
//   second spent waiting for fences (83 % of the wall time), the game's own main thread answers
//   "0 % esperando en el kernel" while it spins on the GPU, and the heavy report of
//   switch-logs/logs-old-1 spends 255 ms of a 306 ms frame inside the GPU.
//
//   Raising the clock was not enough on its own. With the profile applied (switch-logs/logs, 2026-10-11)
//   the clock line reads 460.8 MHz, the 760 ms/s of fence wait stay, and the game sits at 5.0-5.8 FPS at
//   1280x720 with 281-403 draws and 6.1 screens of resolve per frame. The clock was worth +50 % of GPU and
//   the port got about +65 %; the frame is still several times too long, and what is left is GPU work that
//   the game never asked for.
//
// WHAT IT DOES
//   Two halves, both of them settings nobody on the guest side requested.
//
//   1. The console's GPU clock. Nintendo released a 460.8 MHz profile for handheld mode precisely for games
//      that do not fit in 307.2, and Horizon exposes it through apm, the same service any commercial game
//      uses. It is not an overclock: no KIP and no sysmodule is touched, and if the firmware does not have
//      the configuration apm returns an error and changes nothing. 307.2 -> 460.8 is +50 % of GPU.
//
//      The memory clock is left alone unless it is the only way: the high-GPU configurations come in two
//      flavours, with EMC 1331.2 (0x92220008) and with EMC 1600 (0x92220007), and RexSwitchApmAplicar tries
//      them in that order and keeps the first that works. cod2_switch_ram_1600, on by default, is what
//      decides what happens when the firmware has only the second: on, the GPU clock is taken and the RAM
//      goes to 1600 with it; off, nothing is changed at all and the port stays at 307.2 MHz. It does not
//      force 1600 by itself, so it costs no battery on a console that has both.
//
//      The CPU clock is deliberately not touched, and it is not an oversight. The report says
//      "CPU 1020.0 MHz", which is the stock handheld clock; 1785 MHz exists, but on this console it is only
//      reachable through apm's CPU boost mode, and that mode is *fast load*: it raises the CPU and
//      simultaneously drops the GPU to its minimum (ApmCpuBoostMode_FastLoad, libnx's apm.h). There is no
//      profile with both, by Nintendo's design, because the two do not fit in the power budget together.
//      Forcing the CPU up through clkrst instead does not work either, and this file already has the
//      evidence: the same attempt on the memory clock was accepted eight times out of eight and reverted by
//      pcv in under a second every time, because pcv reimposes the clocks of the active apm configuration.
//      So the trade is "GPU 460.8 with CPU 1020" or "CPU 1785 with GPU 76.8", and a frame that is GPU-bound
//      wants the first.
//
//   2. Two renderer defaults the SDK carries and this game never asked for, both of which multiply per-pixel
//      cost. Neither is a guess about the guest: each one puts the renderer back on the behaviour the guest's
//      own fetch constants and render target formats describe.
//
//      anisotropic_override = 3 is the SDK's own default and it is not the guest's: GetSamplerParameters
//      (sdk/src/graphics/vulkan/texture_cache.cpp) starts from the fetch constant's aniso_filter and then
//      overwrites it with 4x for every texture that has mips and linear filtering, including the ones the
//      game asked to be sampled with no anisotropic filtering at all. On a 460.8 MHz GM20B the texture units
//      are the scarce resource - 16 of them - and 4x anisotropic filtering is up to four texel fetches per
//      sample. -1 means "no override": the guest's own value, whatever it is. Strictly less work, and nothing
//      the game did not already ask to see.
//
//      gamma_render_target_as_unorm16 = true makes a k_8_8_8_8_GAMMA render target R16G16B16A16_UNORM, eight
//      bytes per pixel instead of four. The Xbox 360's EDRAM is eight bits per channel for that format, so
//      false - R8G8B8A8_UNORM, which is also what the D3D12 backend uses - is the faithful one as well as the
//      cheap one, and it doubles the rate the colour attachment can be written at. The cost is blending
//      precision in the gamma target, which is where banding could show up if anywhere.
//
//   Both are set through PonerSiNadieLoPidio, so a value given in cod2.toml or on the command line wins and
//   neither change needs a rebuild to undo.
//
//   3. direct_host_resolve = false. The SDK's direct-resolve path (render_target_cache.cpp,
//      TryResolveCopyDirectly) is a preflight that always falls through to the same DumpRenderTargets the
//      non-direct path calls, so with it on every resolve derives its rectangles and looks up its pipelines
//      twice and the GPU work is identical. The round-22 report has 33.7 resolves per frame, all of them on the
//      busiest thread. Off, the dump happens once.
//
// WHY IT WAS NOT THERE
//   The clock half lived in src/aparcado/cod2_ajustes_graficos.cpp, parked along with the inherited native
//   renderer. Nothing in this tree called it, so the request never happened. The apm half does not depend on
//   the renderer at all, which is why it comes back on its own. The two renderer defaults were never touched
//   because the port inherited them from the other game, where the frame budget was different.
//
// HOW TO TURN IT OFF WITHOUT A REBUILD
//   cod2_switch_gpu_mhz = 0 in cod2.toml leaves the console's clocks exactly as they were;
//   anisotropic_override = 3 puts the forced 4x back; gamma_render_target_as_unorm16 = true puts the
//   sixteen-bit gamma target back; direct_host_resolve = true puts the duplicate resolve preflight back. How to
//   check that the clock took effect: the next profiler report says "relojes: ... GPU 460.8 MHz ...". apm hands
//   the change to pcv, so it lands a moment after the request, not on the next line. The three renderer settings
//   only take effect at startup, because the render target cache and the samplers are built once.

#include "cod2_rendimiento.h"

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/platform.h>

#if REX_PLATFORM_SWITCH
#include <rex/ui/switch_apm.h>
#endif

REXCVAR_DEFINE_INT32(cod2_switch_gpu_mhz, 460, "Graficos",
                     "Switch: MHz de GPU que se piden al sistema en portatil (0 = no tocar nada, 384, 460). "
                     "460,8 MHz es un perfil oficial de Horizon, no un overclock; gasta mas bateria y calienta mas")
    .allowed({"0", "384", "460"});

REXCVAR_DEFINE_BOOL(cod2_switch_ram_1600, true, "Graficos",
                    "Switch: aceptar un perfil de GPU alta que suba la memoria a 1600 MHz cuando no hay "
                    "ninguno que deje la RAM donde estaba. NO es lo mismo que pedir 1600: el perfil que "
                    "respeta la memoria se intenta primero. En false, una consola sin ese perfil se queda en "
                    "307,2 MHz. El log [apm] dice cual entro");

// The GPU timestamp counter on this chip does not run at the GPU's clock. NVK reports timestampPeriod = 1 ns
// (limits.timestampPeriod), and it is wrong: the counter is the SoC's fixed 614.4 MHz reference, so one unit is
// 1/614.4 MHz = 1.627 ns whatever the GPU is clocked at. Measured by comparing what the timestamps covered with
// the wall clock (docs/measuring.md, "GPU timestamps on NVK under Horizon"). Without this the report would
// understate every GPU time by 39 %. The knob is here, and not a constant in the renderer, because it is a
// property of the driver that a future NVK may fix: the report prints the resulting share of the wall clock, so
// a scale that is too large shows up as the GPU being busy more than 100 % of the time.
REXCVAR_DEFINE_DOUBLE(cod2_marca_escala, 1.627, "Graficos",
                      "Switch: nanosegundos por unidad del contador de marcas de tiempo de la GPU (1,627 = "
                      "reloj fijo de 614,4 MHz; NVK informa de 1,0 y esta mal). Ver docs/measuring.md");

namespace cod2::rendimiento {

void PonerSiNadieLoPidio(const char* nombre, const char* valor) {
  if (rex::cvar::GetFlagInfo(nombre) == nullptr) {
    REXLOG_DEBUG("Ajuste '{}' no registrado todavia; no lo toco.", nombre);
    return;
  }
  if (rex::cvar::HasNonDefaultValue(nombre)) {
    return;  // the player asked for a value: do not override it.
  }
  if (rex::cvar::SetFlagByName(nombre, valor)) {
    REXLOG_DEBUG("Ajuste por defecto de la build portable: {} = {}", nombre, valor);
  }
}

void AplicarRendimiento() {
  // The guest's own anisotropic filtering, not the SDK's forced 4x.
  PonerSiNadieLoPidio("anisotropic_override", "-1");
  // Eight bits per channel for the gamma render target, which is what the EDRAM holds and what the D3D12
  // backend uses: half the bytes and twice the colour write rate of R16G16B16A16_UNORM.
  PonerSiNadieLoPidio("gamma_render_target_as_unorm16", "false");
  // The SDK's "direct host resolve" is on by default and never resolves anything directly: TryResolveCopyDirectly
  // runs its whole preflight and then ends at render_target_cache.cpp's `return DumpRenderTargets(...)`, which is
  // exactly what the caller would have done with the flag off - and DumpRenderTargets derives the same rectangles
  // and looks up the same pipelines a second time. In game that is 33.7 resolves per frame (round-22 report) each
  // paying for the work twice on the thread that is already the frame's critical path. Off, the dump happens once.
  PonerSiNadieLoPidio("direct_host_resolve", "false");
  REXLOG_INFO("[rendimiento] aniso del invitado, gamma de 8 bits, y relojes:");
#if REX_PLATFORM_SWITCH
  const int mhz = REXCVAR_GET(cod2_switch_gpu_mhz);
  RexSwitchApmPedirGpuMhz(mhz);
  RexSwitchApmPermitirRam1600(REXCVAR_GET(cod2_switch_ram_1600) ? 1 : 0);
  RexSwitchApmAplicar();
  REXLOG_INFO("[rendimiento] perfil de consola pedido: GPU {} MHz{}", mhz,
              mhz == 0 ? " (no se toca nada)" : "; el reloj real lo dice el informe del perfilador");
#endif
}

}  // namespace cod2::rendimiento
