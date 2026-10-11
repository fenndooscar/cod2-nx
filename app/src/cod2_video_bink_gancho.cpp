// cod2 - what Call of Duty 2's own Bink player is handed, so the native decoder can replace it
//
// The failure this exists for
//     A cutscene runs at 0.46 frames per second on the console (docs/audio-and-video.md). The fix is to decode the
//     `.bik` with FFmpeg instead of letting the guest run its own Bink - `app/src/cod2_video_bink.cpp` already does
//     the decoding and passes on all nine of the game's movies (tools/comprobar_bink.ps1). What is missing is the
//     join: which guest function to replace, and where in its context the compressed frame and the output planes are.
//
// The evidence
//     Read from this game's generated code, not inherited:
//       sub_822D1A30  builds "<name>.bik" (the ".bik" string is at 0x8205C208, found with tools/refiere_datos.py),
//                     calls sub_82496B20 with 17408 and then sub_82499BA0 four times. It is the movie player.
//       sub_82496B20  takes a path and a size and returns a 17408-byte object: the Bink context.
//       sub_82497D90  the per-frame entry; called from sub_822D1A30.
//       sub_8249F980  decodes one frame's planes; called from sub_82497D90, calls sub_8249DA50 four times.
//       sub_8249DA50  decodes one plane, 7,984 bytes, the hottest guest function in a cutscene.
//     What none of that reading gives is the *layout*: which argument is the compressed frame, how long it is, and
//     where the output planes are. Those are offsets, and an offset guessed is worse than no offset at all.
//
// What this does
//     Logs the arguments of the movie player and of the two decoder entry points, dumps the 17,408-byte Bink context
//     once, and can dump eight frames' surfaces out of the frame descriptor. The join itself lives in
//     cod2_video_bink_nativo.cpp, which owns the hooks on sub_82496B20 and sub_8249F980; this file does not hook
//     those two, so that a single file decides what a frame is decoded by.
//
//     The dump is what caught the colour cast: the same eight frames taken twice, once with
//     --no-cod2_video_bink_nativo so the guest's own decoder writes them and once without, are byte-identical only
//     when the port puts FFmpeg's chroma planes in the slots the game puts them in. They were crossed, which mirrors
//     the hue - and on a game this warm, that reads as "the colours have gone blue".
//
// How to turn it off without a rebuild
//     --no-cod2_video_bink_diag (default). --cod2_video_bink_diag=true logs the calls; add
//     --cod2_video_bink_volcar=true to also write the context dump, and --cod2_video_bink_planos=true to write eight
//     frames' surfaces to logs/bink/, which is what tools/comprobar_bink_planos.py compares against the native
//     decoder. All three are off by default because the log is written from the guest's own thread, once per frame.
//
//     --cod2_video_bink_volcar_desde=<frame> moves which frames are dumped. It matters: the first eight frames of
//     every one of these nine movies are a colourless fade from black, and a chroma comparison taken there passes
//     whatever the plane order is. `atvi.bik` has real colour from frame 300 on.

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/xmemory.h>

REXCVAR_DEFINE_BOOL(cod2_video_bink_diag, false, "COD2",
                    "Escribe en el log los argumentos del reproductor de Bink del juego (sub_822D1A30, "
                    "sub_82497D90, sub_8249DA50). Apagado por defecto.");
REXCVAR_DEFINE_BOOL(cod2_video_bink_volcar, false, "COD2",
                    "COD2: vuelca el contexto de Bink (17408 bytes) a logs/bink_contexto.bin la primera vez que "
                    "se decodifica un fotograma");
REXCVAR_DEFINE_BOOL(cod2_video_bink_planos, false, "COD2",
                    "COD2: vuelca a logs/bink/ las tres superficies ya descodificadas de los primeros fotogramas, "
                    "para comprobar que el descodificador nativo da lo mismo que el del juego");
