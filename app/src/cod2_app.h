// cod2 - the app itself: where the game data is, which settings are mandatory, and the hang watchdog.
//
// It starts the recompiled game with ReXGlue's own emulated GPU by default. With cod2_renderizador set to
// "nativo" it starts the port's own graphics system instead (src/cod2_nativo_sistema.cpp), and the
// emulation plugin is never loaded: OnPreSetup fills config.graphics, and ReXApp::SetupPresentation only
// looks at gpu_plugin when that is empty (sdk/src/ui/rex_app.cpp:342). See docs/native-renderer.md.
//
// The renderer it starts is complete as a Vulkan backend and complete enough as a Call of Duty 2 renderer to
// draw: it answers the game's GPU-side expectations (the MMIO registers, the ring read pointer, vblank, GPU
// writes to memory, occlusion queries), identifies each draw's shaders from the ring's own IM_LOAD packets,
// and takes an exact record of each draw from this game's own hooks - the shader constructor (sub_8209BD78)
// and the draw submission (sub_82368310). What is still missing is the register marker (an optimisation) and
// the shadow-map vegetation filter, and both are listed at the tail of src/cod2_nativo_ganchos.cpp.

#pragma once

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xthread.h>
#include <rex/ui/keybinds.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "cod2_ajustes_graficos.h"
#include "cod2_nativo_captura.h"
#include "cod2_nativo_sistema.h"
#include "cod2_perfil_pc.h"
#include "cod2_pc_simbolos.h"
#include "cod2_rendimiento.h"

