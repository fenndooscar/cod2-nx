// cod2 - PC build: record the exceptions that end the run.
//
// What happens
//   The PC build runs for a while and then the process disappears with exit code 0xC0000409
//   (STATUS_STACK_BUFFER_OVERRUN, a "fail fast") and nothing in the log: no [FATAL] line, no entry in the
//   Windows event log, no dump. REX_FATAL and assert_always log before they abort, so the death does not
//   come from them, and the SDK's own handler (sdk/src/core/exception_handler_win.cpp) only looks at
//   STATUS_ILLEGAL_INSTRUCTION and STATUS_ACCESS_VIOLATION.
//
//   Two deaths look like that and both were invisible:
//     1. `__fastfail` (the `int 0x29` that /GS, the heap's corruption check and abort() all end in). The
//        exit code is 0xC0000409 and nothing user-visible runs.
//     2. An exception nobody catches. Since UCRT, the unhandled path ends in terminate(), terminate()
//        calls abort(), and abort() is a fail-fast: the same 0xC0000409. The SDK installs a terminate
//        handler on the Switch only (sdk/src/ui/runtime_switch.cpp), so on Windows the type and what()
//        were lost - and in a debugger the same death shows up as 0xE06D7363 (an MSVC C++ exception)
//        instead, which is how it was found.
//
//   It found the first one: a std::filesystem_error - std::filesystem::create_directories called
//   without an error_code, on a path it could not create, thrown inside UserProfile::SaveSetting and
//   caught by nobody. That is why the fix is there and not here; this file only makes the next one
//   visible.
//
// What it does
//   * A terminate handler that writes the exception type, what() and a stack, then aborts as before.
//   * A vectored exception handler for a thrown C++ exception (0xE06D7363), one line per throw: the
//     exception type read from the throw information the CRT passes in ExceptionInformation, and the
//     place it was thrown from. The first throws also get a stack. One line each and never through the
//     logger, because a throw can come from inside the logger and this has to survive it.
//   * A vectored exception handler for the fail-fast itself, in case it ever is delivered.
//   * A vectored exception handler for an access violation the SDK did not claim, which is the one
//     that kills the process. The SDK's own line names the guest address that was touched and not the
//     instruction that touched it, so this adds the host instruction and a stack for the first few.
//     `Unhandled guest access violation: read of guest 0x0000004C` is a null base plus a field
//     offset, and only the instruction says which field of which object was expected there.
//     It also dumps the guest registers of the faulting thread (lr names the call site) and reads the
//     game's own fatal-error buffer, because the death is usually a second failure inside the loop that
//     reports the first one, and the first one is written nowhere else.
//
//   The addresses are host addresses; tools/pc/direccion.py turns them into the sub_XXXXXXXX of the
//   generated code, using logs/simbolos.csv (see cod2_pc_simbolos.cpp). The guest stack it also prints
//   is already in sub_XXXXXXXX form.
//
// How to turn it off without a rebuild
//   --cod2_pc_excepciones=false
//
// Windows only: the Switch has its own crash log (switch_crash_hooks.c).

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/thread_state.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>

#include "cod2_perfil_pc.h"