REXCVAR_DEFINE_INT32(cod2_video_bink_volcar_desde, 0, "COD2",
                     "COD2: primer fotograma de cada pelicula que se vuelca con cod2_video_bink_planos. Los ocho "
                     "primeros fotogramas de estas peliculas son un fundido desde negro sin color, asi que una "
                     "comparacion de croma hecha ahi no distingue nada");

// The generated code declares these as weak aliases of the __imp__ entry points; a hook is a strong
// definition of the bare name that calls the original (see cod2_consola.cpp).
extern "C" void __imp__sub_822D1A30(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_82496B20(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_82497D90(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_8249F980(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_8249DA50(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_82497940(PPCContext& __restrict ctx, uint8_t* base);

namespace {

constexpr uint32_t kContextoBink = 17408;  // what sub_822D1A30 asks sub_82496B20 for
constexpr int kLineas = 60;                // per function, so a whole movie does not fill the log

std::atomic<int> g_lineas_822D1A30{0};
std::atomic<int> g_lineas_82497D90{0};
std::atomic<int> g_lineas_8249DA50{0};
std::atomic<bool> g_volcado{false};
std::mutex g_volcado_mutex;

// A guest string, only when it really looks like one (same rule as cod2_consola.cpp).
std::string TextoInvitado(uint8_t* base, uint32_t direccion, size_t tamano) {
  if (direccion < 0x1000) {
    return {};
  }
  auto* origen = rex::memory::GuestPtr<const char*>(base, direccion);
  std::string s;
  for (size_t n = 0; n + 1 < tamano; ++n) {
    const char c = origen[n];
    if (c == '\0') {
      break;
    }
    if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7E) {
      return {};
    }
    s.push_back(c);
  }
  return s.size() >= 3 ? s : std::string();
}

// The first 16 dwords of a guest object, which is where a context's pointers and sizes live.
std::string Campos(uint8_t* base, uint32_t objeto, uint32_t bytes) {
  if (objeto < 0x1000 || bytes > kContextoBink) {
    return {};
  }
  auto* p = rex::memory::GuestPtr<const uint32_t*>(base, objeto);
  std::string s;
  char trozo[32];
  for (uint32_t i = 0; i < bytes / 4; ++i) {
    std::snprintf(trozo, sizeof(trozo), " %08X", p[i]);
    s += trozo;
  }
  return s;
}

void VolcarContexto(uint8_t* base, uint32_t objeto) {
  if (g_volcado.exchange(true, std::memory_order_relaxed)) {
    return;
  }
  std::lock_guard<std::mutex> cerrojo(g_volcado_mutex);
  if (objeto < 0x1000) {
    REXKRNL_WARN("[bink] el contexto 0x{:08X} no es una direccion de invitado", objeto);
    return;
  }
  const std::filesystem::path ruta =
      rex::filesystem::GetExecutableFolder() / "logs" / "bink_contexto.bin";
  std::error_code ec;
  std::filesystem::create_directories(ruta.parent_path(), ec);
  // std::fopen, not _wfopen: the Switch has no wide-character open. Both paths here are the executable's own folder,
  // so the narrow form is the same string on either platform.
  std::FILE* f = std::fopen(ruta.string().c_str(), "wb");
  if (!f) {
    REXKRNL_WARN("[bink] no se puede escribir {}", ruta.string());
    return;
  }
  std::fwrite(rex::memory::GuestPtr<const void*>(base, objeto), 1, kContextoBink, f);
  std::fclose(f);
  REXKRNL_INFO("[bink] contexto de 0x{:08X} volcado a {} ({} bytes)", objeto, ruta.string(), kContextoBink);
}

// The three plane calls of one frame, as sub_8249DA50 was handed them. Only logged: with the native decoder on it is
// never called at all, which is itself the measurement.
constexpr int kFotogramasVolcados = 8;

std::atomic<int> g_fotogramas_volcados{0};

void Escribir(const std::filesystem::path& carpeta, const std::string& nombre, const void* datos, size_t bytes) {
  std::error_code ec;
  std::filesystem::create_directories(carpeta, ec);
  std::FILE* f = std::fopen((carpeta / nombre).string().c_str(), "wb");
  if (!f) {
    REXKRNL_WARN("[bink] no se puede escribir {}", (carpeta / nombre).string());
    return;
  }
  std::fwrite(datos, 1, bytes, f);
  std::fclose(f);
}

// The planes of one frame, read back out of the frame descriptor `sub_8249F980` is handed. Dumping them from the
// descriptor rather than from the decoder's own arguments is what lets the same dump be taken whether the guest's
// decoder or the native one wrote them, which is the whole point: the two have to be compared.
//
// The surfaces are double buffered, `+0x14` says which one was just written, and each of the two is four 12-byte
// slots of (flag, pointer, stride) at `+0x18`, `+0x48`. The first three are the three planes in **Bink's** order -
// Y, V, U - which is not FFmpeg's; the fourth is the alpha plane, which these movies do not have. The stride is read
// from the surface *not* being written, which is what the guest's own code does.
void VolcarFotograma(uint8_t* base, uint32_t descriptor, uint32_t fotograma) {
  const int n = g_fotogramas_volcados.fetch_add(1);
  if (n >= kFotogramasVolcados || descriptor < 0x1000) {
    return;
  }
  const std::filesystem::path carpeta = rex::filesystem::GetExecutableFolder() / "logs" / "bink";
  char etiqueta[32];

  std::snprintf(etiqueta, sizeof(etiqueta), "fotograma_%02d_objeto.bin", n);
  Escribir(carpeta, etiqueta, rex::memory::GuestPtr<const void*>(base, descriptor), 96);

  auto* d = rex::memory::GuestPtr<const uint32_t*>(base, descriptor);
  const uint32_t escrito = __builtin_bswap32(d[0x14 / 4]) & 1;
  const uint32_t otro = escrito ^ 1;
  for (int i = 0; i < 3; ++i) {
    const uint32_t destino = __builtin_bswap32(d[(0x18 + escrito * 0x30 + i * 12 + 4) / 4]);
    const uint32_t paso = __builtin_bswap32(d[(0x18 + otro * 0x30 + i * 12 + 8) / 4]);
    if (destino < 0x1000 || paso == 0) {
      continue;
    }
    const uint32_t alto = __builtin_bswap32(d[0x08 / 4]) >> (i ? 1 : 0);
    std::snprintf(etiqueta, sizeof(etiqueta), "fotograma_%02d_plano%d.bin", n, i);
    Escribir(carpeta, etiqueta, rex::memory::GuestPtr<const void*>(base, destino),
             static_cast<size_t>(paso) * alto);
  }
  REXKRNL_INFO("[bink] fotograma {} de la pelicula (volcado {}) volcado: descriptor 0x{:08X}, superficie {}", fotograma,
               n, descriptor, escrito);
}

}  // namespace

// The movie player. r3 is the name the game wants, without the extension.
REX_HOOK_RAW(sub_822D1A30) {
  if (REXCVAR_GET(cod2_video_bink_diag) && g_lineas_822D1A30.fetch_add(1) < kLineas) {
    REXKRNL_INFO("[bink] sub_822D1A30 nombre '{}' r4={:08X} r5={:08X} r6={:08X} r7={:08X}",
                 TextoInvitado(base, ctx.r3.u32, 128), ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32);
  }
  __imp__sub_822D1A30(ctx, base);
}

// Opens the .bik. Not hooked: `sub_82496B20` is where the native decoder takes over and
// cod2_video_bink_nativo.cpp owns that hook, which logs the path and the context it got.

// The per-frame entry. r3 is the Bink context, and r4 the frame descriptor `sub_8249F980` will be handed.
REX_HOOK_RAW(sub_82497D90) {
  const int n = g_lineas_82497D90.fetch_add(1);
  const uint32_t contexto = ctx.r3.u32;
  if (REXCVAR_GET(cod2_video_bink_diag) && n < kLineas) {
    REXKRNL_INFO("[bink] sub_82497D90 #{} contexto 0x{:08X} campos:{}", n, ctx.r3.u32,
                 Campos(base, ctx.r3.u32, 64));
  }
  if (REXCVAR_GET(cod2_video_bink_volcar) && n == 0) {
    VolcarContexto(base, ctx.r3.u32);
  }
  __imp__sub_82497D90(ctx, base);
  if (REXCVAR_GET(cod2_video_bink_planos)) {
    // The descriptor is context+184, which is what sub_82497D90 loads into r3 before calling sub_8249F980
    // (`lwz r3,184(r31)` at 0x824982A4). context+0x0C is the frame about to be decoded, plus one.
    auto* p = rex::memory::GuestPtr<const uint32_t*>(base, contexto);
    const uint32_t fotograma = __builtin_bswap32(p[0x0C / 4]);
    if (fotograma > uint32_t(REXCVAR_GET(cod2_video_bink_volcar_desde))) {
      VolcarFotograma(base, __builtin_bswap32(p[184 / 4]), fotograma - 1);
    }
  }
  if (REXCVAR_GET(cod2_video_bink_diag) && n < kLineas) {
    // What the frame's decode left behind: +736 is the value sub_8249F980 returns and +740 is what it is compared
    // against (sub_82497990 does `if (736 >= 740)`). Reproducing that branch is the only reason a native decoder
    // needs to know this number at all, so it is measured rather than guessed.
    auto* p = rex::memory::GuestPtr<const uint32_t*>(base, contexto);
    REXKRNL_INFO("[bink] sub_82497D90 #{} deja 736={} 740={}", n, __builtin_bswap32(p[736 / 4]),
                 __builtin_bswap32(p[740 / 4]));
  }
}

// One frame's planes. Not hooked: `sub_8249F980` is the call the native decoder replaces, and
// cod2_video_bink_nativo.cpp owns that hook. Its arguments are r3 the descriptor, r6 the compressed frame, r7 the
// keyframe flag, r9/r10 the bits that gate a fourth plane.

// One plane. r3 is where it is written, r4 the same plane of the previous frame, r5/r6 the size, r7 the stride,
// r8 the compressed data of this plane. With the native decoder on this is never called, so the log is also the
// check that the guest decoder really was taken out of the path.
REX_HOOK_RAW(sub_8249DA50) {
  const int n = g_lineas_8249DA50.fetch_add(1);
  if (REXCVAR_GET(cod2_video_bink_diag) && n < kLineas) {
    REXKRNL_INFO("[bink] sub_8249DA50 #{} r3={:08X} r4={:08X} r5={:08X} r6={:08X} r7={:08X} r8={:08X} r9={:08X} "
                 "r10={:08X}",
                 n, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32, ctx.r8.u32, ctx.r9.u32, ctx.r10.u32);
  }
  __imp__sub_8249DA50(ctx, base);
}

// The only reader of the value sub_8249F980 returns. It is reached through the Bink function table at 0x8207E670, so
// nothing in the generated code calls it directly and "is it ever called during a movie" can only be answered by
// counting. If this never fires, a native decoder can leave that field alone.
std::atomic<int> g_lineas_82497940{0};

REX_HOOK_RAW(sub_82497940) {
  const int n = g_lineas_82497940.fetch_add(1);
  if (REXCVAR_GET(cod2_video_bink_diag) && n < kLineas) {
    auto* p = rex::memory::GuestPtr<const uint32_t*>(base, ctx.r3.u32);
    REXKRNL_INFO("[bink] sub_82497940 #{} contexto 0x{:08X} 736={} 740={}", n, ctx.r3.u32,
                 ctx.r3.u32 < 0x1000 ? 0 : __builtin_bswap32(p[736 / 4]),
                 ctx.r3.u32 < 0x1000 ? 0 : __builtin_bswap32(p[740 / 4]));
  }
  __imp__sub_82497940(ctx, base);
}
