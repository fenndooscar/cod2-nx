// cod2 - prints this process's integrity level, and whether it can write a folder it is given.
//
// Why it exists: the game cannot create its save folder under C:\Users\<user>\Documents when it is
// launched from inside the workspace, while the same call succeeds from a copy of this program
// launched from outside it. The difference is not the directory and not the user: it is the token
// the launcher hands the child. This prints the one field that explains it.
//
// Usage: quien_soy.exe <carpeta a crear>
#include <windows.h>
#include <sddl.h>

#include <cstdio>
#include <filesystem>
#include <system_error>

int main(int argc, char** argv) {
  HANDLE token = nullptr;
  if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
    DWORD tam = 0;
    GetTokenInformation(token, TokenIntegrityLevel, nullptr, 0, &tam);
    if (tam) {
      auto* buf = static_cast<BYTE*>(malloc(tam));
      if (GetTokenInformation(token, TokenIntegrityLevel, buf, tam, &tam)) {
        auto* etiqueta = reinterpret_cast<TOKEN_MANDATORY_LABEL*>(buf);
        LPWSTR sid = nullptr;
        if (ConvertSidToStringSidW(etiqueta->Label.Sid, &sid)) {
          std::wprintf(L"integridad: %s\n", sid);
          LocalFree(sid);
        }
      }
      free(buf);
    }
    CloseHandle(token);
  }
  std::printf("exe: %s\n", argv[0]);

  if (argc > 1) {
    std::error_code ec;
    std::filesystem::create_directories(argv[1], ec);
    std::printf("crear %s -> ec=%d (%s)\n", argv[1], ec.value(), ec.message().c_str());
  }
  return 0;
}