namespace {

REXCVAR_DEFINE_BOOL(cod2_pc_excepciones, true, "COD2",
                    "PC: dejar rastro de la excepcion que termina el proceso (terminate y las "
                    "excepciones de C++ lanzadas), en logs/excepciones.txt")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

constexpr DWORD kFalloRapido = 0xC0000409;  // STATUS_STACK_BUFFER_OVERRUN, what __fastfail raises
constexpr DWORD kExcepcionCpp = 0xE06D7363; // the code MSVC's throw uses
constexpr DWORD kViolacionAcceso = 0xC0000005;
constexpr int kMarcos = 24;
constexpr int kConPila = 16;  // how many thrown exceptions get their stack written
constexpr int kViolacionesConPila = 4;  // and how many access violations

// The game's own fatal-error path, both read straight out of guest memory. They are the only copy of what
// the game was complaining about: the process dies inside the loop that reports it, so it never reaches
// the log. Addresses from the generated code of sub_8233AB80 (Com_Error):
//   0x83D45480  the 4096-byte buffer it formats the message into (`addi r4,r11,21632`, r11=0x83D40000)
//   0x83D376AC  its re-entrancy flag, non-zero once it is inside that loop (`lwz r11,30380(r28)`)
constexpr uint32_t kBufferComError = 0x83D45480;
constexpr uint32_t kBanderaComError = 0x83D376AC;

std::atomic<int> g_lanzadas{0};
std::atomic<int> g_violaciones{0};
bool g_hablando = false;  // only read and written from the reporter below

const char* NombreSubcodigo(ULONG_PTR subcodigo) {
  switch (subcodigo) {
    case 0:
      return "origen desconocido";
    case 1:
      return "FAST_FAIL_LEGACY_GS_VIOLATION";
    case 2:
      return "FAST_FAIL_STACK_COOKIE_CHECK_FAILURE (se escribio mas alla de un buffer local)";
    case 3:
      return "FAST_FAIL_CORRUPT_LIST_ENTRY (lista de memoria corrupta)";
    case 4:
      return "FAST_FAIL_INCORRECT_STACK (la pila no esta donde deberia)";
    case 5:
      return "FAST_FAIL_INVALID_ARG";
    case 6:
      return "FAST_FAIL_GS_COOKIE_INIT";
    case 7:
      return "FAST_FAIL_FATAL_APP_EXIT (algo llamo a __fastfail a proposito)";
    case 8:
      return "FAST_FAIL_RANGE_CHECK_FAILURE";
    case 9:
      return "FAST_FAIL_UNSAFE_REGISTRY_ACCESS";
    case 13:
      return "FAST_FAIL_INVALID_CONTROL_STACK";
    case 14:
      return "FAST_FAIL_SET_CONTEXT_DENIED";
    case 22:
      return "FAST_FAIL_IN_PAGE_ERROR_READ";
    case 23:
      return "FAST_FAIL_IN_PAGE_ERROR_WRITE";
    default:
      return "subcodigo no documentado";
  }
}

// Module name and offset of an address. No allocation: the handler may be running with the heap held.
std::string DondeEsta(uint64_t direccion) {
  HMODULE modulo = nullptr;
  if (!GetModuleHandleExA(
          GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
          reinterpret_cast<LPCSTR>(uintptr_t(direccion)), &modulo)) {
    return "?";
  }
  char nombre[MAX_PATH] = "?";
  GetModuleFileNameA(modulo, nombre, MAX_PATH);
  const char* corto = strrchr(nombre, '\\');
  char texto[512];
  std::snprintf(texto, sizeof(texto), "%s+0x%llX", corto ? corto + 1 : nombre,
                static_cast<unsigned long long>(direccion - uint64_t(uintptr_t(modulo))));
  return texto;
}

// The exception's own file, never the log: a throw can come from inside the logger, which would then be
// holding its mutex, and a line that does not get written is worse than no line at all. One fopen per
// line, no shared state.
void EscribirCrudo(const char* texto) {
  if (std::FILE* f = std::fopen("logs/excepciones.txt", "ab")) {
    std::fputs(texto, f);
    std::fputc('\n', f);
    std::fclose(f);
  }
}

void EscribirCrudoYLog(const char* texto) {
  EscribirCrudo(texto);
  if (g_hablando) {
    REXLOG_CRITICAL("[pc] {}", texto);
  }
}

// Everything readable about a thrown C++ exception, straight from the record. Pointers are read with
// ReadProcessMemory so a mangled one returns nothing instead of faulting inside the handler.
//
//   ExceptionInformation[0]  the CRT's magic and version ("msc" plus a byte)
//   ExceptionInformation[1]  the exception object itself
//   ExceptionInformation[2]  the _ThrowInfo, which describes how to catch it
//
// Two independent routes to the type name, because either can be missing:
//   a) _ThrowInfo + 24 -> _CatchableTypeArray { int n; _CatchableType* a[] }
//      -> _CatchableType + 8 -> _TypeDescriptor -> + 16 -> char name[]
//   b) the object's own vtable -> RTTI complete object locator at vtable - 8 -> _TypeDescriptor at + 12
// Route (b) only works for a polymorphic class, which std::exception and friends are.
void DescribirExcepcion(const EXCEPTION_RECORD& r, char* salida, size_t tam) {
  const HANDLE yo = GetCurrentProcess();
  auto leer = [yo](uint64_t donde, void* destino, SIZE_T cuantos) {
    SIZE_T leidos = 0;
    return ReadProcessMemory(yo, reinterpret_cast<LPCVOID>(uintptr_t(donde)), destino, cuantos,
                             &leidos) != 0;
  };

  char* p = salida;
  size_t queda = tam;
  auto anadir = [&p, &queda](const char* formato, ...) {
    if (queda < 2) {
      return;
    }
    va_list args;
    va_start(args, formato);
    const int escritos = std::vsnprintf(p, queda, formato, args);
    va_end(args);
    if (escritos > 0) {
      const size_t usados = size_t(escritos) < queda - 1 ? size_t(escritos) : queda - 1;
      p += usados;
      queda -= usados;
    }
  };

  anadir("params=%u", uint32_t(r.NumberParameters));
  for (DWORD i = 0; i < r.NumberParameters && i < 4; ++i) {
    anadir(" p%u=0x%llX", uint32_t(i),
           static_cast<unsigned long long>(r.ExceptionInformation[i]));
  }

  const uint64_t objeto = r.NumberParameters > 1 ? uint64_t(r.ExceptionInformation[1]) : 0;
  const uint64_t info = r.NumberParameters > 2 ? uint64_t(r.ExceptionInformation[2]) : 0;

  char nombre[512] = {};
  if (info) {
    uint64_t tabla = 0;
    if (leer(info + 24, &tabla, sizeof(tabla)) && tabla) {
      uint64_t primer_tipo = 0;
      if (leer(tabla + 8, &primer_tipo, sizeof(primer_tipo)) && primer_tipo) {
        uint64_t descriptor = 0;
        if (leer(primer_tipo + 8, &descriptor, sizeof(descriptor)) && descriptor) {
          leer(descriptor + 16, nombre, sizeof(nombre) - 1);
        }
      }
    }
  }
  if (nombre[0] == '\0' && objeto) {
    // Route (b): vtable - 8 holds the complete object locator, whose TypeDescriptor field is at + 12.
    uint64_t vtable = 0;
    if (leer(objeto, &vtable, sizeof(vtable)) && vtable) {
      uint64_t localizador = 0;
      uint32_t desplazamiento = 0;
      if (leer(vtable - 8, &localizador, sizeof(localizador)) && localizador &&
          leer(localizador + 12, &desplazamiento, sizeof(desplazamiento)) && desplazamiento) {
        leer(vtable + desplazamiento, nombre, sizeof(nombre) - 1);
      }
    }
  }
  anadir(" tipo=\"%s\"", nombre[0] ? nombre : "?");

  // The first 64 bytes of the object, so a type whose name cannot be read is still recognisable.
  if (objeto) {
    unsigned char crudo[64] = {};
    if (leer(objeto, crudo, sizeof(crudo))) {
      anadir(" objeto=");
      for (int i = 0; i < 32; ++i) {
        anadir("%02X", crudo[i]);
      }
    }
  }
}

// What the guest was doing when it faulted. The SDK's line names the guest address that was touched; this
// names the guest code that touched it, which is what the generated code speaks in:
//
//   lr      where the guest function running right now will return to, so it is the exact call site.
//           sub_8233AB80 (Com_Error) leaves 0x8233AC50 or 0x8233ACFC there depending on which of its two
//           RtlLeaveCriticalSection calls it is inside.
//   r3..r6  the arguments that call was made with.
//   r27..   the callee-saved registers, which is where a corrupted pointer shows up first.
//
// Then the game's own fatal-error state. It is the only copy of what the game was complaining about: the
// process dies inside the loop that reports it, so it never reaches the log.
void VolcarContextoInvitado() {
  char linea[1024];
  auto* estado = rex::runtime::ThreadState::Get();
  if (!estado || !estado->context()) {
    EscribirCrudo("  invitado: el hilo que falla no es del juego (sin contexto)");
    return;
  }
  const auto& c = *estado->context();
  std::snprintf(linea, sizeof(linea),
                "  invitado: hilo=0x%08X lr=0x%08X r3=0x%08X r4=0x%08X r5=0x%08X r6=0x%08X "
                "r27=0x%08X r28=0x%08X r29=0x%08X r30=0x%08X r31=0x%08X r1=0x%08X r13=0x%08X "
                "ctr=0x%08X",
                estado->thread_id(), uint32_t(c.lr), c.r3.u32, c.r4.u32, c.r5.u32, c.r6.u32,
                c.r27.u32, c.r28.u32, c.r29.u32, c.r30.u32, c.r31.u32, c.r1.u32, c.r13.u32,
                c.ctr.u32);
  EscribirCrudoYLog(linea);

  auto* memoria = estado->memory();
  if (!memoria) {
    EscribirCrudo("  invitado: sin memoria que leer");
    return;
  }
  const uint8_t* base = memoria->virtual_membase();
  auto leer = [base](uint32_t invitado, void* destino, size_t cuantos) {
    SIZE_T leidos = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(base + invitado),
                             destino, cuantos, &leidos) != 0;
  };

