// cod2 - Bink cutscenes decoded natively with FFmpeg (see cod2_video_bink.cpp)
//
// The game's cutscenes are Bink 1, version 'i' (`42 49 4B 69`), 1280x720 at 30 frames per second, and the game
// decodes them with its own recompiled Bink library (`sub_82497D90` = `BinkDoFrame`, `sub_8249DA50` = one plane).
// This is the native replacement for that library.
//
// Three pieces, deliberately separate:
//
//   * `LeerCabeceraBink` parses the container from a plain buffer. It touches no file and no decoder, so it can be
//     checked on its own against the real `.bik` files (app/src/cod2_video_bink_prueba.cpp).
//   * `DescodificadorBink` drives FFmpeg. It is handed the compressed bytes of one frame and returns YUV420P planes.
//   * `PeliculaBink` joins the two to the game's own file system and is what the hook talks to.
//
// The container format is FFmpeg's own reading of it (`sdk/thirdparty/FFmpeg/libavformat/bink.c`, `read_header`),
// not a guess: little-endian throughout, and every field below is at the offset that file reads it from.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace cod2::video_bink {

// The Bink 1 header, up to and including the start of the frame index.
struct CabeceraBink {
  uint32_t firma = 0;            // the whole little-endian dword at +0: 'BIK' plus the version letter
  uint32_t tamano_fichero = 0;   // +4, plus 8 (that is how FFmpeg reads it)
  uint32_t fotogramas = 0;       // +8
  uint32_t mayor_fotograma = 0;  // +0xC, the largest a compressed frame can be
  uint32_t ancho = 0;            // +0x14
  uint32_t alto = 0;             // +0x18
  uint32_t fps_num = 0;          // +0x1C
  uint32_t fps_den = 0;          // +0x20
  uint32_t extra = 0;            // +0x24, the four bytes the decoder takes as extradata
  uint32_t pistas_audio = 0;     // +0x28
  uint64_t inicio_indice = 0;    // where the frame index starts
  uint64_t inicio_datos = 0;     // where the first frame starts

  uint8_t version() const { return uint8_t(firma >> 24); }
};

// `datos` is the start of the file. Needs enough of it to cover the header and, when the file has audio tracks, the
// audio table that follows; the frame index is read separately, by `LeerIndiceBink`.
bool LeerCabeceraBink(const uint8_t* datos, size_t bytes, CabeceraBink& fuera);

// One entry per frame: the file offset where it starts, and whether it is a keyframe (bit 0 of the stored value).
struct FotogramaIndice {
  uint64_t inicio = 0;
  uint32_t bytes = 0;
  bool clave = false;
};

// `datos` must start at `CabeceraBink::inicio_indice` and hold `cab.fotogramas * 4` bytes.
bool LeerIndiceBink(const uint8_t* datos, size_t bytes, const CabeceraBink& cab, std::vector<FotogramaIndice>& fuera);

// Splits one frame's bytes into its audio and its video.
//
// A frame is not all picture: when the file has audio tracks, each frame begins with one 32-bit length per track,
// each followed by that many bytes of audio, and the video packet is what is left. So the video does not start at
// `FotogramaIndice::inicio` and is not `FotogramaIndice::bytes` long, and a decoder handed the whole region reads
// audio as picture - it reports "DC value went out of bounds" and returns no frame.
//
// `datos` is the frame region as the index describes it. On success `inicio_video` and `bytes_video` are offsets into
// it. This is FFmpeg's own arithmetic (`libavformat/bink.c`, `read_packet`), including the rule that a track with
// fewer than four bytes of audio is skipped rather than returned.
bool SepararAudioBink(const uint8_t* datos, size_t bytes, uint32_t pistas_audio, size_t& inicio_video,
                      size_t& bytes_video);

// One decoded frame: three planes of 8-bit YUV 4:2:0, and the stride of each.
//
// The planes belong to the decoder, not to the caller: they stay valid only until the next `Descodificar`. Copy
// whatever has to outlive that call.
struct Fotograma {
  const uint8_t* planos[3] = {nullptr, nullptr, nullptr};
  int pasos[3] = {0, 0, 0};
  int ancho = 0;
  int alto = 0;
  bool clave = false;
};

// FFmpeg's Bink decoder, driven one frame at a time. No file access: the caller hands it bytes.
class DescodificadorBink {
 public:
  DescodificadorBink();
  ~DescodificadorBink();
  DescodificadorBink(const DescodificadorBink&) = delete;
  DescodificadorBink& operator=(const DescodificadorBink&) = delete;

  // `cab` supplies the version (from `firma`), the size and the extradata the decoder needs to be opened.
  bool Abrir(const CabeceraBink& cab);
  bool Descodificar(const uint8_t* datos, size_t bytes, bool clave, Fotograma& salida);
  void Cerrar();

 private:
  struct Estado;
  std::unique_ptr<Estado> e_;
};

// A `.bik` file opened through the game's own file system, decoded frame by frame.
//
// The frame index is read once, at open; each frame's compressed bytes are read when it is decoded. That matters:
// `cod2_intro.bik` is 182 MB and `cod2_attract.bik` is 191 MB, and the console's process is already at 99.9 % of its
// memory limit, so holding a whole movie in memory is not an option.
class PeliculaBink {
 public:
  PeliculaBink();
  ~PeliculaBink();
  PeliculaBink(const PeliculaBink&) = delete;
  PeliculaBink& operator=(const PeliculaBink&) = delete;

  // `ruta` is a game path, as the game itself would write it (`movies/movie_africa` or `movie_africa.bik`).
  bool Abrir(const std::string& ruta);

  // Decodes the next frame. False at the end of the movie, or if the frame could not be read or decoded.
  bool Siguiente(Fotograma& salida);

  // Decodes the frame at an absolute index, which is what seeking needs.
  bool IrA(uint32_t fotograma, Fotograma& salida);

  bool abierta() const;
  uint32_t fotogramas() const;
  uint32_t indice_actual() const;
  int ancho() const;
  int alto() const;
  uint32_t fps_num() const;
  uint32_t fps_den() const;

 private:
  struct Estado;
  std::unique_ptr<Estado> e_;
};

}  // namespace cod2::video_bink
