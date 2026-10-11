// cod2 - the game's Bink cutscenes decoded by FFmpeg instead of by the guest's own Bink library
//
// The failure this exists for
//     A cutscene runs at 0.46 frames per second on the console, against 26 in the menus and 3 in game
//     (docs/audio-and-video.md). Only 8.7 % of that is the guest's own Bink decoder: 58 % is exception machinery,
//     because the surface the decoder writes is one the port watches, and the next frame reads it back for motion
//     compensation - about one faulting read per pixel. Decoding outside the guest removes both.
//
// The evidence
//     Measured on this game, not inherited. `sub_82496B20` is BinkOpen (it compares the file's first dword against
//     BIKf/g/h/i and owns all five Bink error strings), `sub_82497D90` is the per-frame entry, and `sub_8249F980` is
//     one frame's three planes: it is called once per frame from `sub_82497D90` and from nowhere else.
//
//     The frame descriptor `sub_8249F980` is handed, read out of the running game (app/src/cod2_video_bink_gancho.cpp
//     is the tool that dumps it):
//
//       +0x04 width    +0x08 height    +0x0C width/2    +0x10 height/2    +0x14 which surface is being written
//       +0x18 + i*48: two double-buffered surfaces, each four 12-byte slots of (flag, pointer, stride) - the three
//       planes in Bink's own order, Y then V then U, and a fourth the game leaves at zero. `sub_8249F980` flips
//       +0x14 to the other surface when that surface's Y pointer is not null, writes the three planes into the new
//       one and reads the old one as the reference frame.
//
//     That layout is checked, not assumed: with --no-cod2_video_bink_nativo the guest's own decoder writes the
//     surfaces, and the port's native output is then compared against it frame by frame
//     (tools/comprobar_bink_planos.py, and the procedure is in the header of the gancho file). Both chroma planes
//     have to be compared on a frame with colour in it: these movies all open on a colourless fade from black,
//     which passes whatever order the planes are written in.
//
// What this does
//     Replaces `sub_8249F980`: decodes the frame with the native decoder and writes the three planes into the
//     surfaces the descriptor names, doing the same flip. The guest's decoder is not run at all, so the watched
//     surface is only ever written, never read.
//
// How to turn it off without a rebuild
//     `--no-cod2_video_bink_nativo`. Every call then goes to the guest's own decoder and the cutscenes are back at
//     0.46 frames per second, which is what makes this comparable on the console.

#include "cod2_video_bink.h"

#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/xmemory.h>

REXCVAR_DEFINE_BOOL(cod2_video_bink_nativo, true, "COD2",
                    "COD2: descodifica las cinematicas de Bink con FFmpeg en vez de ejecutar la biblioteca de Bink "
                    "del propio juego. Apagado vuelve al camino del juego (0.46 fotogramas por segundo)");

