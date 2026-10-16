// cod2 - which of the four walls a slow frame is on (the frame rate diagnosis).
//
// cod2_nativo_sistema.cpp asks for this from the pad combo L+R+ZR (F8 on the PC) and prints the answer as
// "VEREDICTO". The numbers it is based on are gathered there; the decision itself lives here, on its own, for
// two reasons: the thresholds are the whole content of the verdict and they need to be readable in one place,
// and a branch chain of this shape is exactly what goes wrong silently (a condition inverted, a threshold that
// can never be reached). tools/pc/probar_diagnostico_fps.cpp runs the cases against it.
//
// The order of the checks is the order that decides it: a ring thread at 100 % hides everything else, because
// it is the thread that keeps the GPU busy, so it is asked first. See the cvar's comment for what each wall
// means and for why the fourth case (nobody saturated) is a separate answer rather than a default.

#pragma once

namespace cod2::nativo {

enum class CausaFps {
  // The thread that records the Vulkan commands. Its own code, so the only cause a renderer change fixes.
  kAnillo,
  // The GPU busy for most of the frame: the frame is what it shades.
  kGpu,
  // The game's own threads (culling, scenery, simulation): no renderer change helps.
  kJuego,
  // Nobody saturated and the GPU idle for half the frame or more: the frame is lost waiting, not working
  // (swapchain interval, rejects, a driver round trip).
  kEspera,
  // None of the above: the suspects are the Swap rejects and the presentation path.
  kNinguna,
};

// `anillo_pct`, `juego_pct`: share of one core / of the wall clock over the interval, in percent.
// `gpu_pct`: GPU busy time as a share of the frame time, in percent; `hay_gpu` is false when there are no GPU
// timestamps, and then the GPU cannot be blamed (the two GPU branches are skipped).
// `hueco_ms`, `ms_fotograma`: GPU idle between submissions and frame time, both in milliseconds.
inline CausaFps DecidirCausaFps(double anillo_pct, bool hay_gpu, double gpu_pct, double juego_pct,
                                double hueco_ms, double ms_fotograma) {
  // The thresholds, named: they are the verdict.
  constexpr double kAnilloSaturado = 60.0;
  constexpr double kGpuSaturada = 80.0;
  constexpr double kJuegoSaturado = 40.0;
  constexpr double kHuecoPorFotograma = 0.5;

  if (anillo_pct >= kAnilloSaturado) {
    return CausaFps::kAnillo;
  }
  if (hay_gpu && gpu_pct >= kGpuSaturada) {
    return CausaFps::kGpu;
  }
  if (juego_pct >= kJuegoSaturado) {
    return CausaFps::kJuego;
  }
  if (hay_gpu && hueco_ms >= ms_fotograma * kHuecoPorFotograma) {
    return CausaFps::kEspera;
  }
  return CausaFps::kNinguna;
}

inline const char* NombreCausaFps(CausaFps causa) {
  switch (causa) {
    case CausaFps::kAnillo:
      return "EL HILO DEL ANILLO (el que graba los comandos)";
    case CausaFps::kGpu:
      return "LA GPU";
    case CausaFps::kJuego:
      return "LOS HILOS DEL PROPIO JUEGO (culling, escenario, logica)";
    case CausaFps::kEspera:
      return "NADIE SATURADO: el fotograma se pierde esperando, no trabajando";
    case CausaFps::kNinguna:
      break;
  }
  return "NINGUNA DE LAS TRES: mirar los rechazos de Swap y el intervalo de la swapchain";
}

}  // namespace cod2::nativo
