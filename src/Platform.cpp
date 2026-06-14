#include "Platform.h"

#include <cstdio>
#include <filesystem>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace platform {

void setConsoleUtf8() {
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);
#endif
}

bool replaceFileAtomic(const std::string& from, const std::string& to) {
#ifdef _WIN32
  return MoveFileExA(from.c_str(), to.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
  std::error_code ec;
  std::filesystem::rename(from, to, ec);
  if (!ec) return true;
  std::filesystem::remove(to, ec);
  ec.clear();
  std::filesystem::rename(from, to, ec);
  return !ec;
#endif
}

double currentProcessPrivateMemoryMB() {
#ifdef _WIN32
  PROCESS_MEMORY_COUNTERS_EX pmc;
  if (GetProcessMemoryInfo(GetCurrentProcess(),
                           reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                           sizeof(pmc))) {
    return static_cast<double>(pmc.PrivateUsage) / (1024.0 * 1024.0);
  }
  return 0.0;
#else
  struct rusage usage;
  if (getrusage(RUSAGE_SELF, &usage) == 0) {
#if defined(__APPLE__)
    return static_cast<double>(usage.ru_maxrss) / (1024.0 * 1024.0);
#else
    return static_cast<double>(usage.ru_maxrss) / 1024.0;
#endif
  }
  return 0.0;
#endif
}

}