  char mensaje[512] = {};
  if (leer(kBufferComError, mensaje, sizeof(mensaje) - 1)) {
    for (char& ch : mensaje) {
      if (ch == '\n' || ch == '\r' || ch == '\t') {
        ch = ' ';
      } else if (static_cast<unsigned char>(ch) < 0x20) {
        ch = '.';
      }
    }
    std::snprintf(linea, sizeof(linea), "  Com_Error dice: \"%s\"", mensaje);
  } else {
    std::snprintf(linea, sizeof(linea), "  Com_Error: no se pudo leer 0x%08X", kBufferComError);
  }
  EscribirCrudoYLog(linea);

  uint32_t bandera = 0;
  leer(kBanderaComError, &bandera, sizeof(bandera));
  std::snprintf(linea, sizeof(linea),
                "  Com_Error reentrante=0x%08X (distinto de cero = el juego ya estaba informando de un "
                "error fatal antes de fallar aqui)",
                bandera);
  EscribirCrudoYLog(linea);

  // The guest's own call stack, which the host one cannot give: a hundred generated functions share the
  // same host frame shape, so RtlCaptureStackBackTrace returns host addresses that tools/pc/direccion.py
  // can turn back into sub_XXXXXXXX, but the *guest* chain is what names the game's functions directly.
  //
  // It is walkable because the generated `stwu r1,-N(r1)` stores the caller's r1 at the new r1, exactly
  // as the instruction does, so 0(r1) is the previous frame. Each frame's words are then scanned for
  // values inside the code range, which is where the saved return address is.
  constexpr uint32_t kCodigoInicio = 0x82090000;  // from app/cod2_manifest.toml, the same range
  constexpr uint32_t kCodigoFin = 0x824A8DEC;     // cod2_pc_simbolos.cpp walks.
  constexpr int kMarcosInvitado = 20;

