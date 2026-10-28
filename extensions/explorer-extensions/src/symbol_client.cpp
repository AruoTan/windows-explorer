#include "symbols.h"
#include "diagnostics.h"

namespace PopupSymbols {
using namespace ExplorerRuntime;
Result ResolveInHelper(HMODULE ownModule, const std::atomic<bool>& desired,
                       const std::atomic<std::uint64_t>& generation, std::uint64_t expected) {
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE rawRead = nullptr, rawWrite = nullptr;
    if (!CreatePipe(&rawRead, &rawWrite, &security, sizeof(Result))) Fail(L"CreatePipe(symbol helper)");
    Handle read(rawRead), write(rawWrite);
    if (!SetHandleInformation(read.get(), HANDLE_FLAG_INHERIT, 0)) Fail(L"SetHandleInformation");
    auto path = ModulePath(ownModule);
    path = path.substr(0, path.find_last_of(L"\\/")) + L"\\" + ExeName;
    std::wstring command = L"\"" + path + L"\" --resolve-popup " +
        std::to_wstring(reinterpret_cast<ULONG_PTR>(write.get())) + L" " + std::to_wstring(GetCurrentProcessId());
    SIZE_T bytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    std::vector<BYTE> storage(bytes);
    auto attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (!InitializeProcThreadAttributeList(attributes, 1, 0, &bytes)) Fail(L"InitializeProcThreadAttributeList");
    struct Cleanup { LPPROC_THREAD_ATTRIBUTE_LIST value; ~Cleanup() { DeleteProcThreadAttributeList(value); } } cleanup{attributes};
    HANDLE inherited = write.get();
    if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &inherited, sizeof(inherited), nullptr, nullptr))
        Fail(L"UpdateProcThreadAttribute");
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.lpAttributeList = attributes;
    PROCESS_INFORMATION child{};
    if (!CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, TRUE,
                        EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW, nullptr, nullptr, &startup.StartupInfo, &child))
        Fail(L"CreateProcessW(symbol helper)");
    Handle process(child.hProcess), thread(child.hThread);
    Log(L"symbols.helper.started", 0, L"helper_pid=" + std::to_wstring(child.dwProcessId));
    ULONGLONG deadline = GetTickCount64() + 180000;
    for (;;) {
        DWORD wait = WaitForSingleObject(process.get(), 100);
        if (wait == WAIT_OBJECT_0) break;
        DWORD failure = 0;
        if (wait == WAIT_FAILED) failure = GetLastError();
        else if (!desired.load() || generation.load() != expected) failure = ERROR_CANCELLED;
        else if (GetTickCount64() > deadline) failure = ERROR_TIMEOUT;
        if (failure) {
            // Only our disposable helper is terminated; no Explorer thread is stopped.
            if (!TerminateProcess(process.get(), failure)) Log(L"TerminateProcess(symbol helper)", GetLastError());
            Log(L"symbols.helper.cancelled", failure);
            Fail(L"Symbol helper cancelled or timed out", failure);
        }
    }
    DWORD exitCode = 0;
    if (!GetExitCodeProcess(process.get(), &exitCode)) Fail(L"GetExitCodeProcess(symbol helper)");
    if (exitCode) Fail(L"Symbol helper failed", exitCode);
    DWORD available = 0;
    if (!PeekNamedPipe(read.get(), nullptr, 0, nullptr, &available, nullptr)) Fail(L"PeekNamedPipe");
    if (available != sizeof(Result)) Fail(L"Invalid symbol helper response", ERROR_INVALID_DATA);
    Result result{};
    DWORD received = 0;
    if (!ReadFile(read.get(), &result, sizeof(result), &received, nullptr)) Fail(L"ReadFile(symbol result)");
    if (received != sizeof(result) || result.magic != 0x45585033) Fail(L"Invalid symbol response header", ERROR_INVALID_DATA);
    if (result.error) Fail(L"Symbols unavailable; feature inactive", result.error);
    return result;
}
}