extern "C" void __imp__sub_82496B20(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_8249F980(PPCContext& __restrict ctx, uint8_t* base);

namespace {

// Bink context.
constexpr uint32_t kIndiceFotograma = 0x0C;  // the frame about to be decoded, plus one
constexpr uint32_t kLimite = 740;            // what the value below is compared against
// Frame descriptor.
constexpr uint32_t kSuperficie = 20;         // which of the two surfaces is being written
constexpr uint32_t kSuperficies = 24;        // the first surface
constexpr uint32_t kPasoSuperficie = 48;     // and the stride between them
constexpr uint32_t kPuntero = 4;             // within a surface's 12-byte slot: pointer, then stride
constexpr uint32_t kPaso = 8;
constexpr uint32_t kRanura = 12;             // between one plane's slot and the next

// The game's three slots hold Bink's three planes in Bink's own order, and Bink's order is not FFmpeg's.
//
// Bink's bitstream carries Y, then V, then U. FFmpeg's decoder reads them in that order and writes the second into
// `data[2]` and the third into `data[1]`, because an `AVFrame` has to be standard YUV420P - that is exactly what its
// `swap_planes` does for version >= 'h' (`libavcodec/bink.c`: `plane_idx = plane ? plane ^ 3 : plane`). The guest's
// own decoder is the real Bink library and writes each bitstream plane into the k-th slot the caller passes, so the
// game's slot 1 holds V and its slot 2 holds U.
//
// So the two chroma planes cross over on the way in. Getting this wrong mirrors the hue, and on a game this warm -
// sand, fire, dusk - that reads as "the colours have gone blue". It is invisible in the check unless the dump is
// taken from a frame with colour in it: every one of these movies opens on a colourless fade from black, which
// passes whatever order the planes are written in. `atvi.bik` has colour from frame 300.
constexpr int kPlanoDeRanura[3] = {0, 2, 1};

uint32_t LeerU32(uint8_t* base, uint32_t direccion) {
  return __builtin_bswap32(rex::memory::GuestPtr<const uint32_t*>(base, direccion)[0]);
}

void EscribirU32(uint8_t* base, uint32_t direccion, uint32_t valor) {
  rex::memory::GuestPtr<uint32_t*>(base, direccion)[0] = __builtin_bswap32(valor);
}

// The path BinkOpen is given. It is a guest string, so it ends at the first NUL.
std::string CaminoInvitado(uint8_t* base, uint32_t direccion) {
  if (direccion < 0x1000) {
    return {};
  }
  const char* p = rex::memory::GuestPtr<const char*>(base, direccion);
  size_t n = 0;
  while (n < 260 && p[n]) {
    ++n;
  }
  return std::string(p, n);
}

struct Estado {
  std::mutex cerrojo;
  cod2::video_bink::PeliculaBink pelicula;
  uint32_t contexto = 0;
  uint32_t descriptor = 0;
  bool lista = false;
};

Estado g_estado;

// Writes the decoded frame into the two surfaces the descriptor names, flipping between them the way the guest does.
void EscribirPlanos(uint8_t* base, uint32_t descriptor, const cod2::video_bink::Fotograma& f) {
  uint32_t indice = LeerU32(base, descriptor + kSuperficie);
  const uint32_t otro = indice ^ 1;
  if (LeerU32(base, descriptor + otro * kPasoSuperficie + kSuperficies + kPuntero) != 0) {
    indice = otro;
    EscribirU32(base, descriptor + kSuperficie, otro);
  }
  for (int p = 0; p < 3; ++p) {
    const uint32_t ranura = uint32_t(p) * kRanura;
    const uint32_t destino = LeerU32(base, descriptor + indice * kPasoSuperficie + kSuperficies + ranura + kPuntero);
    // The stride comes from the other surface, exactly as the guest reads it.
    const uint32_t paso = LeerU32(base, descriptor + otro * kPasoSuperficie + kSuperficies + ranura + kPaso);
    const int q = kPlanoDeRanura[p];
    if (destino < 0x1000 || paso == 0 || !f.planos[q]) {
      continue;
    }
    const int ancho = p ? (f.ancho + 1) / 2 : f.ancho;
    const int alto = p ? (f.alto + 1) / 2 : f.alto;
    for (int y = 0; y < alto; ++y) {
      std::memcpy(rex::memory::GuestPtr<uint8_t*>(base, destino + uint32_t(y) * paso),
                  f.planos[q] + size_t(y) * size_t(f.pasos[q]), size_t(ancho));
    }
  }
}

}  // namespace

// BinkOpen. r3 is the finished path going in, and the 17,408-byte context coming out.
REX_HOOK_RAW(sub_82496B20) {
  const bool activo = REXCVAR_GET(cod2_video_bink_nativo);
  const uint32_t camino = ctx.r3.u32;
  __imp__sub_82496B20(ctx, base);
  if (!activo) {
    return;
  }
  std::lock_guard<std::mutex> cerrojo(g_estado.cerrojo);
  // Whatever was open is finished with, whether or not this one works out.
  g_estado.lista = false;
  g_estado.contexto = 0;
  g_estado.descriptor = 0;
  const uint32_t contexto = ctx.r3.u32;
  if (contexto < 0x1000) {
    return;
  }
  const std::string ruta = CaminoInvitado(base, camino);
  if (ruta.size() < 4 || ruta.compare(ruta.size() - 4, 4, ".bik") != 0) {
    return;
  }
  if (!g_estado.pelicula.Abrir(ruta)) {
    REXLOG_WARN("[video] Bink nativo: el juego abre '{}' y no se pudo abrir aqui; esa pelicula la descodifica el "
                "invitado",
                ruta);
    return;
  }
  g_estado.contexto = contexto;
  g_estado.lista = true;
  REXLOG_INFO("[video] Bink nativo: '{}' {}x{}, {} fotogramas, contexto 0x{:08X}", ruta, g_estado.pelicula.ancho(),
              g_estado.pelicula.alto(), g_estado.pelicula.fotogramas(), contexto);
}

// One frame's three planes. r3 is the frame descriptor, r7 says whether the frame stands on its own, and r9/r10 gate
// a fourth plane this game's movies do not have.
REX_HOOK_RAW(sub_8249F980) {
  if (REXCVAR_GET(cod2_video_bink_nativo)) {
    std::lock_guard<std::mutex> cerrojo(g_estado.cerrojo);
    const uint32_t descriptor = ctx.r3.u32;
    // A movie the game opened but this build did not (or a second context): leave it to the guest.
    const bool nativo = g_estado.lista && descriptor >= 0x1000 &&
                        (g_estado.descriptor == 0 || g_estado.descriptor == descriptor) &&
                        // The fourth plane is gated on these two bits. FFmpeg's decoder is not given the alpha
                        // plane, so a movie that has one is left to the guest rather than half decoded.
                        (ctx.r10.u32 & 0x100000) == 0 && (ctx.r9.u32 & 0x100000) == 0;
    if (nativo) {
      if (g_estado.descriptor == 0) {
        g_estado.descriptor = descriptor;
      }
      const uint32_t indice = LeerU32(base, g_estado.contexto + kIndiceFotograma) - 1;
      cod2::video_bink::Fotograma f;
      // Seeking is not every-frame: `Siguiente` is the ordinary case and only a jump pays for decoding forward from
      // the nearest keyframe, which for these movies is frame 0.
      const bool ok = indice == g_estado.pelicula.indice_actual() ? g_estado.pelicula.Siguiente(f)
                                                                  : g_estado.pelicula.IrA(indice, f);
      if (ok) {
        EscribirPlanos(base, descriptor, f);
        // `sub_82497D90` stores this in the context at +736 and `sub_82497990` compares it with +740; the counter
        // itself is a running tally inside the guest's bit reader and cannot be reproduced. What can is the branch
        // it decides, which is the only thing that reads it: a keyframe leaves it at zero and every other frame
        // above the limit. ponytail: reproduces the branch, not the number; if a later caller ever uses the
        // magnitude, this has to become the real count.
        ctx.r3.u32 = ctx.r7.u32 ? 0 : LeerU32(base, g_estado.contexto + kLimite);
        return;
      }
      REXLOG_WARN("[video] Bink nativo: el fotograma {} no se pudo descodificar; vuelve el descodificador del juego",
                  indice);
      g_estado.lista = false;
    }
  }
  __imp__sub_8249F980(ctx, base);
}
