// cod2 - PC tool: run the game under a debugger and report the exceptions it cannot report itself.
//
// Why it exists
//   The PC build dies with 0xC0000409 (fail-fast) and nothing in the log. A fail-fast is `int 0x29`,
//   and neither a vectored exception handler nor a terminate handler sees it: the kernel terminates the
//   process without dispatching to user-mode handlers. Windows' own dumper (WER LocalDumps) is not
//   configured on this machine and needs administrator rights to configure machine-wide. A debugger
//   does see it, so this is the smallest debugger that answers the two questions worth asking: which
//   subcode, and where.
//
// What it does
//   Creates the process with DEBUG_ONLY_THIS_PROCESS and waits for debug events, forwarding every
//   exception to the program's own handlers (DBG_EXCEPTION_NOT_HANDLED), which is what happens with no
//   debugger attached. It prints only what matters:
//     * a first-chance exception whose code is in the interesting list below, and
//     * any second-chance exception, which is by definition one nobody handled.
//   For each it prints the code, the subcode, the faulting instruction as module+offset, and the return
//   addresses found in the first kilobytes of that thread's stack, also as module+offset. The offsets
//   are the input of tools/pc/direccion.py, which resolves them against the linker map.
//
// Build (from a shell with vcvars64.bat loaded):
//     cl /nologo /O2 /std:c++20 /EHsc /Fe:tools\pc\vigilante_fallo.exe tools\pc\vigilante_fallo.cpp
//
// Usage:
//     tools\pc\vigilante_fallo.exe app\out\pc\cod2.exe --game_data_root=... 
//
// Windows only.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

struct Modulo {
  uint64_t base = 0;
  uint64_t tam = 0;
  std::string nombre;
};

std::vector<Modulo> ListarModulos(HANDLE proceso) {
  std::vector<Modulo> modulos;
  HMODULE handles[1024] = {};
  DWORD necesarios = 0;
  if (!EnumProcessModules(proceso, handles, sizeof(handles), &necesarios)) {
    return modulos;
  }
  const size_t cuantos = necesarios / sizeof(HMODULE);
  for (size_t i = 0; i < cuantos; ++i) {
    MODULEINFO info{};
    if (!GetModuleInformation(proceso, handles[i], &info, sizeof(info))) {
      continue;
    }
    char ruta[MAX_PATH] = {};
    GetModuleFileNameExA(proceso, handles[i], ruta, MAX_PATH);
    const char* corto = strrchr(ruta, '\\');
    Modulo m;
    m.base = uint64_t(reinterpret_cast<uintptr_t>(info.lpBaseOfDll));
    m.tam = info.SizeOfImage;
    m.nombre = corto ? corto + 1 : ruta;
    modulos.push_back(m);
  }
  return modulos;
}

// The module list is taken the first time it is needed and not at CreateProcess: at that moment the
// loader has not run yet and the process has one module (itself).
std::vector<Modulo> g_modulos;
HANDLE g_vm = nullptr;

void AsegurarModulos(HANDLE proceso, DWORD pid) {
  if (!g_vm) {
    // CreateProcess' handle is not guaranteed to include PROCESS_VM_READ, and without it every
    // ReadProcessMemory below fails.
    g_vm = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
  }
  if (!g_modulos.empty()) {
    return;
  }
  g_modulos = ListarModulos(proceso);
  std::printf("modulos:\n");
  for (const Modulo& m : g_modulos) {
    std::printf("  %s base 0x%llX tam 0x%llX\n", m.nombre.c_str(),
                static_cast<unsigned long long>(m.base), static_cast<unsigned long long>(m.tam));
  }
  std::fflush(stdout);
}

std::string Donde(const std::vector<Modulo>& modulos, uint64_t direccion) {
  for (const Modulo& m : modulos) {
    if (direccion >= m.base && direccion < m.base + m.tam) {
      char texto[256];
      std::snprintf(texto, sizeof(texto), "%s+0x%llX", m.nombre.c_str(),
                    static_cast<unsigned long long>(direccion - m.base));
      return texto;
    }
  }
  char texto[64];
  std::snprintf(texto, sizeof(texto), "0x%llX", static_cast<unsigned long long>(direccion));
  return texto;
}

// A stack word is a return address if it lands in some module. The first kilobytes of a stack hold the
// frames that matter. FlushInstructionCache is not needed: only the module list is read.
void VolcarPila(HANDLE proceso, const std::vector<Modulo>& modulos, const CONTEXT& ctx) {
  constexpr size_t kBytes = 8192;
  std::vector<uint8_t> datos(kBytes);
  SIZE_T leidos = 0;
  if (!ReadProcessMemory(proceso, reinterpret_cast<LPCVOID>(uintptr_t(ctx.Rsp)), datos.data(), kBytes,
                         &leidos) ||
      leidos < 8) {
    std::printf("    (no se pudo leer la pila en 0x%llX, error %lu)\n",
                static_cast<unsigned long long>(ctx.Rsp), GetLastError());
    return;
  }
  int mostrados = 0;
  for (size_t i = 0; i + 8 <= size_t(leidos); i += 8) {
    uint64_t palabra = 0;
    std::memcpy(&palabra, datos.data() + i, 8);
    if (palabra < 0x10000) {
      continue;
    }
    const std::string donde = Donde(modulos, palabra);
    if (donde[0] == '0') {
      continue;  // not inside a module
    }
    std::printf("    pila +0x%04zX  0x%016llX  %s\n", i, static_cast<unsigned long long>(palabra),
                donde.c_str());
    if (++mostrados >= 40) {
      std::printf("    ...\n");
      break;
    }
  }
}

