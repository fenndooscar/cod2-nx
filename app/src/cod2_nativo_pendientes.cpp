// cod2 - the native renderer's seams: the pieces the renderer calls that are not derived for this game yet.
//
// WHY THIS FILE EXISTS
//   The renderer's core (src/cod2_nativo_*.cpp) is the previous port's, kept because it is about the Xbox
//   360's hardware and not about its game: the PM4 ring, the Xenos register file, the EDRAM, the Vulkan
//   device, the texture pool. Those files call out, in a small number of places, to pieces that are about
//   the game - the 30 FPS guard, the road-reflection reduction, the D3D register marker, the game's own
//   frame counter. Those pieces are parked in src/aparcado/ and are the other game's.
//
//   Without a definition for each one the renderer does not link at all, which would make the first
//   milestone (the game boots with the port's own graphics system and the screen shows a test colour)
//   impossible to reach. So each one gets the smallest definition that is honest about being a
//   placeholder, and each comment names the parked file that has to replace it and what it must do.
//
// WHAT IS NOT A PLACEHOLDER HERE
//   Nothing that affects what is drawn, because nothing here draws. These are all counters, reports and
//   skip decisions of the other game's optimisations. The one that could change behaviour if it were
//   wrong is MuestrasOriginalesModoActual, and returning 1 is the safe answer: it says "this render
//   target has no multisampling", which makes the renderer treat every target as the simple case.
//
// HOW TO RETIRE THIS FILE
//   One function at a time, when its real version has been re-derived from CoD2's own default.xex: move
//   the parked file back into the build (src/aparcado/README.md, "How to bring one back"), delete the
//   matching stub here, and let the linker tell you if anything else was missing.
//
// WHAT WAS VERIFIED
//   That the renderer links and the app builds with these definitions, and that each stub is reached only
//   from the paths its comment names. Not verified: any of the behaviour, because none of it has run on
//   the console.

#include <atomic>
#include <cstdint>
#include <string>

namespace cod2::guardia30 {

// The 30 FPS guard of the other port: a development aid that stepped the renderer's visual treatment down
// when a race could not hold 30 FPS (see cod2_nativo_destinos.cpp's call to Latir and
// cod2_nativo_dibujos.cpp's to SinSombras). Its real version is in src/aparcado/cod2_recortes_carrera.cpp.
// Nothing here decides anything: Latir and Informe only produce a report, and SinSombras returns the
// user's own setting unchanged, so shadows are drawn exactly as the game asks.
void Latir(double ms) {
  (void)ms;
}

void Informe() {}

bool SinSombras(bool del_usuario) {
  return del_usuario;
}

}  // namespace cod2::guardia30

namespace cod2::escenario_lod {

// The scenery-LOD hook's summary line in the periodic report. Real version:
// src/aparcado/cod2_escenario_lod.cpp. An empty string is the honest answer: no LOD hook is installed.
std::string Resumen() {
  return {};
}

}  // namespace cod2::escenario_lod

namespace cod2::render_targets {

// The multisampling of a video mode before the port touches it, read from the game's own mode table.
// Real version: src/aparcado/cod2_render_targets.cpp. 1 means "no multisampling", which is the safe
// answer: the renderer then takes the simple path for every render target instead of resolving samples
// that are not there.
uint32_t MuestrasOriginalesModoActual(const uint8_t* base) {
  (void)base;
  return 1;
}

}  // namespace cod2::render_targets

namespace cod2::reflejo_demanda {

// Road reflection on demand: the other game drew its road reflection (views 4 and 5) in every race frame
// and skipped it when nothing had read it. Call of Duty 2 has no car and no road reflection, so this whole
// namespace is a no-op here. Real version: src/aparcado/cod2_recortes_carrera.cpp. MedirVisibilidad
// returning false is what switches the whole feature off: the renderer only sets up its occlusion queries
// when it is true.
void AnotarLectura() {}
void AnotarCopia() {}
void AnotarSwap() {}
void AnotarVisible(bool medido) {
  (void)medido;
}
void AnotarOculto() {}
void AnotarTestigo(bool con_muestras) {
  (void)con_muestras;
}

bool MedirVisibilidad() {
  return false;
}

bool VisibilidadComprobada() {
  return false;
}

}  // namespace cod2::reflejo_demanda

namespace cod2::nativo {

// The D3D register marker (see cod2_nativo_ganchos.h): FlushState's ~7 register packets replaced by one
// compound marker the ring understands. Real version: src/aparcado/cod2_d3d_registros_nativo.cpp. The
// marker only exists if FlushState is hooked, and it is not, so the consumer is off and the check has
// nothing to report. ActivarConsumidorMarcadores(false) is what cod2_nativo_sistema.cpp calls at shutdown,
// and (true) at startup only to keep the two symmetric.
void ActivarConsumidorMarcadores(bool activo) {
  (void)activo;
}

void AnotarComprobacionMarcador(bool igual, uint32_t secuencia, uint32_t registro, uint32_t en_registros,
                                uint32_t en_marcador) {
  (void)igual;
  (void)secuencia;
  (void)registro;
  (void)en_registros;
  (void)en_marcador;
}

}  // namespace cod2::nativo

// The game's frame counter. Real version: src/aparcado/cod2_d3d_trace.cpp, which defines it and bumps it
// from a Draw* hook; the renderer only reads it, to report how many game frames went by between two
// reports. With no Draw* hook it stays at zero, and the report says so.
std::atomic<uint64_t> g_cod2_fotogramas_juego{0};
