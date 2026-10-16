// cod2 - self-check of the frame rate verdict (app/src/cod2_nativo_diagnostico_fps.h).
//
// WHY IT EXISTS
//   The verdict is a chain of four thresholds whose whole content is which one fires first. That is the shape
//   of code that breaks without saying anything: invert a comparison, write the thresholds in the wrong order,
//   forget the "no GPU timestamps" case, and the verdict still prints a cause, in the same confident shape, with
//   the reader's next round of work pointed at the wrong thread. The branches cannot be exercised from the game
//   on demand - each one needs a different machine state - so they are exercised here, on the decision alone.
//
// WHAT IT CHECKS
//   Every cause is reachable, each threshold is tested from both sides, and the cases that must NOT produce a
//   verdict (no GPU timestamps) are checked to fall through to the cause they should.
//
// HOW TO RUN IT
//   clang++ -std=c++20 -O1 -I app/src tools/pc/probar_diagnostico_fps.cpp -o out/probar_diagnostico_fps
//   out/probar_diagnostico_fps
//
// It prints one line per check and exits non-zero if any of them fails.

#include <cstdio>

#include "cod2_nativo_diagnostico_fps.h"

namespace {

int fallos = 0;
int comprobaciones = 0;

void Comprobar(const char* que, cod2::nativo::CausaFps visto, cod2::nativo::CausaFps esperado) {
  ++comprobaciones;
  if (visto == esperado) {
    std::printf("ok   %-58s -> %s\n", que, cod2::nativo::NombreCausaFps(visto));
    return;
  }
  ++fallos;
  std::printf("FALLO %-57s -> %s (esperado %s)\n", que, cod2::nativo::NombreCausaFps(visto),
              cod2::nativo::NombreCausaFps(esperado));
}

using cod2::nativo::CausaFps;
using cod2::nativo::DecidirCausaFps;

// A frame of 16.7 ms with the GPU idle most of it: the shape every case is built on by changing one number.
constexpr double kFotogramaMs = 16.7;

}  // namespace

int main() {
  // The ring thread at 100 % of a core, which is what the PC shows when the port is what costs the frame.
  Comprobar("anillo a 100 %, GPU al 13 %", DecidirCausaFps(100.0, true, 13.0, 0.0, 14.6, kFotogramaMs),
            CausaFps::kAnillo);
  // The threshold itself, from both sides: 60 % fires, 59.9 does not.
  Comprobar("anillo a 60 % (el umbral)", DecidirCausaFps(60.0, true, 13.0, 0.0, 14.6, kFotogramaMs),
            CausaFps::kAnillo);
  Comprobar("anillo a 59,9 %: por debajo del umbral", DecidirCausaFps(59.9, true, 13.0, 0.0, 14.6, kFotogramaMs),
            CausaFps::kEspera);

  // The ring first, even when the GPU is also saturated: it is the thread that keeps the GPU busy, so its
  // saturation is the one that decides.
  Comprobar("anillo y GPU saturados: manda el anillo",
            DecidirCausaFps(90.0, true, 95.0, 0.0, 0.2, kFotogramaMs), CausaFps::kAnillo);

  // GPU bound: busy for all of the frame, and the ring has slack.
  Comprobar("anillo al 40 %, GPU al 95 %", DecidirCausaFps(40.0, true, 95.0, 0.0, 0.8, kFotogramaMs),
            CausaFps::kGpu);
  Comprobar("GPU al 80 % (el umbral)", DecidirCausaFps(40.0, true, 80.0, 0.0, 3.3, kFotogramaMs),
            CausaFps::kGpu);
  Comprobar("GPU al 79,9 %: no es la GPU", DecidirCausaFps(40.0, true, 79.9, 0.0, 0.1, kFotogramaMs),
            CausaFps::kNinguna);

  // The game's own threads: they are the wall and the GPU is idle.
  Comprobar("juego al 80 %, anillo al 30 %", DecidirCausaFps(30.0, true, 20.0, 80.0, 10.0, kFotogramaMs),
            CausaFps::kJuego);
  Comprobar("juego al 40 % (el umbral)", DecidirCausaFps(30.0, true, 20.0, 40.0, 10.0, kFotogramaMs),
            CausaFps::kJuego);
  Comprobar("juego al 39,9 %: no es el juego", DecidirCausaFps(10.0, true, 20.0, 39.9, 10.0, kFotogramaMs),
            CausaFps::kEspera);
  // The GPU check is skipped without timestamps, so a game-bound frame cannot be called GPU-bound.
  Comprobar("juego saturado y sin marcas de GPU",
            DecidirCausaFps(30.0, false, 0.0, 80.0, 0.0, kFotogramaMs), CausaFps::kJuego);

  // Nobody saturated and the GPU idle for half the frame or more: the frame is lost waiting.
  Comprobar("todos bajos y la GPU parada media trama",
            DecidirCausaFps(20.0, true, 10.0, 5.0, 8.35, kFotogramaMs), CausaFps::kEspera);
  Comprobar("hueco justo en la mitad del fotograma",
            DecidirCausaFps(20.0, true, 10.0, 5.0, kFotogramaMs * 0.5, kFotogramaMs), CausaFps::kEspera);
  // With the GPU busy and the ring idle there is no gap to blame: it is the last case, not the waiting one.
  Comprobar("todos bajos y sin hueco: el ultimo caso",
            DecidirCausaFps(20.0, true, 60.0, 5.0, 1.0, kFotogramaMs), CausaFps::kNinguna);
  // And without timestamps there is no gap to measure either, so it cannot claim the frame is lost waiting.
  Comprobar("sin marcas de GPU no se puede culpar al hueco",
            DecidirCausaFps(20.0, false, 0.0, 5.0, 0.0, kFotogramaMs), CausaFps::kNinguna);

  // A frame that is slower for the same cause: the thresholds are shares, not absolute times, so the verdict
  // does not change when the frame does.
  Comprobar("el mismo caso con un fotograma de 50 ms",
            DecidirCausaFps(100.0, true, 13.0, 0.0, 43.0, 50.0), CausaFps::kAnillo);

  std::printf("\n%d comprobaciones, %d fallos\n", comprobaciones, fallos);
  return fallos ? 1 : 0;
}
