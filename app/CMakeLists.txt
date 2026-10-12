# cod2 - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.
#
# The app is a skeleton on purpose. Call of Duty 2 has not been run yet, so there is nothing to hook,
# nothing to replace natively and nothing to optimise: the first milestone is the game booting with
# ReXGlue's own emulated GPU, and every problem of the translation is far easier to find there. See
# README.md, "Not done", and src/aparcado/README.md for the code inherited from nfsmw-nx.

cmake_minimum_required(VERSION 3.25)
project(cod2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(COD2_SOURCES
    src/main.cpp
    # The game's own console, in the log (cvar cod2_consola, off by default). Works on both platforms:
    # the sink it replaces is the game's, not the runtime's.
    src/cod2_consola.cpp
    # Profile dump of the instrumented build for PGO (COD2_PGO=generar; empty otherwise).
    src/cod2_pgo.cpp
    # The console profile the game never asks for itself: 460.8 MHz of GPU in handheld through apm
    # (cvar cod2_switch_gpu_mhz, 0 leaves the console alone).
    src/cod2_rendimiento.cpp
    # The game's cutscenes are Bink 1 (`.bik`). This decodes them with FFmpeg instead of letting the guest run its own
    # recompiled Bink: the guest's decoder is only 8.7 % of a cutscene's CPU, but the frame it writes back is a watched
    # texture and every read of it faults, which is 58 % (docs/audio-and-video.md). Built by
    # tools/comprobar_bink.ps1 on its own; the join that puts it in the game is the next line.
    src/cod2_video_bink.cpp
    # What replaces the guest's own Bink: the frame decode of sub_8249F980, written into the surfaces the game's own
    # frame descriptor names. Cvar `cod2_video_bink_nativo`, on by default.
    src/cod2_video_bink_nativo.cpp
    # What the game's own Bink player is handed: the arguments of sub_822D1A30, sub_82496B20, sub_82497D90,
    # sub_8249F980 and sub_8249DA50, plus a one-off dump of the 17,408-byte context sub_82496B20 returns. This is how
    # the offsets the hook needs are found rather than guessed; see the file's header. Both cvars are off by default.
    src/cod2_video_bink_gancho.cpp

    # ----------------------------------------------------------------------------------------------------------
    # The native renderer: the port draws the game with Vulkan instead of imitating the Xbox 360's GPU.
    #
    # This is the same design as the port of Need for Speed: Most Wanted (docs/native-renderer.md), and these
    # files are that port's, moved out of src/aparcado/. They are the parts that turned out to be about the
    # *hardware* and not about the game: the PM4 ring, the Xenos register file, the EDRAM, the Vulkan device,
    # the texture pool. Between them they contain four references to a guest address, and three of those are
    # in comments.
    #
    # What is NOT here is the other game's glue: the hooks on its Direct3D device, its shader constructors, its
    # hot renderer functions, its shadow and LOD reductions. Those are game-specific by nature and are being
    # written again for Call of Duty 2 in src/cod2_nativo_ganchos.cpp and its neighbours. Until they exist the
    # renderer runs and records nothing, which is what the first milestone (the game boots without the
    # emulation and the screen shows a test colour) needs.
    #
    # The cvar is cod2_renderizador: "xenos" (default) loads the SDK's emulation, "nativo" takes the app's own
    # graphics system. See src/cod2_app.h.
    # ----------------------------------------------------------------------------------------------------------
    # C1: the app's own IGraphicsSystem. Presentation, the reserved MMIO range, the vblank thread and the
    # command ring sink: it answers what the game expects from the GPU and hands the draws on.
    src/cod2_nativo_sistema.cpp
    # C2: the copies (resolve and clear) and presenting the resolved texture of each Swap.
    src/cod2_nativo_destinos.cpp
    # C3-C6: the draws themselves - pipelines, vertex and index buffers, textures, state.
    src/cod2_nativo_dibujos.cpp
    # Which shader each draw in the ring is, and the texture pool the draws bind.
    src/cod2_nativo_shaders.cpp
    src/cod2_nativo_texturas_pool.cpp
    # The packed shader library the two above read (shaders/cod2_empaquetar.cpp writes it).
    src/cod2_shader_library.cpp
    # What the ring does not say about each draw, taken on the game thread: which shader each Direct3D draw
    # used, and the register-mirror snapshot the marker path compares against. Hooks the shader constructor
    # (sub_8209BD78); the draw entries are still to be derived, and the file's tail says what they have to
    # be and why.
    src/cod2_nativo_ganchos.cpp
    # The graphics settings the menu offers (F4) that the native renderer honours: post-processing,
    # antialiasing, sky glow and the colour filter, plus the internal resolution and the frame limit, which
    # are passed to the video mode before the game asks for it.
    src/cod2_ajustes_graficos.cpp
    # The ring-progress handshake the renderer drives and the guest's D3D polls used to be replaced by. The
    # two hooks that consume it are not derived for this game yet; the file says which and why.
    src/cod2_espera_anillo.cpp
    # The seams: the pieces the renderer calls that are still the other game's. Each is a placeholder with
    # the parked file it is waiting for named in its comment. Delete them one at a time as the real ones
    # arrive; the linker says when the file is empty.
    src/cod2_nativo_pendientes.cpp
)

if(WIN32)
    add_executable(cod2 WIN32 ${COD2_SOURCES})
    # PC build: masks again the floating-point exceptions that rex::ppc::FPSCR can leave unmasked on x86
    # (see the file). Without it the process dies after a few seconds with STATUS_FLOAT_INEXACT_RESULT.
    target_sources(cod2 PRIVATE src/cod2_pc_coma_flotante.cpp)
    # PC build: records the exception that ends the run - a thrown C++ exception or a fail-fast
    # (0xC0000409) - with its type and stack. The run dies with no line in the log (see the file).
    target_sources(cod2 PRIVATE src/cod2_pc_excepciones.cpp)
    # PC build: logs/simbolos.csv, the host address of every recompiled function, so a crash address can
    # be turned back into the sub_XXXXXXXX of the generated code (tools/pc/direccion.py).
    target_sources(cod2 PRIVATE src/cod2_pc_simbolos.cpp)
    # Sampling CPU profiler for the PC tests (cvar cod2_perfil_pc_desde_s, 0 = never) and the stack dump
    # the hang watchdog uses.
    target_sources(cod2 PRIVATE src/cod2_perfil_pc.cpp)
else()
    add_executable(cod2 ${COD2_SOURCES})
endif()

# GPU_PLUGINS leaves the graphics backend DLL next to the .exe
# (rexgpu-xenosd.dll in Debug, rexgpu-xenos.dll in Release).
#
# Both things are needed to get an image:
#   1. this line, which puts the DLL where the loader looks for it
#   2. gpu_plugin set to "xenos" at run time
#
# The gpu_plugin CVar is empty by default, and empty means "no GPU emulation": the game initializes its
# ring buffer and its graphics callbacks, the runtime discards them with "no GPU emulation loaded", and
# the screen stays black even though the game is running perfectly.
#
# src/cod2_app.h sets gpu_plugin=xenos by default, so the --gpu_plugin argument is not needed; it can
# still be passed to change it.
rexglue_setup_target(cod2 GPU_PLUGINS xenos)

# FFmpeg: src/cod2_video_bink.cpp decodes the game's Bink cutscenes with it. The runtime links libavcodec for its own
# audio decoder, but privately, so the include path and the library do not reach this target on their own. Both come
# from the SDK's own libavcodec target, which is where the Bink decoder is enabled
# (sdk/thirdparty/FFmpeg/config_cod2_bink.h; tools/comprobar_ffmpeg.py checks it stays enabled).
target_link_libraries(cod2 PRIVATE libavcodec libavutil)

# PC build with the SDK as a subdirectory: rex_app.cpp includes imgui.h and rexruntime links imgui
# privately, so its include path does not reach the executable. On Switch rexglue_switch.cmake handles it,
# and with the SDK installed the headers are already in its include directory (the target does not exist
# there). The same goes for renderdoc_app.h, which rex/ui/vulkan/instance.h includes outside Switch.
if(NOT REXGLUE_PLATFORM_SWITCH AND TARGET imgui)
    target_include_directories(cod2 PRIVATE
        $<TARGET_PROPERTY:imgui,INTERFACE_INCLUDE_DIRECTORIES>)
endif()
if(NOT REXGLUE_PLATFORM_SWITCH AND TARGET renderdoc)
    target_include_directories(cod2 PRIVATE
        $<TARGET_PROPERTY:renderdoc,INTERFACE_INCLUDE_DIRECTORIES>)
endif()

# Precompiled SPIR-V of the SDK presenter: the runtime uses it to draw the game image into the presenter
# output.
target_include_directories(cod2 PRIVATE "${REXSDK_DIR}/src/ui/shaders")

# xxHash, vendored in the SDK: the fingerprint of a texture and the key of the shader library. The
# native renderer's loader (src/cod2_shader_library.cpp) includes <xxhash.h> with XXH_INLINE_ALL, so it
# needs the header and not the library.
find_path(COD2_XXHASH_INCLUDE xxhash.h HINTS "${REXSDK_DIR}/thirdparty/xxHash"
    NO_CMAKE_FIND_ROOT_PATH REQUIRED)
target_include_directories(cod2 PRIVATE "${COD2_XXHASH_INCLUDE}")

# Nintendo Switch: cod2.nro next to the ELF. On the SD card it goes in sdmc:/switch/cod2-nx/
# with the game_root/ folder extracted beside it (src/cod2_app.h looks for it there).
if(REXGLUE_PLATFORM_SWITCH)
    # LTO on the game code and ours (not on the SDK libraries). Together with tools/llamadas_directas.py
    # (calls to functions without a hook go to __imp__, which is not weak) the compiler can inline small
    # game functions across files. -flto=2 and partitions: the whole-program phase handles the generated
    # source; with more processes it does not fit in 16 GB.
    option(COD2_LTO "LTO en el codigo del juego y de la app (Switch)" ON)
    if(COD2_LTO)
        foreach(objetivo cod2_recomp cod2)
            if(TARGET ${objetivo})
                target_compile_options(${objetivo} PRIVATE -flto -fno-fat-lto-objects)
            endif()
        endforeach()
        target_link_options(cod2 PRIVATE -flto=2 -flto-partition=balanced)
    endif()
    # Profile-guided optimization (PGO) in two steps.
    #   generar: instrumented build; the game dumps the profile to sdmc:/switch/cod2-nx/pgo every 3 minutes
    #            (src/cod2_pgo.cpp). GCC embeds paths "<profile folder>\<object with # instead of />.gcda"
    #            in the executable; a --wrap of fopen redirects them to the SD card.
    #   usar:    rebuilds with those .gcda files copied to pgo/<edition> on the PC.
    # -fprofile-prefix-path with the build folder in native format (backslashes), which is how GCC sees it:
    # that way each .gcda name is relative and the same on the console and on the PC.
    #
    # pgo/ is empty on purpose: the profiles that came with the fork were recorded for the other game and
    # can never match this one (see README.md, trap 3).
    get_filename_component(COD2_PGO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../pgo/cod2" ABSOLUTE)
    set(COD2_PGO "" CACHE STRING "Optimizacion por perfil: vacio, generar o usar")
    if(COD2_PGO)
        file(TO_NATIVE_PATH "${CMAKE_BINARY_DIR}" COD2_PGO_PREFIJO)
        if(COD2_PGO STREQUAL "generar")
            # -fno-profile-values: libgcov's indirect-call profiling reads the TLS with
            # mrs tpidr_el0, which is 0 on Horizon (libnx uses -mtp=soft), so the instrumented
            # build crashed at startup. Without value profiling the branch and function counts
            # remain, and those are what PGO uses.
            set(COD2_PGO_OPCIONES -fprofile-generate=${COD2_PGO_DIR} "-fprofile-prefix-path=${COD2_PGO_PREFIJO}"
                                   -fprofile-update=single -fno-profile-values)
            target_link_options(cod2 PRIVATE -fprofile-generate -Wl,--wrap=fopen)
            target_compile_definitions(cod2 PRIVATE COD2_PGO_GENERAR=1 "COD2_PGO_DIR=\"${COD2_PGO_DIR}\"")
        elseif(COD2_PGO STREQUAL "usar")
            set(COD2_PGO_OPCIONES -fprofile-use=${COD2_PGO_DIR} "-fprofile-prefix-path=${COD2_PGO_PREFIJO}"
                                   -fprofile-partial-training -fno-profile-values -fprofile-correction
                                   -Wno-missing-profile
                                   -Wno-error=coverage-mismatch)
            # -fprofile-correction: the instrumentation counts without synchronization
            # (-fprofile-update=single) and files used by several threads at once ended up with negative
            # counts: GCC treated that as a corrupt profile and stopped.
            target_link_options(cod2 PRIVATE -fprofile-use -fprofile-partial-training -fprofile-correction)
        else()
            message(FATAL_ERROR "COD2_PGO tiene que ser vacio, generar o usar (es '${COD2_PGO}')")
        endif()
        foreach(objetivo cod2_recomp cod2)
            if(TARGET ${objetivo})
                target_compile_options(${objetivo} PRIVATE ${COD2_PGO_OPCIONES})
            endif()
        endforeach()
    endif()
    # COD2_RECOMP_O2: A/B test of the optimization level of the recompiled code (cod2_recomp). -O3 bloats
    # the code and the A57 has 48 KB of instruction cache: -O2 gives smaller code. It comes after the -O3
    # of CMAKE_CXX_FLAGS_RELEASE, so it takes precedence.
    option(COD2_RECOMP_O2 "Compilar el codigo traducido con -O2 en vez de -O3" OFF)
    if(COD2_RECOMP_O2 AND TARGET cod2_recomp)
        target_compile_options(cod2_recomp PRIVATE -O2)
    endif()
    # Hot functions grouped at the start of .text: the A57 has 48 KB of instruction cache and a small TLB,
    # and hot code scattered across the whole executable forces a page table walk on every far jump. This
    # only changes where each function is placed (-ffunction-sections already gives one section per
    # function). The list comes from stack sampling on the console.
    #
    # The list is Call of Duty 2's: regenerated with `tools/calientes.py <rex_perfil.log> <cod2.elf> <out.txt>
    # app/orden_funciones.ld` from switch-logs/logs-old-1, the session that reaches a level. Before that it
    # held Need for Speed: Most Wanted's addresses, which matched nothing here. Note that it matches by
    # mangled symbol name, so a name that does not exist is silently ignored: a wrong list is not a build
    # error, it is a change that does nothing (see README.md, trap 1). The tool checks each name against
    # the ELF's symbol table for that reason.
    option(COD2_ORDEN_FUNCIONES "Juntar las funciones calientes al principio de .text" ON)
    if(COD2_ORDEN_FUNCIONES)
        # The map is not for the build: it is the only place where the section names the ordering file asks for
        # and the ones the link actually produced can be compared. A pattern that matches nothing is silent
        # (README.md, trap 1), and this is what tools/comprobar_orden.py reads to catch that.
        target_link_options(cod2 PRIVATE
            "-Wl,--section-ordering-file,${CMAKE_CURRENT_SOURCE_DIR}/orden_funciones.ld"
            "-Wl,-Map,${CMAKE_BINARY_DIR}/cod2.map")
        set_property(TARGET cod2 APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/orden_funciones.ld")
    endif()
    # Homebrew menu icon: a 256x256 JPEG, as elf2nro expects. It is not distributed with the sources;
    # without tools/switch/icono/cod2_icono.jpg the NRO gets the default libnx icon.
    set(COD2_ICONO "${CMAKE_CURRENT_SOURCE_DIR}/../tools/switch/icono/cod2_icono.jpg")
    set(COD2_ICONO_ARG)
    if(EXISTS "${COD2_ICONO}")
        set(COD2_ICONO_ARG ICON "${COD2_ICONO}")
    endif()
    rexglue_switch_add_nro(cod2
        NAME "Call of Duty 2"
        AUTHOR "fenndooscar"
        VERSION "0.1.0"
        ${COD2_ICONO_ARG})
endif()
