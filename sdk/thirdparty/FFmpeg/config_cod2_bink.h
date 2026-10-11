/*
 * COD2: Bink video decoder for the game's cutscenes.
 *
 * Call of Duty 2's cutscenes are Bink (.bik), not the WMV3 the previous port handled, and the guest's own
 * statically linked Bink player runs at 0.46 frames per second on the Switch. The port decodes them with
 * FFmpeg instead, from the same compressed bytes the guest asks for, so the Bink decoder is needed here.
 *
 * bink_decoder_select is "blockdsp hpeldsp" (configure, FFmpeg 4.4), and libavcodec/Makefile builds
 * bink.o together with binkdsp.o under CONFIG_BINK_DECODER, so those two objects come with it. blockdsp
 * and hpeldsp are set here too even though config_cod2_wmv3.h already sets them: this file must stay
 * correct on its own, and a duplicated #undef/#define pair is harmless.
 *
 * Only the decoder is enabled. There is no libavformat in this build (see the source list in
 * sdk/thirdparty/CMakeLists.txt), so the Bink demuxer cannot be used and the compressed frames have to be
 * handed to the decoder as packets, exactly as the WMV3 path does.
 */
#undef CONFIG_BINK_DECODER
#define CONFIG_BINK_DECODER 1
#undef CONFIG_BLOCKDSP
#define CONFIG_BLOCKDSP 1
#undef CONFIG_HPELDSP
#define CONFIG_HPELDSP 1
