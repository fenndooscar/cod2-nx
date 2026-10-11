# Audio and video on the Switch

This page explains how the game's sound and its cutscene videos were made to work on the Switch. Read it if a port
has gaps in the sound, a "robotic" sound, sound that stops, or slow videos. Audio problems showed up before frame
rate problems and took the longest to close.

Almost everything here is in the audio path of [ReXGlue](glossary.md#rexglue) or in the game's video player, so most
of it applies to other ports too. The audio path has three parts:

- **XAudio**, the Xbox 360's sound mixing library, which the game uses to play its sounds;
- **[XMA](glossary.md#xma) decoding**: XMA is the Xbox 360's compressed audio format, and the runtime decodes it with
  FFmpeg, an open source audio and video library;
- **the Switch output driver**, which sends the mixed sound to the console's audio output.

The settings named on this page go in `cod2.toml`, next to the NRO (see [cod2.toml](glossary.md#cod2toml)).

## In short

- The output driver needed a steady "pump" that asks the game for sound at a fixed rate, and a small queue of mixed
  sound.
- The "robotic" sound was the game's audio server thread arriving late. It took three fixes: a higher priority for
  that thread, native versions of the hottest audio functions, and XMA decoding moved off the game's thread.
- An intermittent hang of the audio server was fixed with a timeout that also retries.
- Cutscenes are decoded with FFmpeg instead of the game's own WMV3 decoder, which was far too slow. This includes
  videos re-encoded by fan translations, which the game's decoder cannot play.
- FFmpeg no longer rebuilds the same math tables for every new sound.

## Output driver

The output driver is `switch_audio_system.cpp`, in the SDK (`sdk/src/audio/switch/`). It opens audout (the Switch's
audio output service) at 48 kHz, stereo, 16-bit, with three 1,024-sample buffers. Two things were needed on top of
what it did at first:

- **A pump at 187.5 Hz.** It asks the game for one frame of mixed sound at a steady rate, 187.5 times per second.
  Without it, the driver asked the game for four frames in a row and then nothing, which starved the game's voice (its
  stream of sound, see below) between bursts. The setting `audio_switch_bomba` turns the pump on; it is on by default.
- **A small queue of mixed frames**: 10 frames, 53 ms (setting `audio_switch_tramas_en_cola`). It absorbs short
  delays of the game's audio thread without adding a delay you can hear.

The game mixes six channels, and the driver folds them into two (the *downmix*): 0.586 for the front channels, 0.414
for the centre and rear, nothing from the LFE (subwoofer) channel, and hard clipping (values above the maximum are
cut off). Measured on HDMI captures, the mix peaks at 0.93-0.95 and never clips.

## "Robotic" audio

**Symptom:** a metallic, stuttering sound everywhere. It was worse in crashes and in busy areas, and it did not
depend on the frame rate.

**Cause:** the game has an *audio server thread* that prepares its sound in packets of 256 samples. It keeps a ring
of two packets (10.7 ms) for its XAudio *voice* (the stream of sound it plays through XAudio), and each mixed frame
uses one packet. When the server is late, the frame goes out with that voice silent. The result is 5.33 ms gaps
between bits of sound. No sound is lost: silence is inserted.

It took three separate fixes. Here they are from the one that helped most to the one that helped least.

### 1. A higher priority for the audio server

The server was a [guest](glossary.md#guest) thread at priority 0x3B. It took turns, in 10 ms slices, with the main
game thread and another busy guest thread, and the GPU ring thread ran above it (see
[platform-notes.md](platform-notes.md#threads) for how priorities work on the Switch). In crashes it delivered only
89 % of its packets.

- The server now runs at 0x2D (setting `cod2_audio_servidor_prioridad`).
- The audio worker (the thread that mixes each audio frame) may wait up to 30 ms for a late packet, instead of mixing
  that voice as silence (setting `cod2_audio_esperar_servidor_ms`).

Both are in `app/src/cod2_audio_servidor.cpp`. Together they made the problem "barely noticeable".

The SDK's priority table also had a bug. It compared thread names exactly, but thread names get a ` (F80000xx)`
suffix, so the audio worker and the XMA decoder never got the priority meant for them. It now compares with
`starts_with` (in `sdk/src/system/xthread.cpp`).

### 2. Native versions of the hottest audio functions

Two resamplers (they change the sample rate of a sound), a filter, a gain sum (it adds a sound into the mix at a
given volume) and `memset` were rewritten in C++ as [native replacements](glossary.md#native-replacement) of the
recompiled code. The C++ does the same floating-point operations in the same order, so the results are identical:
for example `double(float(std::fma(...)))`, and the PowerPC instruction `fnmsubs` written as `-std::fma(x, y, -z)`.
Over 159 million samples there were zero differences. The server's CPU time dropped by 17.5 %.

The code is in `app/src/`: `cod2_audio_remuestreo.cpp` (resamplers), `cod2_audio_filtro.cpp` (filter),
`cod2_audio_suma.cpp` (gain sum) and `cod2_crt_nativo.cpp` (`memset`).

### 3. XMA decoding off the game's thread

To start decoding a sound, the game calls `XMAEnableContext`. (An XMA *context* is one decoder slot with its sound;
the call writes the context's *kick* register, which tells the decoder to start.) The SDK decoded the context right
there, on the game's audio thread. On the Switch that cost 17.3 % of a core, with peaks of 14.4 ms against a 10.7 ms
buffer, and the GPU ring thread interrupted the decoding halfway.

Nothing in the game calls `WaitForWorkDone()`: the game polls the context (it checks it again and again). So the
decoding does not have to happen inside the kick. Now the kick only marks the context in a bitmap (one bit per
context) and wakes the XMA worker thread, which decodes it. The worker's full sweep over all contexts stays as a
safety net. The code is in `sdk/src/audio/xma_decoder.cpp`. Its setting, `audio_xma_en_trabajador`, chooses which
thread decodes; on the Switch it is the worker by default.

Results on the console:

- the game's audio thread went from 17.3 % to 1.5 % of a core;
- gaps in races went from 43 to 5;
- the data is ready 93 µs after the kick on average (the worst case, 8.3 ms, did not cause a gap);
- as a side effect, the race frame rate went up by 3.5 FPS.

### How it was measured

These tools made the problem measurable:

- counters of empty mixing passes and of delivered packets, every 500 ms;
- the time from the kick to the decoded data;
- a WAV dump of the game's six-channel sound before the driver;
- dumps of each XMA context;
- a gap detector, run over the audio track of videos captured from the console.

## An intermittent hang of the audio server

In about three of seven early runs, the game went completely silent and the race never finished loading.

**Cause:** the audio server thread (the recompiled function `sub_825E3E28` in this game) waits for its event with no
timeout. When it wakes up and its slot is still busy, it goes back to waiting without trying to deliver. So a single
lost end-of-packet notification stops it for good, and with it the game's queue of audio commands.

**Fix:** a timeout on that wait. After 250 ms without progress, the thread retries the delivery and frees the slot.
A timeout alone is not enough: the loop must also retry. The code is in `app/src/cod2_audio_servidor.cpp` (setting
`cod2_audio_rescate`, on by default).

**Lesson:** when a failure is intermittent, one run with working audio does not prove that a build is good.

## Videos

### Call of Duty 2: the cutscenes, and why they are slow

Everything below is the previous port's work and **none of it is compiled for this game**: `cod2_video_nativo.cpp`,
`cod2_video_wmv3.cpp` and the rest of the video path are parked in `app/src/aparcado/`. Call of Duty 2's cutscenes are
Bink (`.bik`), not [WMV3](glossary.md#wmv3), so the game decodes them itself, with its own recompiled decoder.

That decoder is not what makes them slow. Measured on the console, handheld, stock clocks, from the 10-second reports
in `logs/rex/rex_perfil.log`:

| | menu | game | cutscene |
|---|---|---|---|
| frames per second | 26.22 | 3.26 | **0.46** |
| emulated guest reads per second | 8,027 | 299 | **125,434** |
| ms per second waiting for the GPU | 612 | 852 | **0** |

A cutscene draws two quads per frame and never waits for the GPU, so it is not a graphics problem and lowering the
render resolution does not touch it. At 0.46 frames per second a frame takes 2.17 s and carries about 322,000 emulated
reads, against 345,600 pixels in a 720x480 surface - **one emulated read per pixel**.

The cause is the exception path. Horizon does not let a user process make a page read-only, so "watch this memory for
CPU writes" is done by unmapping it, and then reads fault as well - the report header says which of the two paths was
taken: `vigilancia: desmapeo (cada lectura falla)`. The watched range is the video frame the game has just uploaded as
a texture. So 58 % of a cutscene's CPU is the exception path (`svcReturnFromException` 34.2 %, `RexResumeTrap` 7.9 %,
`RexGmQueryProtect` 7.5 %, `RexGmShadowFor` 4.5 %, `RexResumeFromException` 2.6 %, `__libnx_exception_entry` 1.3 %) and
only 8.7 % is the decoder itself (`sub_8249DA50` 5.2 %, `sub_824A2EA0` 3.5 %). The handler is not where the time can be
won: removing both of its locks is worth about 12 %, which would take 0.46 frames per second to 0.52.

**The decoder is now replaced, and that is the whole of the 58 %.** `app/src/cod2_video_bink_nativo.cpp` decodes each
frame with FFmpeg and writes it into the game's own surfaces, so the guest's decoder - and every read it makes of the
previous frame - is out of the path. Measured on the console, same 10-second reports: the cutscenes go from 0.46
frames per second to **28.5-30.9**, and `lecturas emuladas` in the report header from 125,434 per second to **0**. The
read storm *was* the decoder's reference-frame reads, not the game's upload copy.

That leaves the exception path around the surface the game uploads as a texture: still there, but no longer the wall.
At 28.5-30.9 frames per second of 30 the movies run at their own frame rate, and the counter that mattered is zero
rather than merely smaller. If the reads come back, the next step is the one the previous port's bridge already has
the shape of - decode natively, upload that frame as the port's own texture and let the game's draw of the movie quad
find it (`ColaVideo`, `CapturarPlanosVideo`, `AnotarDibujoVideo` in `app/src/aparcado/cod2_video_bridge.h`).

FFmpeg's Bink decoder is now enabled for this build (`config_cod2_bink.h`); it was vendored but on in no configuration
at all. `tools/comprobar_ffmpeg.py` checks that it stays enabled, because a decoder that is not registered makes
`avcodec_find_decoder` return NULL and the port silently falls back to the guest's own decoder.

To confirm the diagnosis without building anything, rename the four campaign cutscenes on the SD card
(`game_root/movie_africa.bik`, `movie_eastern.bik`, `movie_europe.bik`, `movie_victory.bik`): if the game carries on
without them, the cost above disappears with them.

### Call of Duty 2's Bink player: the addresses, and where they came from

None of this is inherited - every address below was re-derived from this game, and the method is written down because
the next one has to be found the same way.

The XEX on disk is compressed, so the strings and tables are not readable from the file: `.bik`, `playMovie` and the
movie names only exist once the runtime has mapped the image. `tools/pc/leer_guest.py --volcar` dumps that image out
of a running `cod2.exe` (`tools/pc/jugar.ps1` starts one), and `tools/refiere_datos.py` then answers "which function
points at this address" by following the `lis`/`addi` pair the compiler emits for every far address. That is how
`0x8205C208` (`.bik`) was found, and it led to the whole module:

| address | what it is | how it is known |
|---|---|---|
| `sub_822D1ED8` | the game's "start a movie" entry, no direct caller (reached through a function pointer) | calls the three below |
| `sub_822D1A30` | opens `<name>.bik`: builds the name, appends the `.bik` at `0x8205C208`, opens the file, then calls the decoder | read from the code |
| `sub_822D12E8`, `sub_822D1648` | the movie object and the blit, both called from `sub_822D1ED8` | read from the code |
| `sub_82497D90` | `BinkDoFrame`: the per-frame entry, frame ring buffer at `+688`..`+700` | read from the code; calls `sub_8249F980` once per frame |
| `sub_8249F980` | the frame driver: flips the double buffer, then one call per plane | read from the code |
| `sub_8249DA50` | decodes one plane; 7,980 bytes, the hottest guest function in a cutscene | read from the code; 3 call sites in `sub_8249F980` for these movies |
| `sub_824A2EA0` | helper of the plane decoder | read from the code |

The files are **Bink 1, version `i`** (`42 49 4B 69`), 1280x720, 30 frames per second - read from the `.bik` headers
themselves. FFmpeg 4.4's Bink decoder covers `b` through `k`, and takes the version from the `codec_tag`, so version
`i` is supported.

The plane state lives in the decoder context in 48-byte records indexed by the double-buffered frame number at
`+20`: `+28`, `+32`, `+40`, `+44`, `+52`, `+56`, `+64`, `+68` are the source pointers the plane decoder is handed,
and `+4`/`+8` and `+12`/`+16` are the two luma and chroma strides. This is read from `sub_8249F980`, which is the
caller, so it is the layout the hook has to reproduce.

### The native Bink decoder

`app/src/cod2_video_bink.cpp` decodes a `.bik` with FFmpeg: it parses the container itself and hands FFmpeg the
compressed bytes of one frame at a time. `app/src/cod2_video_bink.h` is the interface; `PeliculaBink` is the whole
thing from a game path to a frame's planes.

Three things about the container are worth writing down, because each one fails silently:

* **A frame is not all picture.** When the file has audio tracks, every frame begins with one 32-bit length per
  track, each followed by that many bytes of audio, and the video packet is what is left. Hand the decoder the whole
  frame region and it reads audio as picture: it reports `DC value went out of bounds` and returns no frame. Of the
  game's nine movies, one has no audio and the other eight have one or two tracks, so both branches are live.
* **The keyframe bit is stored in the entry for the frame itself**, not the next one - and FFmpeg forces the first
  frame to be a keyframe whatever its bit says, which is what makes the index start somewhere.
* **The decoder needs the version and the extradata before it is opened**, not after: it takes the version from the
  top byte of `codec_tag` and the alpha flag from the first byte of the extradata (`decode_init`, `bink.c`). Set
  either one late and `avcodec_open2` succeeds and then returns no frame.

`tools/comprobar_bink.ps1` is the check, and it runs on all nine files: it parses each one's header and frame index,
splits the audio off the first frame, and decodes eight frames spread across the movie, asserting the decoded size,
that the picture is not uniform somewhere, that the frames are not all identical to each other, and that the audio
split found something to skip. Two mistakes it caught while being written: comparing two `Fotograma`s directly
compares whatever the decoder is holding *now*, because the planes belong to the decoder and not to the caller; and
the first frame of every one of these movies is a fade-in from black, so "the first frame has content" is not a
property of the data.

The nine movies, read from their own headers: `IW_logo.bik` 640x480, everything else 1280x720; `cod2_intro.bik` and
`cod2_attract.bik` are 3,937 frames at 2997/100, the four campaign ones are 1,564 to 3,136 frames at 500000/16683
(about 30); `legal.bik` is the only one without audio.

**The join**, which is `app/src/cod2_video_bink_nativo.cpp`. The port replaces one function, `sub_8249F980`, which is
called once per frame from `sub_82497D90` and from nowhere else. Its first argument is the game's own frame
descriptor, and reading it out of the running game (with the dump `app/src/cod2_video_bink_gancho.cpp` takes) gives
the whole layout:

| offset | what it is |
|---|---|
| `+0x04`, `+0x08` | width, height |
| `+0x0C`, `+0x10` | width/2, height/2 |
| `+0x14` | which of the two surfaces is being written |
| `+0x18`, `+0x48` | the two surfaces, each four 12-byte slots of `(flag, pointer, stride)`: the three planes in **Bink's** order, Y then V then U, and a fourth the game leaves null |

The slot order is the one thing in that table that is not the obvious one, and it is the one that shows on screen.
Bink's bitstream carries Y, V, U, and FFmpeg reads them in that order but writes the second into `data[2]` and the
third into `data[1]`, because an `AVFrame` has to be standard YUV420P - that is what its `swap_planes` does for
version `>= 'h'` (`plane_idx = plane ? plane ^ 3 : plane`, `libavcodec/bink.c`). The game's decoder is the real Bink
library and writes bitstream plane *k* into slot *k*, so the port has to cross the two chroma planes on the way in.
It did not, at first: the planes went into the slots FFmpeg named them by, which mirrors the hue, and on a game this
warm - sand, fire, dusk - that reads as "the colours have gone blue".

The two surfaces are double buffered: `sub_8249F980` flips `+0x14` to the other one when that one's Y pointer is not
null, writes the three planes into the new surface and reads the old one as the reference frame for motion
compensation. That is the whole of the guest's frame step, and the hook does the same thing with FFmpeg's planes.

The frame index is in the Bink context, not the descriptor: `sub_82496B20` returns a 17,408-byte context and
`[context+0x0C]` is the frame about to be decoded, plus one. `[context+184]` is the descriptor.

**It is checked against the game's own decoder, not against a plausible picture.** `--cod2_video_bink_planos=true`
makes the game write eight frames' surfaces to `logs/bink/`, and the check is the same eight frames taken twice: once
with `--no-cod2_video_bink_nativo`, so the guest's own Bink writes them, and once without, so the port does. All 8
frames x 3 planes come out byte for byte identical at the guest's strides (1280 for Y, 768 for a 640-wide chroma
plane) - `tools/comprobar_bink_planos.py` is the comparison.

Where the frames are taken from matters, and this is how the crossed chroma survived the first version of the check:
the first eight frames of **every one** of these nine movies are a colourless fade from black, so a comparison taken
there passes whatever order the planes are written in. `--cod2_video_bink_volcar_desde=300` moves the dump to a frame
with colour in it - `atvi.bik` frame 300 onward - and that is the run that found it. Cross-checked independently
against FFmpeg's own decode of the same frame: FFmpeg's `data[1]` (Cb) comes out equal to the game's *second* surface
and its `data[2]` (Cr) to the game's *first*, which is what "Y, V, U" means.

One thing it does not reproduce: `sub_8249F980` returns a byte count that `sub_82497D90` stores in the context at
`+736`, and `sub_82497990` compares that with `+740` (900 for `legal.bik`). The counter is a running tally inside the
guest's own bit reader and cannot be recomputed from FFmpeg's output, so the hook reproduces the *branch* instead:
zero on a keyframe, `+740` on every other frame, which is what the measured values do (`0` on frame 0, then 3600,
2811, 2638, 2599... all above 900). `sub_82497940`, the only path that reaches the comparison, is reached through the
Bink function table at `0x8207E670` and was never entered in any run here - it is a seek, and these movies are played
straight through.



### The previous port's videos

That game's cutscenes are [WMV3](glossary.md#wmv3) videos. The game's WMV3 decoder, recompiled from the Xbox 360
libraries, took 98 % of a core on the Switch and produced only about 22 frames per second of 720p30 video (1280x720
at 30 frames per second). So videos stuttered and their audio ended before the picture.

The port now gives the same bytes to FFmpeg's WMV3 decoder instead. It reads them through the game's own data
callbacks, and writes the decoded image planes (the Y, U and V parts of each frame) where the game expects them.
Videos now take 7-15 ms per frame. The code is in `app/src/cod2_video_nativo.cpp` and `app/src/cod2_video_wmv3.cpp`,
and the setting `cod2_video_wmv3_nativo` turns it on (on by default).

- **License.** FFmpeg is linked under the LGPL 2.1 (the SDK's build, configured without GPL parts). Do not link a GPL
  build.
- **Traps:**
  - identify the video by the stream being decoded, not by the last `.wmv` opened: the game opens the next one early;
  - read through the game's data callback instead of opening the file separately;
  - leave the "data remaining" count where the game expects it, or the game drops the next frame.
- **Checking.** A shadow mode (setting `cod2_video_wmv3_sombra`, off by default) decodes with both decoders and
  compares them. The videos were identical bit for bit, except for three short sections of one video with a mean
  difference of 0.02.
- **Re-encoded videos (fan translations).** The game's own videos only have I frames (complete pictures) and P frames
  (the changes from the previous picture), and they do not use the loop filter (a smoothing filter that WMV3 can
  apply to each picture). A Brazilian Portuguese fan translation re-encoded its dubbed videos with B frames (built
  from the pictures before and after them) and with the loop filter. The game's recompiled decoder cannot decode
  them: it leaves the picture empty, which shows as green, and it takes up to 330 ms per frame even on a PC. So the
  port decodes these videos with FFmpeg too:
  - the game decodes B frames with a third function (the one at `[ctx+3016]`), which the port replaces as well;
  - FFmpeg returns each frame from the same call that decodes it (`AV_CODEC_FLAG_LOW_DELAY`, and `has_b_frames`
    set to 0 after opening the decoder: without that, FFmpeg holds back the first frame);
  - if FFmpeg fails on one of these videos, the last good picture is shown again until the next I frame, because
    the game's decoder cannot be used as a fallback with them.

  The setting `cod2_video_wmv3_b_diag` (off by default) logs which picture planes and which fields of the decoder
  context each frame changes. It was used to find where the game leaves each type of frame.
- **Frame counters read 30.** During videos the game presents 30 frames per second, so frame counters read 30, not 60.

## Codec reinitialisation cost

Each XMA context opens FFmpeg's decoder again for every new sound, and every time FFmpeg rebuilt the same math
tables. In busy moments, `ff_mdct_init`, the split-radix permutation and the sine/cosine tables took about 28 % of
the XMA thread. Now FFmpeg's FFT and MDCT code keeps the bit-reversal table and the twiddle factors in a cache, so
they are built only once. That removed the cost. The change is in `sdk/thirdparty/FFmpeg/libavcodec/fft_template.c`
and `mdct_template.c`, in the parts marked `COD2`.
