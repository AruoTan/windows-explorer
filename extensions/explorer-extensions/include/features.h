#pragma once
#include "runtime.h"

namespace DriveNavFilter {
DWORD InstallFilter();
void DisableFilter();
DWORD FilteredCount();
void RefreshTree();
}
namespace DesktopPopup {
void Request(bool enabled, HMODULE ownModule) noexcept;
void DeferUnload() noexcept;
}