bool Interesa(DWORD codigo) {
  switch (codigo) {
    case 0xC0000409:  // STATUS_STACK_BUFFER_OVERRUN, what __fastfail raises
    case 0xC0000005:  // STATUS_ACCESS_VIOLATION
    case 0xC000001D:  // STATUS_ILLEGAL_INSTRUCTION
    case 0xC00000FD:  // STATUS_STACK_OVERFLOW
    case 0xC0000135:  // STATUS_DLL_NOT_FOUND
    case 0xC0000142:  // STATUS_DLL_INIT_FAILED
      return true;
    default:
      return false;
  }
}

// The first-chance access violations are not failures: the runtime probes guest addresses on purpose,
// and with a debugger attached every probe stops here. Thousands of them arrive per second, and only
// two shapes are worth printing:
//   * 0xC0000409 at any chance: that is the death this tool exists for;
//   * anything at second chance: by definition, nobody handled it.
bool MereceLaPena(const EXCEPTION_RECORD& r, bool segunda) {
  if (r.ExceptionCode == 0xC0000409) {
    return true;
  }
  return segunda && Interesa(r.ExceptionCode);
}

const char* NombreCodigo(DWORD codigo) {
  switch (codigo) {
    case 0xC0000409:
      return "STATUS_STACK_BUFFER_OVERRUN (fail-fast: __fastfail / abort)";
    case 0xC0000005:
      return "STATUS_ACCESS_VIOLATION";
    case 0xC000001D:
      return "STATUS_ILLEGAL_INSTRUCTION";
    case 0xC00000FD:
      return "STATUS_STACK_OVERFLOW";
    default:
      return "";
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::printf("uso: vigilante_fallo.exe <juego.exe> [argumentos...]\n");
    return 2;
  }

  std::string linea = "\"" + std::string(argv[1]) + "\"";
  for (int i = 2; i < argc; ++i) {
    linea += " ";
    linea += argv[i];
  }
  std::vector<char> orden(linea.begin(), linea.end());
  orden.push_back('\0');

  STARTUPINFOA arranque{};
  arranque.cb = sizeof(arranque);
  PROCESS_INFORMATION proceso{};
  if (!CreateProcessA(nullptr, orden.data(), nullptr, nullptr, FALSE,
                      DEBUG_ONLY_THIS_PROCESS | CREATE_NEW_CONSOLE, nullptr, nullptr, &arranque,
                      &proceso)) {
    std::printf("no se pudo crear el proceso (error %lu)\n", GetLastError());
    return 1;
  }
  std::printf("depurando %s (pid %lu)\n", argv[1], proceso.dwProcessId);
  std::fflush(stdout);

  DWORD salida = 0;
  for (;;) {
    DEBUG_EVENT evento{};
    if (!WaitForDebugEvent(&evento, INFINITE)) {
      break;
    }
    DWORD continuar = DBG_EXCEPTION_NOT_HANDLED;

    if (evento.dwDebugEventCode == EXCEPTION_DEBUG_EVENT) {
      const EXCEPTION_RECORD& r = evento.u.Exception.ExceptionRecord;
      const bool segunda = evento.u.Exception.dwFirstChance == 0;
      if (MereceLaPena(r, segunda)) {
        AsegurarModulos(proceso.hProcess, evento.dwProcessId);
        CONTEXT ctx{};
        ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
        HANDLE hilo = OpenThread(THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE,
                                 evento.dwThreadId);
        const bool hay_ctx = hilo && GetThreadContext(hilo, &ctx);
        std::printf("\n%s %08lX %s\n", segunda ? "SEGUNDA OPORTUNIDAD" : "excepcion",
                    static_cast<unsigned long>(r.ExceptionCode), NombreCodigo(r.ExceptionCode));
        std::printf("  hilo %lu  direccion de la excepcion %s\n", evento.dwThreadId,
                    Donde(g_modulos, uint64_t(reinterpret_cast<uintptr_t>(r.ExceptionAddress))).c_str());
        if (r.NumberParameters) {
          std::printf("  parametros:");
          for (DWORD i = 0; i < r.NumberParameters && i < EXCEPTION_MAXIMUM_PARAMETERS; ++i) {
            std::printf(" %llu", static_cast<unsigned long long>(r.ExceptionInformation[i]));
          }
          std::printf("\n");
        }
        if (hay_ctx) {
          std::printf("  rip %s  rsp 0x%llX  rbp 0x%llX\n", Donde(g_modulos, ctx.Rip).c_str(),
                      static_cast<unsigned long long>(ctx.Rsp),
                      static_cast<unsigned long long>(ctx.Rbp));
          VolcarPila(g_vm ? g_vm : proceso.hProcess, g_modulos, ctx);
        }
        std::fflush(stdout);
        if (hilo) {
          CloseHandle(hilo);
        }
      }
    } else if (evento.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT) {
      salida = evento.u.ExitProcess.dwExitCode;
      std::printf("\nel proceso termino con 0x%08lX (%ld)\n", static_cast<unsigned long>(salida),
                  static_cast<long>(salida));
      std::fflush(stdout);
      ContinueDebugEvent(evento.dwProcessId, evento.dwThreadId, DBG_CONTINUE);
      break;
    }

    ContinueDebugEvent(evento.dwProcessId, evento.dwThreadId, continuar);
  }

  if (g_vm) {
    CloseHandle(g_vm);
  }
  CloseHandle(proceso.hThread);
  CloseHandle(proceso.hProcess);
  return 0;
}