// cod2 - Bink cutscenes decoded natively with FFmpeg (see cod2_video_bink.h)
//
// The container, as FFmpeg's own demuxer reads it (`sdk/thirdparty/FFmpeg/libavformat/bink.c`, `read_header`), is
// little-endian throughout:
//
//   +0x00  'BIK' plus the version letter, one little-endian dword ('BIKi' reads as 0x694B4942)
//   +0x04  file size minus 8
//   +0x08  frame count
//   +0x0C  the largest a compressed frame can be
//   +0x10  four bytes to skip
//   +0x14  width          +0x18  height
//   +0x1C  fps numerator  +0x20  fps denominator
//   +0x24  four bytes of decoder extradata (bit 0x10 of them is the alpha flag)
//   +0x28  audio track count
//   then   four bytes to skip, but only for a 'k' revision
//   then   if there are audio tracks: four bytes per track, then a 16-bit sample rate and a 16-bit flags word per
//          track, then a 32-bit id per track
//   then   the frame index: one 32-bit file offset per frame, bit 0 meaning "keyframe"
//   then   the frames themselves; the first index entry points at the first one
//
// The decoder needs two of those fields before it is opened, not after: it takes its version from the top byte of
// codec_tag and its alpha flag from the extradata (`decode_init`, bink.c). Setting either one late gives a decoder
// that opens without complaint and then returns no frame.

#include "cod2_video_bink.h"

#include <cstring>
#include <span>

// The self-check (app/src/cod2_video_bink_prueba.cpp, built by tools/comprobar_bink.ps1) compiles this file with
// COD2_BINK_SIN_SDK, which leaves out everything that needs the runtime. What remains - the container parser and the
// FFmpeg decoder - is what the check exercises, and it is the part that can be wrong silently.
#ifdef COD2_BINK_SIN_SDK
#define REXLOG_WARN(...) ((void)0)
#define REXLOG_ERROR(...) ((void)0)
#define REXLOG_INFO(...) ((void)0)
#else
#include <rex/filesystem/file.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#endif

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavutil/mem.h"
}

