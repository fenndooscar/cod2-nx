// cod2 - PNG captures of the game image, taken from inside the port.
//
// The failure this exists for
//     The only way to know what the port is putting on the screen was to photograph the screen, and that lies
//     twice over. Capturing this window with PrintWindow returns an all-black image for this swapchain, and
//     capturing the desktop captures whichever window happens to be on top - which on this machine was once a
//     *different* ReXGlue port, whose menu was then read as "cod2 paints a menu". Neither failure is visible in
//     the PNG: both are just pixels.
//
// What this does instead
//     Asks the presenter for the image it is about to show (Presenter::CaptureGuestOutput) and writes it as a
//     PNG. It is the port's own idea of the frame, taken before the compositor, so no overlapping window and no
//     window manager can be in the way - and it works the same on the console, where there is no screen to
//     photograph at all. Two runs of the same build can be compared (native against emulated) without anybody
//     looking at anything.
//
// Catching a flicker
//     An artefact that appears for a few frames while the player or an NPC moves cannot be caught by hand, so
//     this can be run the other way round: one small capture every few hundred milliseconds for a whole
//     session, and then tools/pc/buscar_parpadeo.py names the frames that differ sharply from their
//     neighbours. cod2_captura_escala writes them downscaled (a 320x180 PNG is about 25 kB and its encoder
//     costs nothing), which is what makes a few hundred captures per run affordable. The capture thread is
//     self-limiting: it asks the presenter for an image that the game has already produced, so a short
//     interval can only ever waste a wake-up, never stall the ring.
//
// How to turn it off without a rebuild
//     --cod2_captura_cada_s=0 (the default, never) or --cod2_captura_cada_ms for a sub-second interval.
//     --cod2_captura_cada_s=5 writes one every 5 s to capturas/ next to the executable, at most
//     --cod2_captura_max (20 by default). The capture runs on its own thread, so it does not stall the ring;
//     the presenter is asked for it on every capture because at startup it may not exist yet.
//
// The PNG is written uncompressed (deflate "stored" blocks): the SDK does not include stb_image_write and this
// is enough for testing. A full-size 1280x720 capture takes about 2.8 MB; the downscaled ones are tiny.

#include "cod2_nativo_captura.h"

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/ui/presenter.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

REXCVAR_DEFINE_INT32(cod2_captura_cada_s, 0, "COD2",
                     "Guardar una captura PNG de la imagen del juego cada N segundos en capturas/ "
                     "junto al ejecutable (0 = nunca; solo para pruebas)")
    .range(0, 3600);
/*
 * The sub-second interval, for catching an artefact that lasts a few frames.
 *
 * A flicker while the player or an NPC moves cannot be caught by hand: the run has to be captured whole and
 * then searched. With --cod2_captura_cada_ms=200 a ten-minute session is 3,000 images; that is only affordable
 * downscaled (--cod2_captura_escala=4 gives 320x180, about 25 kB each), and the search is
 * tools/pc/buscar_parpadeo.py, which names the frames that differ sharply from their neighbours.
 *
 * Why this cannot hurt the game: the capture thread asks the presenter for an image the game has already
 * produced (CaptureGuestOutput copies a finished guest-output image). It never records anything into the
 * frame, so a fast interval can waste a wake-up and nothing else.
 */
REXCVAR_DEFINE_INT32(cod2_captura_cada_ms, 0, "COD2",
                     "Guardar una captura PNG cada N milisegundos (0 = nunca). Tiene prioridad sobre "
                     "cod2_captura_cada_s; para cazar un parpadeo de pocos fotogramas (solo pruebas)")
    .range(0, 60000);
REXCVAR_DEFINE_INT32(cod2_captura_max, 20, "COD2", "Maximo de capturas por ejecucion")
    .range(1, 100000);
REXCVAR_DEFINE_INT32(cod2_captura_escala, 1, "COD2",
                     "Divisor del tamano de las capturas (1 = 1280x720 tal cual, 4 = 320x180). Una captura "
                     "pequena es lo que hace viable capturar cientos por partida")
    .range(1, 16);

