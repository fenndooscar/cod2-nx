// cod2 - the settings that decide the frame rate and that the game does not ask for itself
// (cod2_rendimiento.cpp)

#pragma once

namespace cod2::rendimiento {

// Requests the handheld GPU profile through apm, drops the GPU work the SDK adds on top of what the
// guest asks for, and logs both. Call it with cod2.toml already read and before SetupPresentation
// (OnPostInitLogging). Does nothing if cod2_switch_gpu_mhz is 0.
void AplicarRendimiento();

// Sets a cvar to a value only if the player did not ask for one of their own, either on the command
// line or in cod2.toml. That is what lets every default below be undone without a rebuild.
void PonerSiNadieLoPidio(const char* nombre, const char* valor);

}  // namespace cod2::rendimiento
