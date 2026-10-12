#pragma once
/*
 * Periodic log lines, off the working threads.
 *
 * The log is synchronous (log_async = false): every few lines the FILE is flushed to the SD card and the
 * writing thread stalls for several ms. Every 10 s the game thread used to write 16 lines, the audio thread 3
 * and the ring thread 7 (the C2 lines of cod2_nativo_destinos.cpp) on their own. These lines are formatted
 * on the thread that produces them and written by the "COD2 informes" thread (InformeDiferido,
 * cod2_nativo_sistema.cpp). Warnings and errors are still written immediately, from their own thread.
 */
#include <string>

#include <rex/logging.h>

namespace cod2::nativo {
void InformeDiferido(std::string linea);
}  // namespace cod2::nativo

#define COD2_INFORME_DIFERIDO(...) ::cod2::nativo::InformeDiferido(fmt::format(__VA_ARGS__))
