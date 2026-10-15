// cod2 - self-check for the vertex-upload deduplication table (cod2_nativo_vertices_dedupe.h).
//
// WHY IT EXISTS. The table is the only place in the port that can hand a draw bytes it did not copy, and the
// whole visible artefact search (large flat triangles while the player or an NPC moves) ends up asking
// whether a hit is exact. The exactness rests on three mechanisms that are easy to break silently while
// editing: the generation (Olvidar), the frame (NuevoFotograma) and the paranoid fingerprint. None of them
// shows up as a compile error if it stops working, and none of them shows up in the log either - the table
// simply serves bytes it should not and the picture is wrong for a few frames.
//
// HOW TO RUN IT. Header-only and dependency-free, so it compiles on its own:
//
//     clang++ -std=c++20 -O1 -I app/src tools/pc/probar_dedupe_vertices.cpp -o out/dedupe && out/dedupe
//
// It decides nothing about the game; it checks that a hit happens when it should and does NOT happen when the
// guest may have rewritten the range.

#include "cod2_nativo_vertices_dedupe.h"

#include <cstdio>
#include <string>

using cod2::nativo::DedupeVertices;

namespace {

int fallos = 0;

void Exigir(bool valor, const std::string& que) {
  if (!valor) {
    ++fallos;
    std::printf("FALLO: %s\n", que.c_str());
  } else {
    std::printf("  ok: %s\n", que.c_str());
  }
}

}  // namespace

int main() {
  DedupeVertices d;
  uint64_t offset = 0;

  // 1. The same range twice in the same frame is a hit, and it is the offset that was recorded.
  d.NuevoFotograma(1);
  Exigir(!d.Buscar(0x1000, 256, 0, offset), "una direccion desconocida no acierta");
  d.Anotar(0x1000, 256, 0, 0x4000);
  Exigir(d.Buscar(0x1000, 256, 0, offset), "la misma direccion y tamano acierta");
  Exigir(offset == 0x4000, "el acierto devuelve el offset que se anoto");
  Exigir(d.aciertos() == 1 && d.bytes_ahorrados() == 256,
         "los contadores cuentan el acierto y sus bytes");

  // 2. The byte order and the size are part of the key: a different order or size is not the same copy.
  Exigir(!d.Buscar(0x1000, 256, 1, offset), "el mismo rango con otro orden no acierta");
  Exigir(!d.Buscar(0x1000, 512, 0, offset), "el mismo rango con otro tamano no acierta");

  // 3. Olvidar() invalidates everything: it is the guest having waited for the GPU, so it may have rewritten.
  d.Olvidar();
  Exigir(!d.Buscar(0x1000, 256, 0, offset), "tras Olvidar() el mismo rango ya no acierta");
  Exigir(d.olvidos() == 1, "Olvidar() se cuenta");

  // 4. A new frame invalidates everything too (the upload buffer was reset, the offsets are gone).
  d.NuevoFotograma(2);
  d.Anotar(0x2000, 128, 0, 0x8000);
  Exigir(d.Buscar(0x2000, 128, 0, offset), "en el fotograma nuevo acierta lo del fotograma nuevo");
  d.NuevoFotograma(3);
  Exigir(!d.Buscar(0x2000, 128, 0, offset), "en el fotograma siguiente ya no acierta");

  // 5. The paranoid mode: the fingerprint recorded with the copy is compared on lookup, and a mismatch is a
  //    miss (so the caller copies and the frame is right) whose details are handed back for the log.
  d.NuevoFotograma(4);
  d.Anotar(0x3000, 64, 0, 0xC000, 0x1111);
  Exigir(d.Buscar(0x3000, 64, 0, offset, 0x1111), "con la huella que se guardo, acierta");
  DedupeVertices::Choque choque;
  Exigir(!d.Buscar(0x3000, 64, 0, offset, 0x2222, &choque),
         "con otra huella NO acierta: el rango cambio y hay que copiarlo");
  Exigir(choque.direccion == 0x3000 && choque.bytes == 64 && choque.offset == 0xC000 &&
             choque.guardada == 0x1111 && choque.vista == 0x2222,
         "el choque lleva direccion, tamano, offset y las dos huellas");
  Exigir(d.discrepancias() == 1, "la discrepancia se cuenta aparte de los aciertos");
  Exigir(d.aciertos() == 3, "y no suma como acierto");

  // 6. Without a fingerprint (huella 0) the table must not consult the stored one: that is the normal path,
  //    and it has to stay free of the paranoid check.
  d.NuevoFotograma(5);
  d.Anotar(0x4000, 32, 0, 0x10000, 0x3333);
  Exigir(d.Buscar(0x4000, 32, 0, offset, 0), "huella 0 no comprueba contenido (camino normal)");
  Exigir(d.discrepancias() == 1, "y no inventa una discrepancia");

  std::printf(fallos ? "\n%d FALLOS\n" : "\n%d fallos: todo bien\n", fallos);
  return fallos ? 1 : 0;
}