  std::snprintf(linea, sizeof(linea), "  pila del invitado desde r1=0x%08X:", c.r1.u32);
  EscribirCrudoYLog(linea);

  uint32_t sp = c.r1.u32;
  for (int marco = 0; marco < kMarcosInvitado && sp >= 0x1000; ++marco) {
    uint32_t arriba = 0;
    if (!leer(sp, &arriba, sizeof(arriba))) {
      break;
    }
    arriba = __builtin_bswap32(arriba);  // guest words are big-endian
    if (arriba <= sp || arriba - sp > 0x10000) {
      arriba = sp + 128;  // not a back chain any more; still worth one frame of scanning
    }

    uint32_t palabras[64] = {};
    uint32_t cuantas = (arriba - sp) / 4;
    if (cuantas > 64) {
      cuantas = 64;
    }
    if (!leer(sp, palabras, cuantas * sizeof(uint32_t))) {
      break;
    }

    char destinos[320] = {};
    size_t usados = 0;
    for (uint32_t i = 0; i < cuantas; ++i) {
      const uint32_t valor = __builtin_bswap32(palabras[i]);
      if (valor >= kCodigoInicio && valor < kCodigoFin) {
        const int escritos = std::snprintf(destinos + usados, sizeof(destinos) - usados, "%s0x%08X",
                                           usados ? " " : "", valor);
        if (escritos <= 0 || size_t(escritos) >= sizeof(destinos) - usados) {
          break;
        }
        usados += size_t(escritos);
      }
    }
    std::snprintf(linea, sizeof(linea), "    marco %2d: sp=0x%08X  codigo en la pila: %s", marco, sp,
                  destinos[0] ? destinos : "(ninguno)");
    EscribirCrudoYLog(linea);

    sp = arriba;
  }
}

