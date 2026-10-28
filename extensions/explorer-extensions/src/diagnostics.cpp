#include "diagnostics.h"
#include <shlobj.h>
#include <winternl.h>
#include <bit>

namespace ExplorerRuntime {
std::wstring DataDirectory() {
    PWSTR path = nullptr;
    HRESULT result = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &path);
    if (FAILED(result)) Fail(L"SHGetKnownFolderPath", static_cast<DWORD>(result));
    std::wstring directory = std::wstring(path) + L"\\ExplorerExtensions";
    CoTaskMemFree(path);
    int error = SHCreateDirectoryExW(nullptr, directory.c_str(), nullptr);
    if (error != ERROR_SUCCESS && error != ERROR_ALREADY_EXISTS && error != ERROR_FILE_EXISTS)
        Fail(L"SHCreateDirectoryExW", static_cast<DWORD>(error));
    return directory;
}
std::wstring LogDirectory() {
    auto directory = DataDirectory() + L"\\logs";
    if (!CreateDirectoryW(directory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
        Fail(L"CreateDirectoryW(logs)");
    return directory;
}
void Log(const wchar_t* event, DWORD code, const std::wstring& detail) noexcept {
    static SRWLOCK lock = SRWLOCK_INIT;
    AcquireSRWLockExclusive(&lock);
    try {
        SYSTEMTIME time{};
        GetSystemTime(&time);
        wchar_t prefix[256]{};
        swprintf_s(prefix, L"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ pid=%lu tid=%lu %ls code=%lu (0x%08lX) ",
                   time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond,
                   time.wMilliseconds, GetCurrentProcessId(), GetCurrentThreadId(), event, code, code);
        std::wstring line = std::wstring(prefix) + detail + L"\r\n";
        OutputDebugStringW(line.c_str());
        std::wstring file = LogDirectory() + L"\\" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                            std::to_wstring(ProcessStart(GetCurrentProcess())) + L".log";
        Handle output(CreateFileW(file.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (output) {
            int bytes = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()), nullptr, 0, nullptr, nullptr);
            if (bytes > 0) {
                std::string text(static_cast<size_t>(bytes), '\0');
                WideCharToMultiByte(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()), text.data(), bytes, nullptr, nullptr);
                DWORD written = 0;
                if (!WriteFile(output.get(), text.data(), static_cast<DWORD>(text.size()), &written, nullptr))
                    OutputDebugStringW(L"ExplorerExtensions: diagnostic WriteFile failed\n");
            }
        } else { OutputDebugStringW(L"ExplorerExtensions: diagnostic CreateFile failed\n"); }
    } catch (...) { OutputDebugStringW(L"ExplorerExtensions: diagnostic failure\n"); }
    ReleaseSRWLockExclusive(&lock);
}
void LogEnvironment() noexcept {
    try {
        using GetVersion = LONG (WINAPI*)(PRTL_OSVERSIONINFOW);
        auto getVersion = std::bit_cast<GetVersion>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
        RTL_OSVERSIONINFOW version{};
        version.dwOSVersionInfoSize = sizeof(version);
        LONG status = getVersion ? getVersion(&version) : static_cast<LONG>(0xC0000002);
        DWORD revision = 0, bytes = sizeof(revision);
        LSTATUS error = RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"UBR",
                                    RRF_RT_REG_DWORD, nullptr, &revision, &bytes);
        Log(L"os", static_cast<DWORD>(status), std::to_wstring(version.dwMajorVersion) + L"." +
            std::to_wstring(version.dwMinorVersion) + L"." + std::to_wstring(version.dwBuildNumber) + L"." +
            std::to_wstring(revision));
        if (error != ERROR_SUCCESS) Log(L"RegGetValueW(UBR)", error);
    } catch (...) { Log(L"os.exception", ERROR_UNHANDLED_EXCEPTION); }
}
void LogModule(HMODULE module) {
    auto path = ModulePath(module);
    DWORD unused = 0;
    DWORD size = GetFileVersionInfoSizeW(path.c_str(), &unused);
    if (!size) Fail(L"GetFileVersionInfoSizeW");
    std::vector<BYTE> data(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data())) Fail(L"GetFileVersionInfoW");
    VS_FIXEDFILEINFO* info = nullptr;
    UINT bytes = 0;
    if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &bytes) || bytes < sizeof(*info))
        Fail(L"VerQueryValueW", ERROR_INVALID_DATA);
    Log(L"module.twinui", 0, std::to_wstring(HIWORD(info->dwFileVersionMS)) + L"." +
        std::to_wstring(LOWORD(info->dwFileVersionMS)) + L"." + std::to_wstring(HIWORD(info->dwFileVersionLS)) + L"." +
        std::to_wstring(LOWORD(info->dwFileVersionLS)));
    struct Translation { WORD language, codepage; };
    Translation* translation = nullptr;
    if (VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&translation), &bytes) &&
        bytes >= sizeof(Translation)) {
        wchar_t query[96]{};
        swprintf_s(query, L"\\StringFileInfo\\%04x%04x\\FileVersion", translation->language, translation->codepage);
        wchar_t* version = nullptr;
        if (VerQueryValueW(data.data(), query, reinterpret_cast<void**>(&version), &bytes) && bytes && version)
            Log(L"module.twinui.resource_version", 0, version);
    }
}
}
