#pragma once
#include <string>

namespace platform {
void setConsoleUtf8();
bool replaceFileAtomic(const std::string& from, const std::string& to);
double currentProcessPrivateMemoryMB();
}  // namespace platform
