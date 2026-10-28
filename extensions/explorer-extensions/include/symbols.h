#pragma once
#include "runtime.h"
#include <atomic>
#include <cstdint>

namespace PopupSymbols {
enum class Signature : DWORD { Unknown = 0, Desktop = 1, DesktopAndSwitchType = 2 };
struct Identity { GUID guid{}; DWORD age = 0; DWORD imageSize = 0; };
struct Result {
    DWORD magic = 0x45585033;
    DWORD error = ERROR_NOT_FOUND;
    Identity identity{};
    DWORD rva = 0;
    Signature signature = Signature::Unknown;
};
static_assert(sizeof(Result) < 256);
Identity ReadIdentity(HMODULE module);
std::wstring IdentityKey(const Identity& identity);
bool SameIdentity(const Identity& left, const Identity& right);
bool ExecutableRva(HMODULE module, DWORD rva);
Result Resolve(); // Helper process only: owns its DbgHelp session.
Result ResolveInHelper(HMODULE ownModule, const std::atomic<bool>& desired,
                       const std::atomic<std::uint64_t>& generation, std::uint64_t expected);
}