LONG CALLBACK ManejadorExcepciones(EXCEPTION_POINTERS* info) {
  const EXCEPTION_RECORD& r = *info->ExceptionRecord;
  if (r.ExceptionCode != kFalloRapido && r.ExceptionCode != kExcepcionCpp &&
      r.ExceptionCode != kViolacionAcceso) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  if (!REXCVAR_GET(cod2_pc_excepciones)) {
    return EXCEPTION_CONTINUE_SEARCH;
  }

  // An access violation the SDK did not claim is the one that kills the process. The SDK's own line
  // says which guest address was touched, and the guest address alone does not say where from: this
  // adds the host instruction, which tools/pc/direccion.py turns into a sub_XXXXXXXX. Only the first
  // few get a stack, because a run that is dying can raise a great many of them.
  if (r.ExceptionCode == kViolacionAcceso) {
    const int numero = g_violaciones.fetch_add(1, std::memory_order_relaxed) + 1;
    if (numero > kViolacionesConPila) {
      return EXCEPTION_CONTINUE_SEARCH;
    }
    g_hablando = true;
    const uint64_t tocado = r.NumberParameters > 1 ? uint64_t(r.ExceptionInformation[1]) : 0;
    char linea[1024];
    std::snprintf(linea, sizeof(linea),
                  "VIOLACION DE ACCESO %d: %s de 0x%016llX en %s", numero,
                  r.NumberParameters > 0 && r.ExceptionInformation[0] ? "escritura" : "lectura",
                  static_cast<unsigned long long>(tocado),
                  DondeEsta(info->ContextRecord->Rip).c_str());
    EscribirCrudoYLog(linea);

    // The host registers, which are the ground truth: the guest's r3 is the argument the host
    // instruction is actually using, so comparing the two says whether the guest context below is
    // the faulting thread's at all.
    std::snprintf(linea, sizeof(linea),
                  "  host: rip=0x%016llX rcx=0x%016llX rdx=0x%016llX r8=0x%016llX r9=0x%016llX "
                  "rsp=0x%016llX",
                  static_cast<unsigned long long>(info->ContextRecord->Rip),
                  static_cast<unsigned long long>(info->ContextRecord->Rcx),
                  static_cast<unsigned long long>(info->ContextRecord->Rdx),
                  static_cast<unsigned long long>(info->ContextRecord->R8),
                  static_cast<unsigned long long>(info->ContextRecord->R9),
                  static_cast<unsigned long long>(info->ContextRecord->Rsp));
    EscribirCrudoYLog(linea);

    VolcarContextoInvitado();
    void* marcos[kMarcos] = {};
    const USHORT cuantos = RtlCaptureStackBackTrace(0, kMarcos, marcos, nullptr);
    for (USHORT i = 0; i < cuantos; ++i) {
      const uint64_t marco = uint64_t(reinterpret_cast<uintptr_t>(marcos[i]));
      std::snprintf(linea, sizeof(linea), "  marco %2u: 0x%016llX  %s", i,
                    static_cast<unsigned long long>(marco), DondeEsta(marco).c_str());
      EscribirCrudoYLog(linea);
    }
    if (auto logger = rex::GetLogger()) {
      logger->flush();
    }
    if (numero == 1) {
      cod2::perfil_pc::VolcarPilas("violacion");
    }
    return EXCEPTION_CONTINUE_SEARCH;
  }

  g_hablando = true;

  // The thread is stopped here, so this is the exact place the exception was raised from.
  void* marcos[kMarcos] = {};
  const USHORT cuantos = RtlCaptureStackBackTrace(0, kMarcos, marcos, nullptr);
  const uint64_t rip = info->ContextRecord->Rip;

  char linea[1024];
  if (r.ExceptionCode == kExcepcionCpp) {
    const int numero = g_lanzadas.fetch_add(1, std::memory_order_relaxed) + 1;
    char detalle[768] = {};
    DescribirExcepcion(r, detalle, sizeof(detalle));
    std::snprintf(linea, sizeof(linea), "EXCEPCION C++ %d lanzada en %s: %s", numero,
                  DondeEsta(rip).c_str(), detalle);
    EscribirCrudoYLog(linea);
    if (numero > kConPila) {
      // One line each: enough to see the last throw before the process died, and no stack flood.
      return EXCEPTION_CONTINUE_SEARCH;
    }
  } else {
    const ULONG_PTR subcodigo =
        r.NumberParameters > 0 ? r.ExceptionInformation[0] : 0;
    std::snprintf(linea, sizeof(linea), "FALLO RAPIDO %08X, subcodigo %llu (%s) en %s",
                  uint32_t(kFalloRapido), static_cast<unsigned long long>(subcodigo),
                  NombreSubcodigo(subcodigo), DondeEsta(rip).c_str());
    EscribirCrudoYLog(linea);
  }

  for (USHORT i = 0; i < cuantos; ++i) {
    const uint64_t marco = uint64_t(reinterpret_cast<uintptr_t>(marcos[i]));
    std::snprintf(linea, sizeof(linea), "  marco %2u: 0x%016llX  %s", i,
                  static_cast<unsigned long long>(marco), DondeEsta(marco).c_str());
    EscribirCrudoYLog(linea);
  }
  if (g_hablando) {
    if (auto logger = rex::GetLogger()) {
      logger->flush();
    }
  }
  if (r.ExceptionCode == kFalloRapido) {
    // Every other thread, in case the one that failed is a worker and the state that matters is
    // elsewhere. The caller is skipped by the dump itself.
    cod2::perfil_pc::VolcarPilas("fallo_rapido");
  }
  return EXCEPTION_CONTINUE_SEARCH;  // the process dies (or handles it) as it would have
}