namespace cod2::captura {
namespace {

std::mutex g_mutex;
std::condition_variable g_cv;
bool g_parar = false;
std::thread g_hilo;

uint32_t Crc32(const uint8_t* datos, size_t n, uint32_t crc) {
  static const std::array<uint32_t, 256> tabla = [] {
    std::array<uint32_t, 256> t{};
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int k = 0; k < 8; ++k) {
        c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
      }
      t[i] = c;
    }
    return t;
  }();
  for (size_t i = 0; i < n; ++i) {
    crc = tabla[(crc ^ datos[i]) & 0xFF] ^ (crc >> 8);
  }
  return crc;
}

void PonerBe32(std::vector<uint8_t>& salida, uint32_t valor) {
  salida.push_back(uint8_t(valor >> 24));
  salida.push_back(uint8_t(valor >> 16));
  salida.push_back(uint8_t(valor >> 8));
  salida.push_back(uint8_t(valor));
}

void Chunk(std::vector<uint8_t>& salida, const char tipo[4], const std::vector<uint8_t>& datos) {
  PonerBe32(salida, uint32_t(datos.size()));
  const size_t inicio_tipo = salida.size();
  salida.insert(salida.end(), tipo, tipo + 4);
  salida.insert(salida.end(), datos.begin(), datos.end());
  const uint32_t crc =
      Crc32(salida.data() + inicio_tipo, salida.size() - inicio_tipo, 0xFFFFFFFFu) ^ 0xFFFFFFFFu;
  PonerBe32(salida, crc);
}

// RGBX (R8 G8 B8 X8, like rex::ui::RawImage) to 8-bit RGB PNG, divided by `escala` with a box average.
//
// The box average is what keeps a downscaled capture honest for this purpose: a single-pixel artefact would
// vanish in a nearest-neighbour sample, and it survives here as a small colour shift. It also keeps the stored
// texture detail - a 4x reduction is the difference between 2.8 MB and 25 kB per image, i.e. between a hundred
// captures and a few thousand.
std::vector<uint8_t> CodificarPng(const rex::ui::RawImage& imagen, uint32_t escala) {
  const uint32_t ancho_origen = imagen.width;
  const uint32_t alto_origen = imagen.height;
  const uint32_t ancho = std::max(1u, ancho_origen / escala);
  const uint32_t alto = std::max(1u, alto_origen / escala);
  std::vector<uint8_t> crudo;
  crudo.reserve(size_t(alto) * (size_t(ancho) * 3 + 1));
  for (uint32_t y = 0; y < alto; ++y) {
    crudo.push_back(0);  // sin filtro
    for (uint32_t x = 0; x < ancho; ++x) {
      uint32_t suma[3] = {0, 0, 0};
      uint32_t n = 0;
      for (uint32_t dy = 0; dy < escala; ++dy) {
        const uint32_t fy = y * escala + dy;
        if (fy >= alto_origen) {
          break;
        }
        const uint8_t* fila = imagen.data.data() + size_t(fy) * imagen.stride;
        for (uint32_t dx = 0; dx < escala; ++dx) {
          const uint32_t fx = x * escala + dx;
          if (fx >= ancho_origen) {
            break;
          }
          suma[0] += fila[fx * 4];
          suma[1] += fila[fx * 4 + 1];
          suma[2] += fila[fx * 4 + 2];
          ++n;
        }
      }
      const uint32_t div = n ? n : 1;
      crudo.push_back(uint8_t(suma[0] / div));
      crudo.push_back(uint8_t(suma[1] / div));
      crudo.push_back(uint8_t(suma[2] / div));
    }
  }

  // zlib with "stored" blocks and Adler-32 at the end.
  std::vector<uint8_t> zlib = {0x78, 0x01};
  size_t pos = 0;
  do {
    const size_t trozo = std::min<size_t>(65535, crudo.size() - pos);
    const bool ultimo = pos + trozo == crudo.size();
    zlib.push_back(ultimo ? 1 : 0);
    zlib.push_back(uint8_t(trozo));
    zlib.push_back(uint8_t(trozo >> 8));
    zlib.push_back(uint8_t(~trozo));
    zlib.push_back(uint8_t(~trozo >> 8));
    zlib.insert(zlib.end(), crudo.begin() + pos, crudo.begin() + pos + trozo);
    pos += trozo;
  } while (pos < crudo.size());
  uint32_t a = 1, b = 0;
  for (uint8_t byte : crudo) {
    a = (a + byte) % 65521;
    b = (b + a) % 65521;
  }
  PonerBe32(zlib, (b << 16) | a);

  std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  std::vector<uint8_t> ihdr;
  PonerBe32(ihdr, ancho);
  PonerBe32(ihdr, alto);
  ihdr.push_back(8);  // bits per channel
  ihdr.push_back(2);  // RGB
  ihdr.push_back(0);  // deflate
  ihdr.push_back(0);  // filtro adaptativo
  ihdr.push_back(0);  // sin entrelazado
  Chunk(png, "IHDR", ihdr);
  Chunk(png, "IDAT", zlib);
  Chunk(png, "IEND", {});
  return png;
}