namespace cod2::video_bink {
namespace {

uint32_t Le32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

// Enough for the header and the whole audio table: 44 bytes plus four 32-bit fields per track, and FFmpeg allows at
// most 32 tracks. The frame index is read separately, because it is as long as the movie is.
constexpr size_t kBytesCabecera = 44 + 32 * 4 * 3 + 16;

}  // namespace

bool LeerCabeceraBink(const uint8_t* datos, size_t bytes, CabeceraBink& fuera) {
  if (bytes < 44) {
    REXLOG_WARN("[video] Bink nativo: solo {} bytes, no cabe la cabecera", bytes);
    return false;
  }
  const uint32_t firma = Le32(datos + 0x00);
  if ((firma & 0xFFFFFF) != 0x4B4942 && (firma & 0xFFFFFF) != 0x32424B) {  // 'BIK' or 'KB2'
    REXLOG_WARN("[video] Bink nativo: la firma {:08X} no es BIK ni KB2", firma);
    return false;
  }
  fuera.firma = firma;
  fuera.tamano_fichero = Le32(datos + 0x04) + 8;
  fuera.fotogramas = Le32(datos + 0x08);
  fuera.mayor_fotograma = Le32(datos + 0x0C);
  fuera.ancho = Le32(datos + 0x14);
  fuera.alto = Le32(datos + 0x18);
  fuera.fps_num = Le32(datos + 0x1C);
  fuera.fps_den = Le32(datos + 0x20);
  fuera.extra = Le32(datos + 0x24);
  fuera.pistas_audio = Le32(datos + 0x28);

  if (fuera.fotogramas == 0 || fuera.fotogramas > 1000000) {
    REXLOG_WARN("[video] Bink nativo: {} fotogramas no es un numero creible", fuera.fotogramas);
    return false;
  }
  if (fuera.ancho == 0 || fuera.alto == 0 || fuera.fps_num == 0 || fuera.fps_den == 0) {
    REXLOG_WARN("[video] Bink nativo: cabecera imposible ({}x{}, fps {}/{})", fuera.ancho, fuera.alto, fuera.fps_num,
                fuera.fps_den);
    return false;
  }
  if (fuera.mayor_fotograma == 0 || fuera.mayor_fotograma > fuera.tamano_fichero) {
    REXLOG_WARN("[video] Bink nativo: el mayor fotograma ({} bytes) no cabe en el fichero ({} bytes)",
                fuera.mayor_fotograma, fuera.tamano_fichero);
    return false;
  }

  size_t p = 44;
  // The 'k' revision added four bytes after the audio track count; FFmpeg skips them for 'BIK'k and for KB2 i/j/k.
  const uint32_t version = firma >> 24;
  if ((firma & 0xFFFFFF) == 0x4B4942 ? version == 'k' : (version == 'i' || version == 'j' || version == 'k')) {
    p += 4;
  }
  if (fuera.pistas_audio > 32) {
    REXLOG_WARN("[video] Bink nativo: {} pistas de audio no es un numero creible", fuera.pistas_audio);
    return false;
  }
  p += size_t(fuera.pistas_audio) * 4 * 3;
  if (p + 4 > bytes) {
    REXLOG_WARN("[video] Bink nativo: la cabecera ocupa {} bytes y solo se leyeron {}", p + 4, bytes);
    return false;
  }
  fuera.inicio_indice = p;
  fuera.inicio_datos = uint64_t(Le32(datos + p)) & ~uint64_t(1);
  return true;
}

bool LeerIndiceBink(const uint8_t* datos, size_t bytes, const CabeceraBink& cab, std::vector<FotogramaIndice>& fuera) {
  if (bytes < size_t(cab.fotogramas) * 4) {
    REXLOG_WARN("[video] Bink nativo: el indice ocupa {} bytes y hay {}", size_t(cab.fotogramas) * 4, bytes);
    return false;
  }
  fuera.clear();
  fuera.reserve(cab.fotogramas);
  uint64_t pos = uint64_t(Le32(datos)) & ~uint64_t(1);
  for (uint32_t i = 0; i < cab.fotogramas; ++i) {
    uint64_t siguiente;
    bool clave;
    if (i + 1 == cab.fotogramas) {
      // FFmpeg does the same: the last frame runs to the end of the file and is never a keyframe.
      siguiente = cab.tamano_fichero;
      clave = false;
    } else {
      siguiente = uint64_t(Le32(datos + size_t(i + 1) * 4)) & ~uint64_t(1);
      // The keyframe bit is stored in the entry for frame i itself, and FFmpeg reads it that way - but not for the
      // first frame, whose bit is not set and which is a keyframe by definition (the index has to start somewhere).
      clave = i == 0 || (Le32(datos + size_t(i) * 4) & 1) != 0;
    }
    if (siguiente <= pos || siguiente > cab.tamano_fichero) {
      REXLOG_WARN("[video] Bink nativo: indice roto en el fotograma {} ({} -> {}, fichero {})", i, pos, siguiente,
                  cab.tamano_fichero);
      return false;
    }
    fuera.push_back({pos, uint32_t(siguiente - pos), clave});
    pos = siguiente;
  }
  return true;
}

// --- DescodificadorBink -----------------------------------------------------------------------------------------

bool SepararAudioBink(const uint8_t* datos, size_t bytes, uint32_t pistas_audio, size_t& inicio_video,
                      size_t& bytes_video) {
  size_t p = 0;
  size_t restante = bytes;
  for (uint32_t pista = 0; pista < pistas_audio; ++pista) {
    if (restante < 4) {
      REXLOG_WARN("[video] Bink nativo: la pista {} no cabe en los {} bytes del fotograma", pista, bytes);
      return false;
    }
    const uint32_t audio = Le32(datos + p);
    if (audio > restante - 4) {
      REXLOG_WARN("[video] Bink nativo: la pista {} dice ocupar {} bytes y al fotograma le quedan {}", pista, audio,
                  restante - 4);
      return false;
    }
    p += 4 + size_t(audio);
    restante -= 4 + size_t(audio);
  }
  inicio_video = p;
  bytes_video = restante;
  return restante > 0;
}
struct DescodificadorBink::Estado {
  AVCodecContext* codec = nullptr;
  AVPacket* paquete = nullptr;
  AVFrame* cuadro = nullptr;

