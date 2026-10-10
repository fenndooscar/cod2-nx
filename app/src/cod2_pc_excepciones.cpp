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
//
//   The addresses are host addresses; tools/pc/direccion.py turns them into the sub_XXXXXXXX of the
//   generated code, using logs/simbolos.csv (see cod2_pc_simbolos.cpp).
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
constexpr int kMarcos = 24;
constexpr int kConPila = 16;  // how many thrown exceptions get their stack written

std::atomic<int> g_lanzadas{0};
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

LONG CALLBACK ManejadorExcepciones(EXCEPTION_POINTERS* info) {
  const EXCEPTION_RECORD& r = *info->ExceptionRecord;
  if (r.ExceptionCode != kFalloRapido && r.ExceptionCode != kExcepcionCpp) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  if (!REXCVAR_GET(cod2_pc_excepciones)) {
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