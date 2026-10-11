// cod2 - the console profile the game does not ask for itself (cod2_rendimiento.cpp)

#pragma once

namespace cod2::rendimiento {

// Requests the handheld GPU profile through apm and logs what was asked. Call it with cod2.toml already
// read and before SetupPresentation (OnPostInitLogging). Does nothing if cod2_switch_gpu_mhz is 0.
void AplicarRendimiento();

}  // namespace cod2::rendimiento