  ~Estado() {
    if (cuadro) {
      av_frame_free(&cuadro);
    }
    if (paquete) {
      av_packet_free(&paquete);
    }
    if (codec) {
      avcodec_free_context(&codec);
    }
  }
};

DescodificadorBink::DescodificadorBink() : e_(std::make_unique<Estado>()) {}
DescodificadorBink::~DescodificadorBink() = default;

bool DescodificadorBink::Abrir(const CabeceraBink& cab) {
  const AVCodec* bink = avcodec_find_decoder(AV_CODEC_ID_BINKVIDEO);
  if (!bink) {
    // Registered by sdk/thirdparty/ffmpeg-overlay/codec_list.c, kept enabled by tools/comprobar_ffmpeg.py. Without
    // it the port falls back to the guest's own decoder and the cutscenes stay at 0.46 frames per second, with no
    // error anywhere to say why.
    REXLOG_ERROR("[video] Bink nativo: FFmpeg no trae el descodificador BINKVIDEO (revisa codec_list.c)");
    return false;
  }
  e_ = std::make_unique<Estado>();
  e_->codec = avcodec_alloc_context3(bink);
  if (!e_->codec) {
    return false;
  }
  e_->codec->width = e_->codec->coded_width = int(cab.ancho);
  e_->codec->height = e_->codec->coded_height = int(cab.alto);
  e_->codec->thread_count = 1;
  // Both of these have to be in place before avcodec_open2: decode_init reads the version out of codec_tag and the
  // alpha flag out of the extradata.
  e_->codec->codec_tag = cab.firma;
  e_->codec->extradata = static_cast<uint8_t*>(av_mallocz(4 + AV_INPUT_BUFFER_PADDING_SIZE));
  std::memcpy(e_->codec->extradata, &cab.extra, 4);
  e_->codec->extradata_size = 4;
  if (avcodec_open2(e_->codec, bink, nullptr) < 0) {
    REXLOG_ERROR("[video] Bink nativo: avcodec_open2 fallo ({}x{}, version '{}')", cab.ancho, cab.alto,
                 char(cab.version()));
    e_ = std::make_unique<Estado>();
    return false;
  }
  e_->paquete = av_packet_alloc();
  e_->cuadro = av_frame_alloc();
  if (!e_->paquete || !e_->cuadro) {
    REXLOG_ERROR("[video] Bink nativo: sin memoria para el paquete o el cuadro");
    e_ = std::make_unique<Estado>();
    return false;
  }
  return true;
}

void DescodificadorBink::Cerrar() { e_ = std::make_unique<Estado>(); }

bool DescodificadorBink::Descodificar(const uint8_t* datos, size_t bytes, bool clave, Fotograma& salida) {
  if (!e_->codec || !e_->paquete || !e_->cuadro || !datos || !bytes) {
    return false;
  }
  av_packet_unref(e_->paquete);
  if (av_new_packet(e_->paquete, int(bytes)) < 0) {
    return false;
  }
  std::memcpy(e_->paquete->data, datos, bytes);
  e_->paquete->flags = clave ? AV_PKT_FLAG_KEY : 0;
  int r = avcodec_send_packet(e_->codec, e_->paquete);
  if (r < 0) {
    REXLOG_WARN("[video] Bink nativo: avcodec_send_packet {} ({} bytes)", r, bytes);
    return false;
  }
  r = avcodec_receive_frame(e_->codec, e_->cuadro);
  if (r < 0) {
    REXLOG_WARN("[video] Bink nativo: avcodec_receive_frame {} ({} bytes)", r, bytes);
    return false;
  }
  for (int i = 0; i < 3; ++i) {
    salida.planos[i] = e_->cuadro->data[i];
    salida.pasos[i] = e_->cuadro->linesize[i];
  }
  salida.ancho = e_->cuadro->width;
  salida.alto = e_->cuadro->height;
  salida.clave = clave;
  return true;
}

#ifndef COD2_BINK_SIN_SDK

// --- PeliculaBink -----------------------------------------------------------------------------------------------

namespace {

// A file opened through the SDK's VFS, so a game path resolves the same way whether the data is a folder or a
// mounted image. Copied from the parked WMV3 path (app/src/aparcado/cod2_video_wmv3.cpp), which is not compiled.
struct FicheroJuego {
  rex::filesystem::File* f = nullptr;
  uint64_t tamano = 0;

  ~FicheroJuego() {
    if (f) {
      f->Destroy();
    }
  }

  bool Abrir(const std::string& ruta) {
    rex::filesystem::FileAction accion;
    const auto estado = REX_KERNEL_FS()->OpenFile(nullptr, ruta, rex::filesystem::FileDisposition::kOpen,
                                                  rex::filesystem::FileAccess::kGenericRead, false, true, &f, &accion);
    if (estado != 0 || !f) {
      REXLOG_WARN("[video] Bink nativo: no se puede abrir '{}' ({:08X})", ruta, uint32_t(estado));
      f = nullptr;
      return false;
    }
    tamano = f->entry()->size();
    return true;
  }

  bool Leer(uint64_t desplazamiento, uint8_t* destino, size_t bytes) {
    if (!f) {
      return false;
    }
    size_t leidos = 0;
    const auto estado = f->ReadSync(std::span<uint8_t>(destino, bytes), size_t(desplazamiento), &leidos);
    return estado == 0 && leidos == bytes;
  }
};

}  // namespace

struct PeliculaBink::Estado {
  FicheroJuego fichero;
  CabeceraBink cab;
  std::vector<FotogramaIndice> indice;
  DescodificadorBink descodificador;
  std::vector<uint8_t> comprimido;
  uint32_t siguiente = 0;
  bool abierta = false;

