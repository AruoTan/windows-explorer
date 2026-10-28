#pragma once
#include <windows.h>
#include <objbase.h>
#include <cstdint>
#include <cwchar>
#include "drive-path.h"

namespace DriveNavFilter {
struct NativeRegistryKey {
    HKEY value = nullptr;
    NativeRegistryKey() = default;
    NativeRegistryKey(const NativeRegistryKey&) = delete;
    ~NativeRegistryKey() { if (value) RegCloseKey(value); }
    bool Open(HKEY parent, const wchar_t* path) {
        return RegOpenKeyExW(parent, path, 0, KEY_READ, &value) == ERROR_SUCCESS;
    }
};
template <size_t N>
inline bool ReadNativeString(HKEY key, const wchar_t* name, wchar_t (&value)[N]) {
    DWORD bytes = sizeof(value);
    return RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, nullptr, value, &bytes) == ERROR_SUCCESS &&
           bytes >= sizeof(wchar_t);
}
inline int RegisteredAliasDrive(HKEY classesRoot, const wchar_t* name) {
    if (wcslen(name) != 38 || name[0] != L'{' || name[37] != L'}') return -1;
    GUID guid{};
    if (FAILED(CLSIDFromString(name, &guid)) || IsEqualGUID(guid, GUID_NULL)) return -1;
    wchar_t canonical[40]{};
    if (!StringFromGUID2(guid, canonical, 40)) return -1;
    wchar_t path[64]{};
    swprintf_s(path, L"CLSID\\%ls", canonical);
    NativeRegistryKey alias, instance, bag;
    if (!alias.Open(classesRoot, path)) return -1;
    DWORD pinned = 0, bytes = sizeof(pinned);
    if (RegGetValueW(alias.value, nullptr, L"System.IsPinnedToNameSpaceTree", RRF_RT_REG_DWORD,
                     nullptr, &pinned, &bytes) != ERROR_SUCCESS || pinned != 1) return -1;
    if (!instance.Open(alias.value, L"Instance") || !bag.Open(instance.value, L"InitPropertyBag")) return -1;
    wchar_t type[40]{}, target[8]{};
    // The Windows Folder Shortcut class exposes TargetFolderPath through its
    // standard property bag. Unknown shell extensions remain untouched.
    if (!ReadNativeString(instance.value, L"CLSID", type) ||
        _wcsicmp(type, L"{0E5AAE11-A475-4C5B-AB00-C66DE400274E}") != 0 ||
        !ReadNativeString(bag.value, L"TargetFolderPath", target)) return -1;
    return DriveIndex(target);
}
inline std::uint32_t RegisteredDriveMask(HKEY namespaceRoot, HKEY classesRoot) {
    std::uint32_t mask = 0;
    for (DWORD index = 0;; ++index) {
        wchar_t name[64]{};
        DWORD length = 64;
        LSTATUS result = RegEnumKeyExW(namespaceRoot, index, name, &length, nullptr, nullptr, nullptr, nullptr);
        if (result == ERROR_NO_MORE_ITEMS) break;
        if (result == ERROR_MORE_DATA) continue;
        if (result != ERROR_SUCCESS) break;
        int drive = RegisteredAliasDrive(classesRoot, name);
        if (drive >= 0) mask |= std::uint32_t{1} << drive;
    }
    return mask;
}
}
