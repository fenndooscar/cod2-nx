// cod2 - self-check for the native Bink path (cod2_video_bink.cpp)
//
// It parses a real `.bik` file's container and decodes three of its frames with FFmpeg, then asserts what it found
// against what the caller says the file is. This is the check that fails if the header offsets, the frame index, the
// audio/video split or the decoder setup drift: all four are silent when they are wrong - a wrong offset gives a
// plausible number, and a decoder opened without its version or its extradata simply returns no frame.
//
// Built and run by tools/comprobar_bink.ps1, which compiles cod2_video_bink.cpp with COD2_BINK_SIN_SDK so that the
// part that opens the game's files is left out and nothing of the runtime is needed.
//
// Usage: cod2_video_bink_prueba <fichero.bik> <ancho> <alto> <fotogramas> <pistas_audio> [--volcar <carpeta>]
//
// With `--volcar` it also decodes the first eight frames in order and writes their three planes, tightly packed, as
// `nuestro_NN_planoP.bin`. These are FFmpeg's planes in FFmpeg's order, which is Y, U, V; the game's own surfaces
// hold Bink's order, Y, V, U, so a comparison against a dump of the game has to cross the two chroma planes. For
// checking the port's join, the reference should be the game's own decoder instead - see
// tools/comprobar_bink_planos.py. What this is for is looking at a frame.
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "cod2_video_bink.h"

using namespace cod2::video_bink;

namespace {

bool LeerTodo(const std::string& ruta, std::vector<uint8_t>& fuera) {
  std::ifstream f(ruta, std::ios::binary | std::ios::ate);
  if (!f) {
    return false;
  }
  const auto tam = f.tellg();
  f.seekg(0);
  fuera.resize(size_t(tam));
  f.read(reinterpret_cast<char*>(fuera.data()), tam);
  return bool(f);
}

// A copy of a frame's luma plane, tightly packed.
//
// It has to be a copy: `Fotograma`'s planes point into the decoder's own frame, and FFmpeg hands the same buffer back
// on a later call. Comparing two `Fotograma`s directly compares whatever the decoder is holding *now*, which made
// this check report two identical frames for every file.
struct Luma {
  std::vector<uint8_t> bytes;
  int ancho = 0;
  int alto = 0;

