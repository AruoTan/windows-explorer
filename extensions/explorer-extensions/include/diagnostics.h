#pragma once
#include "runtime.h"

namespace ExplorerRuntime {
std::wstring DataDirectory();
std::wstring LogDirectory();
void Log(const wchar_t* event, DWORD code = 0, const std::wstring& detail = {}) noexcept;
void LogEnvironment() noexcept;
void LogModule(HMODULE module);
}
