// cod2 - PC build: PC-only start-up diagnostics (see cod2_pc_simbolos.cpp).
#pragma once

namespace cod2::pc_simbolos {
#if defined(_WIN32)
void Escribir();
#else
inline void Escribir() {}
#endif
}  // namespace cod2::pc_simbolos