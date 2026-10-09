// cod2 - cinematicas WMV3 descodificadas con FFmpeg (ver cod2_video_nativo.cpp)

#pragma once

#include <cstdint>

namespace cod2::video_nativo {

// Movie frames decoded with FFmpeg since startup. The hang watchdog (cod2_app.h) mixes it into its
// signature: during a cutscene the game threads spend almost all their time waiting in the same place
// and their registers do not change even though the video advances.
uint64_t FotogramasNativos();

}  // namespace cod2::video_nativo
