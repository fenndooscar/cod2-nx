// cod2 - environment variables for Mesa/NVK before creating Vulkan (cod2_entorno_mesa.cpp)

#pragma once

namespace cod2::entorno {

// Sets the variables from cod2_mesa_entorno and, on the Switch, logs whether Mesa's on-disk shader cache
// exists and how big it is. Must be called with the toml already read and before SetupPresentation
// (OnPostInitLogging).
void AplicarEntornoMesa();

}  // namespace cod2::entorno
