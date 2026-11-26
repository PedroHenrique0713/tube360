#pragma once
#include <algorithm>
#include <cstdint>
#include <cmath>
#include "net.hpp"
#include "boot_log.h"
#ifdef __XSDK__
#include "xsdk/kernel.h"
#endif

// Só uma fotografia do servidor; o console nunca decide que o prazo venceu.
struct SleepState {
  bool active = false, end = false, notebook = false;
  double seconds = 0; uint64_t stamp = 0;
  bool parse(const std::string& text, uint64_t now) {
    auto f = net::split(text.substr(0, text.find('\n')), '\t');
    if (f.size() != 5 || f[0] != "ok" || (f[1] != "on" && f[1] != "off")) return false;
    char* tail = nullptr; double n = std::strtod(f[2].c_str(), &tail);
    if (f[2].empty() || *tail || !std::isfinite(n) || n < -1 ||
        (f[3] != "xbox" && f[3] != "xbox_notebook") || (f[4] != "minutos" && f[4] != "fim_do_video")) return false;
    active = f[1] == "on"; seconds = n; notebook = f[3] == "xbox_notebook"; end = f[4] == "fim_do_video"; stamp = now;
    return true;
  }
  double remaining(uint64_t now, bool paused = false) const { return paused || seconds < 0 ? seconds : std::max(0.0, seconds - (now - stamp) / 1000.0); }
};

static bool powerDownXbox() {
#ifdef __XSDK__
  // Ordinal: xenia-project/xenia, src/xenia/kernel/xboxkrnl/xboxkrnl_table.inc (0x28).
  // Rotina 5: github.com/ClementDreptin/XexUtils/blob/main/src/Kernel.h, FIRMWARE_REENTRY.
  HANDLE module = 0; void* address = nullptr;
  if (XexGetModuleHandle("xboxkrnl.exe", &module) != 0 ||
      !NT_SUCCESS(XexGetProcedureAddress(module, 0x28, &address)) || !address) {
    LOG("timer: não foi possível resolver HalReturnToFirmware"); return false;
  }
  constexpr uint32_t HalPowerDownRoutine = 5;
  LOG("timer: HalReturnToFirmware(HalPowerDownRoutine)");
  boot_session_clean();
  ((void (*)(uint32_t))address)(HalPowerDownRoutine);
  boot_session_resume();
  return false;  // só retorna se o kernel não desligou
#else
  LOG("timer: desligar recebido; build PC fecha o app sem desligar a máquina");
  return true;
#endif
}
