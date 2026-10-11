// cod2 - the console profile the game does not ask for itself.
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
// WHAT IT DOES
//   Nintendo released a 460.8 MHz GPU profile for handheld mode precisely for games that do not fit in
//   307.2, and Horizon exposes it through apm, the same service any commercial game uses. It is not an
//   overclock: no KIP and no sysmodule is touched, and if the firmware does not have the configuration
//   apm returns an error and changes nothing. 307.2 -> 460.8 is +50 % of GPU.
//
//   The memory clock is left alone unless it is the only way: the high-GPU configurations come in two
//   flavours, with EMC 1331.2 (0x92220008) and with EMC 1600 (0x92220007), and RexSwitchApmAplicar tries
//   them in that order and keeps the first that works. cod2_switch_ram_1600, on by default, is what decides
//   what happens when the firmware has only the second: on, the GPU clock is taken and the RAM goes to 1600
//   with it; off, nothing is changed at all and the port stays at 307.2 MHz. It does not force 1600 by
//   itself, so it costs no battery on a console that has both.
//
// WHY IT WAS NOT THERE
//   It lived in src/aparcado/cod2_ajustes_graficos.cpp, parked along with the inherited native renderer.
//   Nothing in this tree called it, so the request never happened. The apm half does not depend on the
//   renderer at all, which is why it comes back on its own.
//
// HOW TO TURN IT OFF WITHOUT A REBUILD
//   cod2_switch_gpu_mhz = 0 in cod2.toml leaves the console exactly as it was. How to check that it took
//   effect: the next profiler report says "relojes: ... GPU 460.8 MHz ...". apm hands the change to pcv,
//   so it lands a moment after the request, not on the next line.

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

namespace cod2::rendimiento {

void AplicarRendimiento() {
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