  bool Decodificar(uint32_t i, Fotograma& salida) {
    if (i >= indice.size()) {
      return false;
    }
    const FotogramaIndice& f = indice[i];
    if (f.bytes > comprimido.size()) {
      REXLOG_WARN("[video] Bink nativo: el fotograma {} ocupa {} bytes y el mayor declarado es {}", i, f.bytes,
                  cab.mayor_fotograma);
      return false;
    }
    if (!fichero.Leer(f.inicio, comprimido.data(), f.bytes)) {
      REXLOG_WARN("[video] Bink nativo: no se pudo leer el fotograma {} ({} bytes en {})", i, f.bytes, f.inicio);
      return false;
    }
    // The frame's audio comes first, one length and one blob per track; only what is left is picture.
    size_t inicio_video = 0;
    size_t bytes_video = 0;
    if (!SepararAudioBink(comprimido.data(), f.bytes, cab.pistas_audio, inicio_video, bytes_video)) {
      REXLOG_WARN("[video] Bink nativo: el fotograma {} no tiene datos de video", i);
      return false;
    }
    return descodificador.Descodificar(comprimido.data() + inicio_video, bytes_video, f.clave, salida);
  }
};

PeliculaBink::PeliculaBink() : e_(std::make_unique<Estado>()) {}
PeliculaBink::~PeliculaBink() = default;

bool PeliculaBink::Abrir(const std::string& ruta) {
  e_ = std::make_unique<Estado>();
  if (!e_->fichero.Abrir(ruta)) {
    return false;
  }
  std::vector<uint8_t> cabecera(std::min<uint64_t>(e_->fichero.tamano, kBytesCabecera));
  if (!e_->fichero.Leer(0, cabecera.data(), cabecera.size()) ||
      !LeerCabeceraBink(cabecera.data(), cabecera.size(), e_->cab)) {
    return false;
  }
  e_->comprimido.resize(size_t(e_->cab.mayor_fotograma) + 4096);
  std::vector<uint8_t> indice(size_t(e_->cab.fotogramas) * 4);
  if (!e_->fichero.Leer(e_->cab.inicio_indice, indice.data(), indice.size()) ||
      !LeerIndiceBink(indice.data(), indice.size(), e_->cab, e_->indice)) {
    return false;
  }
  if (!e_->descodificador.Abrir(e_->cab)) {
    return false;
  }
  e_->siguiente = 0;
  e_->abierta = true;
  REXLOG_INFO("[video] Bink nativo: '{}' version '{}', {}x{}, {} fotogramas, fps {}/{}", ruta, char(e_->cab.version()),
              e_->cab.ancho, e_->cab.alto, e_->cab.fotogramas, e_->cab.fps_num, e_->cab.fps_den);
  return true;
}

bool PeliculaBink::Siguiente(Fotograma& salida) {
  if (!e_->abierta) {
    return false;
  }
  if (!e_->Decodificar(e_->siguiente, salida)) {
    return false;
  }
  ++e_->siguiente;
  return true;
}

bool PeliculaBink::IrA(uint32_t fotograma, Fotograma& salida) {
  if (!e_->abierta || fotograma >= e_->indice.size()) {
    return false;
  }
  // Not every Bink frame stands on its own, so a seek has to decode forward from the nearest keyframe.
  uint32_t desde = fotograma;
  while (desde > 0 && !e_->indice[desde].clave) {
    --desde;
  }
  for (uint32_t i = desde; i < fotograma; ++i) {
    Fotograma descartado;
    if (!e_->Decodificar(i, descartado)) {
      return false;
    }
  }
  if (!e_->Decodificar(fotograma, salida)) {
    return false;
  }
  e_->siguiente = fotograma + 1;
  return true;
}

bool PeliculaBink::abierta() const { return e_->abierta; }
uint32_t PeliculaBink::fotogramas() const { return e_->cab.fotogramas; }
uint32_t PeliculaBink::indice_actual() const { return e_->siguiente; }
int PeliculaBink::ancho() const { return int(e_->cab.ancho); }
int PeliculaBink::alto() const { return int(e_->cab.alto); }
uint32_t PeliculaBink::fps_num() const { return e_->cab.fps_num; }
uint32_t PeliculaBink::fps_den() const { return e_->cab.fps_den; }

#endif  // COD2_BINK_SIN_SDK

}  // namespace cod2::video_bink
