// cod2 - says what Windows exposes for a game pad: the XInput slots, and the raw input devices SDL
// enumerates.
//
// Why it exists: a pad that SDL had been reporting stopped appearing in the log while Windows still
// showed a healthy device node and XInput still answered for slot 0. The port's log only prints what
// SDL reports, so it cannot tell "Windows no longer has the pad" apart from "Windows has it and SDL
// is not picking it up" -- and the two have different fixes. This prints both layers at once, from
// the same kind of process that runs the game.
//
// Usage: mando_ver.exe [segundos]   -> default 6. Polls XInput while enumerating raw input.
#include <windows.h>
#include <xinput.h>

#include <cstdio>
#include <cstdlib>

static const char* nombre_uso(USHORT page, USHORT uso) {
  static char buf[64];
  std::snprintf(buf, sizeof(buf), "uso %04X:%04X", page, uso);
  return buf;
}

int main(int argc, char** argv) {
  const int segundos = argc > 1 ? std::atoi(argv[1]) : 6;

  std::printf("--- XInput ---\n");
  DWORD paquete_inicial[XUSER_MAX_COUNT] = {};
  bool hay[XUSER_MAX_COUNT] = {};
  for (DWORD i = 0; i < XUSER_MAX_COUNT; i++) {
    XINPUT_STATE e = {};
    DWORD r = XInputGetState(i, &e);
    hay[i] = (r == ERROR_SUCCESS);
    paquete_inicial[i] = e.dwPacketNumber;
    // SDL does not use XInputGetState to find pads: SDL_XINPUT_JoystickDetect gates on
    // XInputGetCapabilities. The two can disagree, and when they do SDL sees nothing while
    // every other XInput consumer sees a working pad.
    XINPUT_CAPABILITIES caps = {};
    DWORD rc = XInputGetCapabilities(i, XINPUT_FLAG_GAMEPAD, &caps);
    if (hay[i]) {
      std::printf("ranura %u: conectado, paquete %u, botones 0x%04X, sticks %d %d %d %d\n", i,
                  e.dwPacketNumber, e.Gamepad.wButtons, e.Gamepad.sThumbLX, e.Gamepad.sThumbLY,
                  e.Gamepad.sThumbRX, e.Gamepad.sThumbRY);
    } else {
      std::printf("ranura %u: XInputGetState error %lu\n", i, r);
    }
    std::printf("          XInputGetCapabilities -> %s (tipo %u, subtipo %u)\n",
                rc == ERROR_SUCCESS ? "OK" : "FALLA", caps.Type, caps.SubType);
    if (rc != ERROR_SUCCESS) {
      std::printf("          codigo %lu%s\n", rc,
                  rc == ERROR_DEVICE_NOT_CONNECTED ? " (ERROR_DEVICE_NOT_CONNECTED)" : "");
    }
  }

  std::printf("--- Raw Input (lo que enumera SDL en Windows) ---\n");
  UINT cuantos = 0;
  if (GetRawInputDeviceList(nullptr, &cuantos, sizeof(RAWINPUTDEVICELIST)) != 0 || cuantos == 0) {
    std::printf("no se pudo listar (error %lu)\n", GetLastError());
  } else {
    auto* lista = static_cast<RAWINPUTDEVICELIST*>(malloc(sizeof(RAWINPUTDEVICELIST) * cuantos));
    cuantos = GetRawInputDeviceList(lista, &cuantos, sizeof(RAWINPUTDEVICELIST));
    int mandos = 0;
    for (UINT i = 0; i < cuantos; i++) {
      RID_DEVICE_INFO info = {};
      info.cbSize = sizeof(info);
      UINT tam = sizeof(info);
      if (GetRawInputDeviceInfoA(lista[i].hDevice, RIDI_DEVICEINFO, &info, &tam) < 0) {
        continue;
      }
      if (info.dwType != RIM_TYPEHID) {
        continue;
      }
      // Page 1, usage 4 (joystick) or 5 (game pad) is what SDL's Windows driver accepts.
      const bool mando = info.hid.usUsagePage == 0x01 &&
                         (info.hid.usUsage == 0x04 || info.hid.usUsage == 0x05);
      if (!mando) {
        continue;
      }
      mandos++;
      UINT largo = 0;
      GetRawInputDeviceInfoA(lista[i].hDevice, RIDI_DEVICENAME, nullptr, &largo);
      auto* nombre = static_cast<char*>(malloc(largo + 1));
      nombre[0] = 0;
      GetRawInputDeviceInfoA(lista[i].hDevice, RIDI_DEVICENAME, nombre, &largo);
      std::printf("%s  %s\n", nombre_uso(info.hid.usUsagePage, info.hid.usUsage), nombre);
      free(nombre);
    }
    std::printf("mandos que ve Raw Input: %d\n", mandos);
    free(lista);
  }

  std::printf("--- el mando manda algo? (%d s) ---\n", segundos);
  int cambios[XUSER_MAX_COUNT] = {};
  for (int s = 0; s < segundos * 4; s++) {
    for (DWORD i = 0; i < XUSER_MAX_COUNT; i++) {
      if (!hay[i]) {
        continue;
      }
      XINPUT_STATE e = {};
      if (XInputGetState(i, &e) == ERROR_SUCCESS && e.dwPacketNumber != paquete_inicial[i]) {
        cambios[i]++;
        paquete_inicial[i] = e.dwPacketNumber;
      }
    }
    Sleep(250);
  }
  for (DWORD i = 0; i < XUSER_MAX_COUNT; i++) {
    if (hay[i]) {
      std::printf("ranura %u: %d cambios de estado -> %s\n", i, cambios[i],
                  cambios[i] ? "el mando esta vivo" : "SIN cambios: quieto o inactivo");
    }
  }
  return 0;
}