class Cod2App : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<Cod2App>(new Cod2App(ctx, "cod2", PPCImageConfig));
  }

  // ==========================================================================
  //  0. THE NATIVE RENDERER
  //
  //  With cod2_renderizador = "nativo" the graphics system is the port's own
  //  (src/cod2_nativo_sistema.cpp) and the Xenos emulation plugin is not
  //  loaded, because ReXApp::SetupPresentation only reaches for gpu_plugin
  //  when config.graphics is empty (sdk/src/ui/rex_app.cpp:342). The code
  //  default is still the emulation: this switch is what makes the comparison
  //  possible, and it is the escape hatch if the native path misbehaves on the
  //  console.
  // ==========================================================================
  void OnPreSetup(rex::RuntimeConfig& config) override {
    if (cod2::nativo::Activo()) {
      config.graphics = cod2::nativo::CrearSistemaGrafico();
    }
  }

 protected:
  // ==========================================================================
  //  1. WHERE THE GAME DATA IS
  //
  //  Without this, starting without --game_data_root dies with
  //      "--game_data_root was not provided."
  //  because SetupEnvironment only looks at the cvar and, if it is empty,
  //  ConstructRuntime aborts.
  //
  //  OnConfigurePaths runs right after the PathConfig is built and before
  //  anyone uses it, which is the place to fill the gap. ORDER MATTERS: this
  //  runs *before* cod2.toml is read (SetupEnvironment reads it a few lines
  //  further down), so the priority is: --game_data_root on the command line,
  //  and otherwise whatever is found here. Setting game_data_root in the toml
  //  does not work, and that is not our doing: it is how the SDK is ordered.
  //
  //  It searches, in this order:
  //    1. an .iso whose name matches the executable's
  //    2. any other .iso in the folder, in alphabetical order
  //    3. a game_root\ folder, in case someone prefers to extract it
  //
  //  (1) exists so that a folder with cod2.exe and cod2.iso works
  //  unambiguously even if there are more images.
  // ==========================================================================
  void OnConfigurePaths(rex::PathConfig& paths) override {
    ElegirCarpetaDeGuardados();
    if (!paths.game_data_root.empty()) {
      return;  // given on the command line; it takes precedence.
    }

    std::error_code ec;
    const auto carpeta = rex::filesystem::GetExecutableFolder();
    if (carpeta.empty() || !std::filesystem::is_directory(carpeta, ec)) {
      return;
    }

    std::filesystem::path preferida;
    std::vector<std::filesystem::path> otras;

    std::string yo;
    {
      const auto exe = rex::filesystem::GetExecutablePath();
      if (!exe.empty()) {
        yo = exe.stem().string();
        std::transform(yo.begin(), yo.end(), yo.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });
      }
    }

    for (const auto& e : std::filesystem::directory_iterator(carpeta, ec)) {
      if (ec) break;
      if (!e.is_regular_file(ec)) continue;

      std::string ext = e.path().extension().string();
      std::transform(ext.begin(), ext.end(), ext.begin(),
                     [](unsigned char c) { return char(std::tolower(c)); });
      if (ext != ".iso") continue;

      std::string base = e.path().stem().string();
      std::transform(base.begin(), base.end(), base.begin(),
                     [](unsigned char c) { return char(std::tolower(c)); });

      if (!yo.empty() && base == yo) {
        preferida = e.path();
      } else {
        otras.push_back(e.path());
      }
    }

    if (!preferida.empty()) {
      paths.game_data_root = preferida;
    } else if (!otras.empty()) {
      std::sort(otras.begin(), otras.end());
      paths.game_data_root = otras.front();
    } else {
      // No ISO: an extracted folder next to it also works.
      const auto extraida = carpeta / "game_root";
      if (std::filesystem::is_directory(extraida, ec)) {
        paths.game_data_root = extraida;
      }
    }
    // If nothing is found it is left empty on purpose: the SDK gives its own
    // message, which is clearer than anything we could put here.
  }

  // ==========================================================================
  //  2. COPY OF THE SAVES, NEXT TO THE EXECUTABLE
  //
  //  The game's saves live where the runtime puts them, which is an opaque
  //  place: <data>/<xuid>/<title>/<type>/<name>. The player cannot see them and
  //  there is no convenient way to copy them or recover a corrupted one.
  //
  //  With content_backup_root the runtime also leaves a copy sorted by profile
  //  when each save is closed:
  //      saves/<profile>/actual      the latest save
  //      saves/<profile>/anterior    the previous one, in case the latest gets corrupted
  //
  //  On the Switch the NRO is in sdmc:/switch/cod2-nx/, so this gives
  //  sdmc:/switch/cod2-nx/saves. On PC, next to the .exe, like everything else.
  //
  //  ORDER: this runs before cod2.toml is read, so the toml can change the
  //  folder or leave it empty to copy nothing.
  // ==========================================================================
  void ElegirCarpetaDeGuardados() {
    if (rex::cvar::GetFlagInfo("content_backup_root") == nullptr) {
      return;  // SDK without that option: nothing happens
    }
    if (!rex::cvar::GetFlagByName("content_backup_root").empty()) {
      return;  // given on the command line
    }
    const auto carpeta = rex::filesystem::GetExecutableFolder();
    if (carpeta.empty()) {
      return;
    }
    const auto destino = carpeta / "saves";
    if (!rex::cvar::SetFlagByName("content_backup_root", rex::path_to_utf8(destino))) {
      REXLOG_WARN("[guardado] no se pudo fijar la carpeta de copias en {}",
                  rex::path_to_utf8(destino));
    }
  }

  // ==========================================================================
  //  3. MANDATORY SETTINGS
  //
  //  So that a plain "cod2.exe", without a single argument, starts just as well
  //  as with the usual long command line.
  //
  //  Only the settings not set explicitly are touched: HasNonDefaultValue tells
  //  "this is the factory default" from "someone asked for this". That way the
  //  command line and cod2.toml still take precedence.
  // ==========================================================================
  void OnPostInitLogging() override {
    // The graphics settings that work with the native renderer. Both calls lost their caller when the
    // renderer was parked (commit af5a0ab deleted the line; the file went to src/aparcado/), and the file
    // has now come back - so the calls come back with it. They are not cosmetic:
    //   AplicarAjustesGraficos  internal resolution and FPS limit into the video mode before the game asks
    //                           for it, the console overlays, and the perf interval.
    //   OcultarAjustesSinEfecto removes from the F4 menu what does nothing under the native renderer.
    //   VigilarAjustesEnVivo    what makes AntialiasingFxaa(), ResplandorCielo(), TratamientoVisual() and
    //                           VersionPosproceso() return anything: the renderer reads all four
    //                           (cod2_nativo_destinos.cpp), and without the watcher they are always off.
    cod2::ajustes::AplicarAjustesGraficos();
    cod2::ajustes::OcultarAjustesSinEfecto();
    cod2::ajustes::VigilarAjustesEnVivo();
    // Without a GPU plugin the screen stays black: the game runs, but the
    // runtime discards its graphics calls with "no GPU emulation loaded".
    PonerSiNadieLoPidio("gpu_plugin", "xenos");
#if !REX_PLATFORM_SWITCH
    // Keyboard and mouse in addition to the gamepad. The Switch has neither:
    // the synthetic device would only shadow the libnx controllers.
    PonerSiNadieLoPidio("mnk_mode", "true");
#endif
    // The console's GPU profile, which the game never asks for on its own and without which the GPU is
    // the wall of every in-game frame (cod2_rendimiento.cpp). Off with cod2_switch_gpu_mhz = 0.
    cod2::rendimiento::AplicarRendimiento();
  }

  void OnPostSetup() override {
    ArrancarVigilante();
    // Sampling CPU profiler, PC only (cod2_perfil_pc_desde_s; off by default).
    cod2::perfil_pc::Arrancar();
    // Host address of every recompiled function, PC only: without it a crash address inside the game's
    // code cannot be turned back into the sub_XXXXXXXX the generated code speaks in
    // (tools/pc/direccion.py).
    cod2::pc_simbolos::Escribir();
    /*
     * cod2_captura_cada_s. It has to be started HERE and not in the native graphics system, which is
     * where it used to be: the whole point of the capture is to compare the two renderers
     * (cod2_renderizador = "nativo" against the SDK's Xenos emulation, which is the reference), and a
     * capture thread that only exists in one of them cannot do that. Measured: with the capture started
     * from the native system, a run with cod2_renderizador = "xenos" wrote no PNG at all and said
     * nothing about it.
     *
     * This is the right moment: OnPostSetup runs at the end of ConstructRuntime, after SetupPresentation
     * has already stood the presenter up (sdk/src/ui/rex_app.cpp:105 and :115), and it runs for both
     * backends because both reach their presenter through IGraphicsSystem. OnShutdown runs first in
     * OnDestroy, before the presenter is torn down, so the thread is joined while the pointer is still
     * valid.
     *
     * The presenter is asked for on every capture and not kept: with the native renderer it is created
     * during setup and destroyed on a device loss, and a stale pointer would be a crash, not a wrong PNG.
     */
    cod2::captura::Arrancar([this]() -> rex::ui::Presenter* {
      const rex::Runtime* rt = runtime();
      const rex::system::IGraphicsSystem* gs = rt ? rt->graphics_system() : nullptr;
      return gs ? gs->presenter() : nullptr;
    });
    /*
     * The frame rate diagnosis (cod2_nativo_sistema.cpp, PedirDiagnosticoFps).
     *
     * On the console it is the pad combo L+R+ZR (switch_input_driver.cpp), which is why the port also exports
     * Cod2DiagnosticoFps for it. On the PC there is no pad combo to press, so the same request is a keybind
     * here, F8 by default and rebindable like the SDK's own (bind_console and friends): it shows up in the F4
     * settings menu and is saved in the configuration.
     *
     * It is registered from the app and not from the SDK for the same reason the watchdog lives here: it is
     * the port's own instrument, and the SDK should not carry it.
     */
    rex::ui::RegisterBind("bind_cod2_diagnostico", "F8",
                          "COD2: frame rate diagnosis (the native renderer writes the verdict to the log)",
                          [] { cod2::nativo::PedirDiagnosticoFps(); });
  }

  void OnShutdown() override {
    cod2::captura::Parar();
    rex::ui::UnregisterBind("bind_cod2_diagnostico");
    cod2::perfil_pc::Parar();
    PararVigilante();
  }

 private:
  // "Only if nobody asked" lives in cod2_rendimiento.cpp, next to the defaults that use it.
  static void PonerSiNadieLoPidio(const char* nombre, const char* valor) {
    cod2::rendimiento::PonerSiNadieLoPidio(nombre, valor);
  }

  // ==========================================================================
  //  4. HANG WATCHDOG
  //
  //  THE PROBLEM IT SOLVES
  //  When the game freezes, absolutely nothing shows up in the log: no error,
  //  no kernel call, no graphics command. Total silence. That rules out an
  //  exception or an unregistered function (those are visible) and leaves a
  //  single explanation: all the game's threads are stopped at once, waiting
  //  for something that never arrives.
  //
  //  And a deadlock cannot be diagnosed from the log, because what defines it
  //  is precisely that nothing gets written anymore. The threads have to be
  //  asked directly.
  //
  //  HOW IT WORKS
  //  A separate thread looks at all guest threads once per second and records
  //  two registers of each one:
  //
  //    lr  where the current function would return to. It changes constantly
  //        in code that makes progress.
  //    r1  the stack pointer. Same.
  //
  //  If for several seconds in a row no thread has moved either of the two, the
  //  game is not slow: it is stopped. Then the table is dumped.
  //
  //  WHAT THE DUMP GIVES
  //  For each thread: its entry address (which tells which thread it is), lr,
  //  r1 and r13. With that, a thread that waits (lr stuck in a kernel wait
  //  function) can be told apart from one that spins (lr jumping between two or
  //  three addresses).
  //
  //  It lives in the app and not in the SDK on purpose: that way it can be
  //  changed without rebuilding the whole SDK, and it does not impose a
  //  watchdog thread on anyone else.
  // ==========================================================================

  void ArrancarVigilante() {
    vigilante_activo_ = true;
    vigilante_ = std::thread([this] { VigilanteMain(); });
  }

  void PararVigilante() {
    vigilante_activo_ = false;
    if (vigilante_.joinable()) {
      vigilante_.join();
    }
  }

  template <typename Lista>
  static void VolcarHilos(const Lista& hilos, bool grave) {
    for (auto& h : hilos) {
      const auto* cp = h->creation_params();
      auto* estado = h->thread_state();
      if (estado && estado->context()) {
        const auto& c = *estado->context();
        if (grave) {
          REXLOG_ERROR("[vigilante]   hilo id=0x{:X} entrada=0x{:08X} principal={} corriendo={} | "
                       "lr=0x{:08X} r1=0x{:08X} r13=0x{:08X} r3=0x{:08X} ctr=0x{:08X} "
                       "ultimo_indirecto=0x{:08X}",
                       h->thread_id(), cp->start_address, h->main_thread(), h->is_running(),
                       static_cast<uint32_t>(c.lr), c.r1.u32, c.r13.u32, c.r3.u32, c.ctr.u32,
                       c.last_indirect_target);
        } else {
          REXLOG_DEBUG("[vigilante]   hilo id=0x{:X} entrada=0x{:08X} principal={} corriendo={} | "
                       "lr=0x{:08X} r1=0x{:08X} r13=0x{:08X} r3=0x{:08X} ctr=0x{:08X} "
                       "ultimo_indirecto=0x{:08X}",
                       h->thread_id(), cp->start_address, h->main_thread(), h->is_running(),
                       static_cast<uint32_t>(c.lr), c.r1.u32, c.r13.u32, c.r3.u32, c.ctr.u32,
                       c.last_indirect_target);
        }
      } else {
        REXLOG_DEBUG("[vigilante]   hilo id=0x{:X} entrada=0x{:08X} sin contexto", h->thread_id(),
                     cp->start_address);
      }
    }
  }

  void VigilanteMain() {
    // How many seconds in a row without anything moving before raising the
    // alarm. Five is generous: a game at 10 fps still moves registers a hundred
    // times per second, so five still seconds are not slowness.
    constexpr int kSegundosParaSospechar = 5;
    constexpr int kSegundosEntreVolcados = 15;

    uint64_t firma_anterior = 0;
    int quietos = 0;
    int desde_ultimo_volcado = 0;
    int desde_instantanea = 0;
    bool avisado = false;
    int volcados_pilas = 0;  // logs/pilas_N.txt on PC (cod2_perfil_pc.cpp)

    while (vigilante_activo_) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      if (!vigilante_activo_) break;

      auto* kernel = rex::system::kernel_state();
      if (!kernel) continue;

      auto hilos = kernel->object_table()->GetObjectsByType<rex::system::XThread>();
      if (hilos.empty()) continue;

      // A signature of "where everyone is". It does not need to be a good hash:
      // it only has to change if some register changes.
      //
      // Watch the order. GetObjectsByType does not guarantee it: in the real
      // dumps the threads came out shuffled from one round to the next, and
      // even repeated. So the signature changed on its own even when nothing
      // moved, and the alarm never fired. Putting each thread in a map keyed by
      // its id fixes it: the map sorts itself and a repeated id overwrites
      // instead of being counted twice.
      std::map<uint32_t, uint64_t> por_hilo;
      for (auto& h : hilos) {
        auto* estado = h->thread_state();
        if (!estado || !estado->context()) continue;
        const auto& c = *estado->context();
        por_hilo[h->thread_id()] =
            static_cast<uint64_t>(c.lr) ^ (static_cast<uint64_t>(c.r1.u32) << 20) ^
            (static_cast<uint64_t>(c.r3.u32) << 40);
      }

      uint64_t firma = 1469598103934665603ull;
      for (const auto& [id_hilo, huella] : por_hilo) {
        firma = (firma ^ id_hilo) * 1099511628211ull;
        firma = (firma ^ huella) * 1099511628211ull;
      }

      // PERIODIC SNAPSHOT, WHATEVER HAPPENS.
      //
      // The alarm above only fires if nothing moves, and the hangs chased in
      // the fork's game were not of that kind: the registers kept changing,
      // meaning the game runs code but makes no progress. A tight loop waiting
      // for something that never arrives looks just as stuck from outside, and
      // yet the alarm does not catch it.
      //
      // So every ten seconds it records where each thread is, whether there is
      // a problem or not. When the game freezes, two or three snapshots of the
      // bad stretch remain, and if lr cycles between the same two or three
      // addresses, there is the loop. Debug level: no noise in normal use.
      if (++desde_instantanea >= 10) {
        desde_instantanea = 0;
        REXLOG_DEBUG("[vigilante] instantanea: {} hilos del juego", hilos.size());
        VolcarHilos(hilos, false);
      }

      if (firma != firma_anterior) {
        if (avisado) {
          REXLOG_WARN("[vigilante] el juego ha vuelto a moverse despues de {} s parado.", quietos);
          avisado = false;
        }
        firma_anterior = firma;
        quietos = 0;
        desde_ultimo_volcado = 0;
        continue;
      }

      ++quietos;
      ++desde_ultimo_volcado;
      if (quietos < kSegundosParaSospechar) continue;
      if (avisado && desde_ultimo_volcado < kSegundosEntreVolcados) continue;
      desde_ultimo_volcado = 0;

      REXLOG_ERROR("[vigilante] {} s sin que se mueva ni un registro en ninguno de los {} hilos "
                   "del juego. Esto no es lentitud: esta parado.",
                   quietos, hilos.size());
      VolcarHilos(hilos, true);
      // And the stacks of every thread in the process, host ones included: on
      // PC it writes logs/pilas_N.txt (cod2_perfil_pc.cpp). Only on the first
      // two alarms.
      if (volcados_pilas < 2) {
        ++volcados_pilas;
        cod2::perfil_pc::VolcarPilas("vigilante");
      }
      avisado = true;
    }
  }

  std::thread vigilante_;
  std::atomic<bool> vigilante_activo_{false};
};