// The other half: an exception nobody catches. terminate() is where that ends, and what() has the only
// readable description of it.
[[noreturn]] void Terminacion() {
  g_hablando = true;
  char motivo[768] = "std::terminate sin excepcion activa";
  if (std::exception_ptr actual = std::current_exception()) {
    try {
      std::rethrow_exception(actual);
    } catch (const std::exception& e) {
      std::snprintf(motivo, sizeof(motivo), "std::terminate: %s", e.what());
    } catch (...) {
      std::snprintf(motivo, sizeof(motivo),
                    "std::terminate: excepcion de un tipo que no deriva de std::exception");
    }
  }
  void* marcos[kMarcos] = {};
  const USHORT cuantos = RtlCaptureStackBackTrace(0, kMarcos, marcos, nullptr);
  char linea[1024];
  std::snprintf(linea, sizeof(linea), "TERMINATE: %s", motivo);
  EscribirCrudoYLog(linea);
  for (USHORT i = 0; i < cuantos; ++i) {
    const uint64_t marco = uint64_t(reinterpret_cast<uintptr_t>(marcos[i]));
    std::snprintf(linea, sizeof(linea), "  marco %2u: 0x%016llX  %s", i,
                  static_cast<unsigned long long>(marco), DondeEsta(marco).c_str());
    EscribirCrudoYLog(linea);
  }
  if (auto logger = rex::GetLogger()) {
    logger->flush();
  }
  cod2::perfil_pc::VolcarPilas("terminate");
  std::abort();
}

struct Registro {
  // Installed unconditionally: the cvar is read inside the handlers, because a static constructor
  // cannot rely on another translation unit's cvar having been registered yet.
  Registro() {
    AddVectoredExceptionHandler(1, &ManejadorExcepciones);
    std::set_terminate(&Terminacion);
  }
};
Registro g_registro;

}  // namespace

#endif  // _WIN32