void Bucle(std::function<rex::ui::Presenter*()> obtener_presentador, int cada_ms, int maximo,
           uint32_t escala) {
  const std::filesystem::path carpeta = rex::filesystem::GetExecutableFolder() / "capturas";
  const auto inicio = std::chrono::steady_clock::now();
  for (int n = 1; n <= maximo; ++n) {
    {
      std::unique_lock<std::mutex> cerrojo(g_mutex);
      if (g_cv.wait_for(cerrojo, std::chrono::milliseconds(cada_ms), [] { return g_parar; })) {
        return;
      }
    }
    rex::ui::Presenter* presentador = obtener_presentador ? obtener_presentador() : nullptr;
    rex::ui::RawImage imagen;
    if (!presentador || !presentador->CaptureGuestOutput(imagen) || !imagen.width ||
        !imagen.height) {
      REXLOG_WARN("[captura] {}: no hay imagen del juego que capturar", n);
      continue;
    }
    std::error_code ec;
    std::filesystem::create_directories(carpeta, ec);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - inicio)
                        .count();
    // The millisecond stamp is part of the name because a flicker is a handful of frames: the sequence is read
    // by the time between captures, and a name that only counted seconds put thousands of images in the same
    // bucket.
    char nombre[64];
    std::snprintf(nombre, sizeof(nombre), "captura_%05d_%06lldms.png", n,
                  static_cast<long long>(ms));
    const std::filesystem::path ruta = carpeta / nombre;
    const std::vector<uint8_t> png = CodificarPng(imagen, escala);
    std::ofstream fichero(ruta, std::ios::binary);
    fichero.write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size()));
    if (!fichero) {
      REXLOG_WARN("[captura] No se pudo escribir {}", ruta.string());
      continue;
    }
    REXLOG_INFO("[captura] {} ({}x{} de {}x{})", ruta.string(), imagen.width / escala,
                imagen.height / escala, imagen.width, imagen.height);
  }
}

}  // namespace

void Arrancar(std::function<rex::ui::Presenter*()> obtener_presentador) {
  // The sub-second interval wins when it is set: a run that wants a flicker wants it captured densely.
  const int cada_ms = REXCVAR_GET(cod2_captura_cada_ms) > 0
                          ? int(REXCVAR_GET(cod2_captura_cada_ms))
                          : int(REXCVAR_GET(cod2_captura_cada_s)) * 1000;
  if (cada_ms <= 0 || g_hilo.joinable()) {
    return;
  }
  {
    std::lock_guard<std::mutex> cerrojo(g_mutex);
    g_parar = false;
  }
  const uint32_t escala = uint32_t(REXCVAR_GET(cod2_captura_escala));
  g_hilo = std::thread(Bucle, std::move(obtener_presentador), cada_ms,
                       int(REXCVAR_GET(cod2_captura_max)), escala);
  REXLOG_INFO("[captura] Una captura cada {} ms, 1/{} de tamano (maximo {})", cada_ms, escala,
              int(REXCVAR_GET(cod2_captura_max)));
}

void Parar() {
  {
    std::lock_guard<std::mutex> cerrojo(g_mutex);
    g_parar = true;
  }
  g_cv.notify_all();
  if (g_hilo.joinable()) {
    g_hilo.join();
  }
}

}  // namespace cod2::captura
