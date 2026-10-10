// cod2 - PC build: the host address of the game's recompiled functions, for reading a crash.
//
// Why it exists
//   The PC build dies inside translated code and all that is left is a host address:
//   `cod2.exe+0x144D415`. Nothing ties that number to the `sub_8229AF10` that the generated code, the
//   codegen log and `app/huecos.toml` talk about. The link decides where each of the 21,000 functions
//   lands and writes it nowhere, so without symbols the host address of a translated function cannot be
//   turned back into its guest address.
//
// What it does
//   At start-up it walks the guest code range, and for every address registered in the dispatch table it
//   writes one line to logs/simbolos.csv:
//
//       host;guest;siguiente_host
//
//   sorted by host address. tools/pc/direccion.py then resolves a host address by taking the last line
//   at or below it and reporting `sub_XXXXXXXX+0xNN`, along with the distance to the next function, so a
//   wrong match is visible instead of plausible.
//
// How to turn it off without a rebuild
//   --cod2_pc_simbolos=false
//
// The file only exists on a PC: on the console the crash log already speaks in guest addresses.

#if defined(_WIN32)

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/kernel_state.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <utility>
#include <vector>

namespace {

REXCVAR_DEFINE_BOOL(cod2_pc_simbolos, true, "COD2",
                    "PC: escribir logs/simbolos.csv con la direccion de host de cada funcion "
                    "recompilada, para poder resolver la direccion de un fallo")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// The module the game's own code lives in, from the entrypoint of app/cod2_manifest.toml.
constexpr uint32_t kCodigoInicio = 0x82090000;
constexpr uint32_t kCodigoFin = 0x824A8DEC;

}  // namespace

namespace cod2::pc_simbolos {

// Called once the kernel exists and the module is registered (see cod2_app.h).
void Escribir() {
  if (!REXCVAR_GET(cod2_pc_simbolos)) {
    return;
  }
  auto* kernel = rex::system::kernel_state();
  if (!kernel) {
    return;
  }
  auto* despachador = kernel->function_dispatcher();
  if (!despachador) {
    return;
  }

  std::vector<std::pair<uintptr_t, uint32_t>> funciones;
  for (uint32_t direccion = kCodigoInicio; direccion < kCodigoFin; direccion += 4) {
    if (::PPCFunc* fn = despachador->GetFunction(direccion)) {
      funciones.emplace_back(reinterpret_cast<uintptr_t>(fn), direccion);
    }
  }
  std::sort(funciones.begin(), funciones.end());

  const std::filesystem::path ruta =
      rex::filesystem::GetExecutableFolder() / "logs" / "simbolos.csv";
  std::error_code ec;
  std::filesystem::create_directories(ruta.parent_path(), ec);
  std::FILE* f = _wfopen(ruta.c_str(), L"wb");
  if (!f) {
    REXLOG_WARN("[pc] no se pudo escribir {}", ruta.string());
    return;
  }
  std::fprintf(f, "#host;guest;siguiente\n");
  for (size_t i = 0; i < funciones.size(); ++i) {
    const uintptr_t siguiente = i + 1 < funciones.size() ? funciones[i + 1].first : 0;
    std::fprintf(f, "%llX;%08X;%llX\n", static_cast<unsigned long long>(funciones[i].first),
                 funciones[i].second, static_cast<unsigned long long>(siguiente));
  }
  std::fclose(f);
  REXLOG_INFO("[pc] {} funciones en {} para resolver direcciones de host", funciones.size(),
              ruta.string());
}

}  // namespace cod2::pc_simbolos

#endif  // _WIN32