  size_t Valores() const {
    std::set<uint8_t> valores(bytes.begin(), bytes.end());
    return valores.size();
  }
};

Luma CopiarLuma(const Fotograma& f) {
  Luma l;
  l.ancho = f.ancho;
  l.alto = f.alto;
  l.bytes.resize(size_t(f.ancho) * size_t(f.alto));
  for (int y = 0; y < f.alto; ++y) {
    std::memcpy(&l.bytes[size_t(y) * size_t(f.ancho)], f.planos[0] + size_t(y) * f.pasos[0], size_t(f.ancho));
  }
  return l;
}

// How many rows of luma differ between two frames. Zero means the decoder produced the same picture twice.
size_t DiferenciaLuma(const Luma& a, const Luma& b) {
  size_t distintas = 0;
  for (int y = 0; y < a.alto; ++y) {
    distintas += size_t(std::memcmp(&a.bytes[size_t(y) * size_t(a.ancho)], &b.bytes[size_t(y) * size_t(b.ancho)],
                                    size_t(a.ancho)) != 0);
  }
  return distintas;
}

// Decodes frame `i`: reads its region, splits the audio off it and hands the picture to the decoder.
bool DecodificarFotograma(DescodificadorBink& desc, const std::vector<uint8_t>& datos,
                          const std::vector<FotogramaIndice>& indice, uint32_t pistas_audio, uint32_t i,
                          Fotograma& salida) {
  const FotogramaIndice& f = indice[i];
  size_t inicio_video = 0;
  size_t bytes_video = 0;
  if (!SepararAudioBink(datos.data() + f.inicio, f.bytes, pistas_audio, inicio_video, bytes_video)) {
    return false;
  }
  return desc.Descodificar(datos.data() + f.inicio + inicio_video, bytes_video, f.clave, salida);
}

// The three planes of one frame, tightly packed: `ancho` bytes per row, no stride.
bool VolcarPlanos(const std::string& carpeta, uint32_t n, const Fotograma& f) {
  for (int p = 0; p < 3; ++p) {
    const int ancho = p ? (f.ancho + 1) / 2 : f.ancho;
    const int alto = p ? (f.alto + 1) / 2 : f.alto;
    char ruta[512];
    std::snprintf(ruta, sizeof(ruta), "%s/nuestro_%02u_plano%d.bin", carpeta.c_str(), n, p);
    std::ofstream salida(ruta, std::ios::binary | std::ios::trunc);
    if (!salida) {
      return false;
    }
    for (int y = 0; y < alto; ++y) {
      salida.write(reinterpret_cast<const char*>(f.planos[p] + size_t(y) * size_t(f.pasos[p])), ancho);
    }
    if (!salida) {
      return false;
    }
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 6 && !(argc == 8 && std::strcmp(argv[6], "--volcar") == 0)) {
    std::fprintf(stderr, "uso: %s <fichero.bik> <ancho> <alto> <fotogramas> <pistas_audio> [--volcar <carpeta>]\n",
                 argv[0]);
    return 2;
  }
  const std::string ruta = argv[1];
  const uint32_t ancho = uint32_t(std::atoi(argv[2]));
  const uint32_t alto = uint32_t(std::atoi(argv[3]));
  const uint32_t fotogramas = uint32_t(std::atoi(argv[4]));
  const uint32_t pistas_audio = uint32_t(std::atoi(argv[5]));

  std::vector<uint8_t> datos;
  if (!LeerTodo(ruta, datos)) {
    std::fprintf(stderr, "no se puede leer '%s'\n", ruta.c_str());
    return 2;
  }

  CabeceraBink cab;
  assert(LeerCabeceraBink(datos.data(), datos.size(), cab) && "no se pudo leer la cabecera");
  assert(cab.version() == 'i' && "la version no es 'i'");
  assert(cab.ancho == ancho && "el ancho no es el esperado");
  assert(cab.alto == alto && "el alto no es el esperado");
  assert(cab.fotogramas == fotogramas && "el numero de fotogramas no es el esperado");
  // The audio count is what the audio table's length is derived from, so a file with tracks and a file without them
  // are both worth running: get the length wrong and the frame index is read from the wrong place.
  assert(cab.pistas_audio == pistas_audio && "el numero de pistas de audio no es el esperado");
  assert(cab.tamano_fichero == datos.size() && "el tamano declarado no es el del fichero");

  std::vector<FotogramaIndice> indice;
  assert(LeerIndiceBink(datos.data() + cab.inicio_indice, datos.size() - size_t(cab.inicio_indice), cab, indice) &&
         "no se pudo leer el indice");
  assert(indice.size() == fotogramas && "el indice no tiene un elemento por fotograma");
  assert(indice[0].inicio == cab.inicio_datos && "el primer fotograma no empieza donde dice el indice");
  assert(indice[0].clave && "el primer fotograma no esta marcado como clave");
  for (size_t i = 1; i < indice.size(); ++i) {
    assert(indice[i].inicio == indice[i - 1].inicio + indice[i - 1].bytes && "el indice tiene un hueco");
    assert(indice[i].bytes > 0 && "un fotograma ocupa cero bytes");
  }

  DescodificadorBink desc;
  assert(desc.Abrir(cab) && "no se pudo abrir el descodificador");

  // The reference for the join: the first eight frames, in order and from a fresh decoder, which is how the console's
  // own dump numbers them.
  if (argc == 8) {
    for (uint32_t i = 0; i < 8; ++i) {
      Fotograma f;
      assert(DecodificarFotograma(desc, datos, indice, pistas_audio, i, f) &&
             "no se pudo descodificar uno de los ocho primeros fotogramas");
      assert(VolcarPlanos(argv[7], i, f) && "no se pudo escribir el volcado de un fotograma");
    }
    std::printf("volcados los 8 primeros fotogramas a %s\n", argv[7]);
  }

  // Eight frames spread across the movie, the first one included. The first two catch a decoder that ignores its
  // input; the spread catches a frame index that is right at the start and wrong further in.
  //
  // Neither "the frame has content" nor "two frames differ" holds on its own: every one of these movies fades in from
  // black - the first frame of `movie_eastern.bik` is five values of it - and a legal notice can hold still. What
  // must hold is that the decoder produces a picture somewhere and that it answers to what it was given.
  std::vector<uint32_t> muestras;
  for (int i = 0; i < 8; ++i) {
    muestras.push_back(uint32_t((uint64_t(i) * fotogramas) / 8));
  }
  std::vector<Luma> lumas;
  for (uint32_t i : muestras) {
    Fotograma f;
    assert(DecodificarFotograma(desc, datos, indice, pistas_audio, i, f) &&
           "no se pudo descodificar uno de los fotogramas de la muestra");
    assert(f.ancho == int(ancho) && f.alto == int(alto) && "un fotograma descodificado no mide lo que dice");
    assert(f.pasos[0] > 0 && f.pasos[1] > 0 && "un fotograma no trae los pasos de sus planos");
    lumas.push_back(CopiarLuma(f));
  }

  size_t maximo = 0;
  size_t iguales = 0;
  for (size_t i = 0; i < lumas.size(); ++i) {
    maximo = std::max(maximo, lumas[i].Valores());
    if (i && DiferenciaLuma(lumas[0], lumas[i]) == 0) {
      ++iguales;
    }
  }
  assert(maximo > 2 && "ningun fotograma de la muestra tiene imagen: no se descodifico nada");
  assert(iguales < lumas.size() - 1 && "todos los fotogramas de la muestra salen identicos al primero");

  // The audio really is in the frame: if the split found nothing to skip, the audio lengths are being read wrong.
  const FotogramaIndice& f0 = indice[0];
  size_t inicio_video = 0;
  size_t bytes_video = 0;
  assert(SepararAudioBink(datos.data() + f0.inicio, f0.bytes, pistas_audio, inicio_video, bytes_video) &&
         "no se pudo separar el audio del primer fotograma");
  if (pistas_audio) {
    assert(inicio_video > 0 && bytes_video < f0.bytes && "el primer fotograma no trae audio que separar");
  } else {
    assert(inicio_video == 0 && bytes_video == f0.bytes && "un fichero sin audio no deberia tener nada que separar");
  }

  std::printf("%s: %ux%u, %u fotogramas, %u pista(s) de audio, version '%c', paso Y %d, video del primer fotograma "
              "%zu de %u bytes, valores de luma por muestra:",
              ruta.c_str(), cab.ancho, cab.alto, cab.fotogramas, cab.pistas_audio, char(cab.version()),
              lumas[0].ancho ? int(lumas[0].ancho) : 0, bytes_video, f0.bytes);
  for (size_t i = 0; i < lumas.size(); ++i) {
    std::printf(" %zu@%u", lumas[i].Valores(), muestras[i]);
  }
  std::printf(" (identicos al primero: %zu de %zu)\n", iguales, lumas.size());
  return 0;
}
