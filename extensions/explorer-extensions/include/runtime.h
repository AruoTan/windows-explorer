#pragma once
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <sddl.h>
#include <string>
#include <vector>

namespace ExplorerRuntime {
inline constexpr wchar_t ControlPath[] = L"Software\\Nilesoft\\Shell\\ExplorerExtensions";
inline constexpr wchar_t LegacyControlPath[] = L"Software\\Nilesoft\\Shell\\DriveNavigationFilter";
inline constexpr wchar_t DriveSetting[] = L"DriveNavigationFilterEnabled";
inline constexpr wchar_t PopupSetting[] = L"DisableVirtualDesktopPopups";
inline constexpr wchar_t RunPath[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
inline constexpr wchar_t RunName[] = L"NilesoftExplorerExtensions";
inline constexpr wchar_t LegacyRunName[] = L"NilesoftDriveNavigationFilter";
inline constexpr wchar_t DllName[] = L"ExplorerExtensions.dll";
inline constexpr wchar_t ExeName[] = L"ExplorerExtensions.exe";
inline constexpr wchar_t Version[] = L"0.3.0";

struct Failure { DWORD code; std::wstring message; };
[[noreturn]] inline void Fail(const wchar_t* message, DWORD code = GetLastError()) {
    throw Failure{code, message};
}
class Handle {
    HANDLE value_;
public:
    explicit Handle(HANDLE value = nullptr) : value_(value) {}
    ~Handle() { if (value_ && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE get() const { return value_; }
    explicit operator bool() const { return value_ && value_ != INVALID_HANDLE_VALUE; }
};
struct Key {
    HKEY value = nullptr;
    ~Key() { if (value) RegCloseKey(value); }
    Key() = default;
    Key(const Key&) = delete;
    bool Open(const wchar_t* path, REGSAM access = KEY_READ) {
        return RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, access, &value) == ERROR_SUCCESS;
    }
    void Create(const wchar_t* path) {
        LSTATUS error = RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, nullptr, 0,
                                        KEY_READ | KEY_WRITE, nullptr, &value, nullptr);
        if (error != ERROR_SUCCESS) Fail(L"Cannot save current-user settings", error);
    }
};
inline void SetDword(HKEY key, const wchar_t* name, DWORD value) {
    LSTATUS error = RegSetValueExW(key, name, 0, REG_DWORD,
                                  reinterpret_cast<const BYTE*>(&value), sizeof(value));
    if (error != ERROR_SUCCESS) Fail(L"Cannot write registry value", error);
}
inline void SetString(HKEY key, const wchar_t* name, const std::wstring& value) {
    LSTATUS error = RegSetValueExW(key, name, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(value.c_str()), static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    if (error != ERROR_SUCCESS) Fail(L"Cannot write registry value", error);
}
struct Settings { bool drive = false; bool popup = false; bool Any() const { return drive || popup; } };
inline bool ReadBool(const wchar_t* path, const wchar_t* name, bool fallback = false) {
    DWORD value = 0, bytes = sizeof(value);
    LSTATUS result = RegGetValueW(HKEY_CURRENT_USER, path, name, RRF_RT_REG_DWORD,
                                 nullptr, &value, &bytes);
    return result == ERROR_SUCCESS ? value == 1 : fallback;
}
inline Settings ReadSettings(bool includeLegacy = false) {
    return {ReadBool(ControlPath, DriveSetting, includeLegacy && ReadBool(LegacyControlPath, L"Enabled")),
            ReadBool(ControlPath, PopupSetting)};
}
inline bool ControlEnabled() {
    return ReadSettings().Any();
}
inline std::wstring ModulePath(HMODULE module = nullptr) {
    std::vector<wchar_t> path(32768);
    DWORD length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length == path.size()) Fail(L"Cannot locate the extension files");
    return std::wstring(path.data(), length);
}
inline std::wstring ExecutableDirectory() {
    std::wstring path = ModulePath();
    return path.substr(0, path.find_last_of(L"\\/"));
}
inline std::wstring ProcessSid(HANDLE process) {
    HANDLE raw = nullptr;
    if (!OpenProcessToken(process, TOKEN_QUERY, &raw)) return {};
    Handle token(raw);
    DWORD bytes = 0;
    GetTokenInformation(token.get(), TokenUser, nullptr, 0, &bytes);
    if (!bytes) return {};
    std::vector<BYTE> data(bytes);
    if (!GetTokenInformation(token.get(), TokenUser, data.data(), bytes, &bytes)) return {};
    LPWSTR sid = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid, &sid)) return {};
    std::wstring result(sid);
    LocalFree(sid);
    return result;
}
inline std::wstring ObjectName(const wchar_t* type) {
    std::wstring sid = ProcessSid(GetCurrentProcess());
    if (sid.empty()) Fail(L"Cannot identify the current user", ERROR_ACCESS_DENIED);
    return std::wstring(L"Local\\NilesoftShell.ExplorerExtensions.") + type + L"." + sid;
}
class SettingsLock {
    Handle mutex_;
    bool owned_ = false;
public:
    SettingsLock() : mutex_(CreateMutexW(nullptr, FALSE, ObjectName(L"Settings").c_str())) {
        if (!mutex_) Fail(L"Cannot create settings mutex");
        DWORD wait = WaitForSingleObject(mutex_.get(), 15000);
        owned_ = wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED;
        if (!owned_) Fail(L"Another enable/disable operation is still running", ERROR_TIMEOUT);
    }
    ~SettingsLock() { if (owned_) ReleaseMutex(mutex_.get()); }
};
class ValueSnapshot {
    HKEY key_;
    const wchar_t* name_;
    bool exists_ = false;
    DWORD type_ = 0;
    std::vector<BYTE> data_;
public:
    ValueSnapshot(HKEY key, const wchar_t* name) : key_(key), name_(name) {
        DWORD bytes = 0;
        LSTATUS error = RegQueryValueExW(key_, name_, nullptr, &type_, nullptr, &bytes);
        if (error == ERROR_FILE_NOT_FOUND) return;
        if (error != ERROR_SUCCESS) Fail(L"Cannot read previous startup settings", error);
        data_.resize(bytes);
        error = RegQueryValueExW(key_, name_, nullptr, &type_, data_.data(), &bytes);
        if (error != ERROR_SUCCESS) Fail(L"Cannot read previous startup settings", error);
        data_.resize(bytes);
        exists_ = true;
    }
    void Restore() const {
        ValueSnapshot current(key_, name_);
        if (current.exists_ == exists_ && (!exists_ || (current.type_ == type_ && current.data_ == data_))) return;
        LSTATUS error = exists_ ? RegSetValueExW(key_, name_, 0, type_, data_.data(), static_cast<DWORD>(data_.size())) :
                                 RegDeleteValueW(key_, name_);
        if (error != ERROR_SUCCESS && error != ERROR_FILE_NOT_FOUND) Fail(L"Cannot restore previous startup settings", error);
    }
};
inline ULONGLONG ProcessStart(HANDLE process) {
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(process, &creation, &exit, &kernel, &user)) return 0;
    return (static_cast<ULONGLONG>(creation.dwHighDateTime) << 32) | creation.dwLowDateTime;
}
inline bool IsExplorer(HANDLE process, DWORD pid, const std::wstring& sid) {
    DWORD ownSession = 0, targetSession = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &ownSession) ||
        !ProcessIdToSessionId(pid, &targetSession) || ownSession != targetSession) return false;
    wchar_t image[32768]{}, windows[MAX_PATH]{};
    DWORD length = 32768;
    if (!QueryFullProcessImageNameW(process, 0, image, &length) ||
        !GetWindowsDirectoryW(windows, MAX_PATH)) return false;
    std::wstring expected = std::wstring(windows) + L"\\explorer.exe";
    return _wcsicmp(image, expected.c_str()) == 0 && ProcessSid(process) == sid;
}
inline std::wstring StatusPath(DWORD pid) {
    return std::wstring(ControlPath) + L"\\Processes\\" + std::to_wstring(pid);
}
inline void PublishStatus(const wchar_t* feature, const wchar_t* state, DWORD error, DWORD filtered = 0) noexcept {
    try {
        Key key;
        key.Create(StatusPath(GetCurrentProcessId()).c_str());
        SetString(key.value, (std::wstring(feature) + L"State").c_str(), state);
        SetDword(key.value, (std::wstring(feature) + L"Error").c_str(), error);
        SetDword(key.value, L"FilteredEnumerations", filtered);
        ULONGLONG start = ProcessStart(GetCurrentProcess());
        RegSetValueExW(key.value, L"ProcessStart", 0, REG_QWORD,
                      reinterpret_cast<const BYTE*>(&start), sizeof(start));
    } catch (...) { OutputDebugStringW(L"ExplorerExtensions: unable to publish status\n"); }
}
}
