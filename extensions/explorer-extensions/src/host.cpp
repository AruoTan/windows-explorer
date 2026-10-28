#include "runtime.h"
#include "diagnostics.h"
#include "symbols.h"
#include <shellapi.h>
#include <bit>
#include <map>
#include <set>
#include <exception>

using namespace ExplorerRuntime;
namespace {
constexpr int DriveBox = 101, PopupBox = 102;
constexpr wchar_t Title[] = L"Explorer Extensions";
struct Module {
    HMODULE value = nullptr;
    ~Module() { if (value && !FreeLibrary(value)) Log(L"FreeLibrary(host copy)", GetLastError()); }
};
struct Hooks {
    struct Entry { HHOOK hook; DWORD pid; ULONGLONG start; };
    std::map<DWORD, Entry> values;
    std::set<DWORD> denied;
    void Clear() {
        for (auto& [thread, entry] : values) {
            (void)thread;
            if (!UnhookWindowsHookEx(entry.hook)) Log(L"UnhookWindowsHookEx", GetLastError());
        }
        values.clear();
    }
    ~Hooks() { Clear(); }
};
struct Candidate { DWORD thread; DWORD pid; };
BOOL CALLBACK FindExplorerWindow(HWND window, LPARAM data) {
    wchar_t name[64]{};
    if (!GetClassNameW(window, name, 64)) return TRUE;
    if (_wcsicmp(name, L"CabinetWClass") && _wcsicmp(name, L"ExploreWClass") &&
        _wcsicmp(name, L"Shell_TrayWnd") && _wcsicmp(name, L"Progman")) return TRUE;
    DWORD pid = 0;
    DWORD thread = GetWindowThreadProcessId(window, &pid);
    if (thread && pid) {
        try { reinterpret_cast<std::vector<Candidate>*>(data)->push_back({thread, pid}); }
        catch (...) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return FALSE; }
    }
    return TRUE;
}
void PollExplorer(Hooks& hooks, HMODULE module, HOOKPROC callback, const std::wstring& sid) {
    std::vector<Candidate> candidates;
    if (!EnumWindows(FindExplorerWindow, reinterpret_cast<LPARAM>(&candidates))) Fail(L"EnumWindows");
    std::set<DWORD> found;
    for (auto candidate : candidates) {
        if (found.contains(candidate.thread)) continue;
        found.insert(candidate.thread);
        Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, candidate.pid));
        if (!process) {
            DWORD error = GetLastError();
            if (hooks.denied.insert(candidate.thread).second)
                Log(L"OpenProcess(explorer); no elevation retry", error, L"target_pid=" + std::to_wstring(candidate.pid));
            continue;
        }
        if (!IsExplorer(process.get(), candidate.pid, sid)) continue;
        BOOL wow64 = FALSE;
        if (!IsWow64Process(process.get(), &wow64)) { Log(L"IsWow64Process", GetLastError()); continue; }
        if (wow64) continue;
        ULONGLONG start = ProcessStart(process.get());
        if (!start) { Log(L"GetProcessTimes(explorer)", GetLastError()); continue; }
        auto old = hooks.values.find(candidate.thread);
        if (old != hooks.values.end() && (old->second.pid != candidate.pid || old->second.start != start)) {
            if (!UnhookWindowsHookEx(old->second.hook)) Log(L"UnhookWindowsHookEx(stale)", GetLastError());
            hooks.values.erase(old);
            old = hooks.values.end();
            hooks.denied.erase(candidate.thread);
        }
        if (old == hooks.values.end()) {
            if (hooks.denied.contains(candidate.thread)) continue;
            HHOOK hook = SetWindowsHookExW(WH_GETMESSAGE, callback, module, candidate.thread);
            if (!hook) {
                Log(L"SetWindowsHookExW; no elevation retry", GetLastError(), L"target_pid=" + std::to_wstring(candidate.pid));
                hooks.denied.insert(candidate.thread);
                continue;
            }
            hooks.values.emplace(candidate.thread, Hooks::Entry{hook, candidate.pid, start});
            Log(L"loader.attached", 0, L"target_pid=" + std::to_wstring(candidate.pid));
        }
        if (!PostThreadMessageW(candidate.thread, WM_NULL, 0, 0)) Log(L"PostThreadMessageW", GetLastError());
    }
    for (auto it = hooks.values.begin(); it != hooks.values.end();) {
        if (!found.contains(it->first)) {
            if (!UnhookWindowsHookEx(it->second.hook)) Log(L"UnhookWindowsHookEx(gone)", GetLastError());
            it = hooks.values.erase(it);
        } else ++it;
    }
    for (auto it = hooks.denied.begin(); it != hooks.denied.end();) {
        if (!found.contains(*it)) it = hooks.denied.erase(it); else ++it;
    }
}
void CheckPlatform() {
    SYSTEM_INFO system{};
    GetNativeSystemInfo(&system);
    if (system.wProcessorArchitecture != PROCESSOR_ARCHITECTURE_AMD64 || sizeof(void*) != 8)
        Fail(L"Windows x64 (AMD64) is required", ERROR_NOT_SUPPORTED);
}
void LoadExtension(Module& module) {
    auto path = ExecutableDirectory() + L"\\" + DllName;
    module.value = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module.value) Fail(L"LoadLibraryExW(ExplorerExtensions.dll)");
    if (!GetProcAddress(module.value, "ExplorerExtensionsGetMessageHook")) Fail(L"Incompatible DLL", ERROR_BAD_EXE_FORMAT);
}
int Background() {
    Handle singleton(CreateMutexW(nullptr, FALSE, ObjectName(L"Loader").c_str()));
    if (!singleton) Fail(L"CreateMutexW(loader)");
    DWORD locked = WaitForSingleObject(singleton.get(), 0);
    if (locked == WAIT_TIMEOUT) return 0;
    if (locked != WAIT_OBJECT_0 && locked != WAIT_ABANDONED) Fail(L"WaitForSingleObject(loader)");
    Module module;
    LoadExtension(module);
    auto callback = std::bit_cast<HOOKPROC>(GetProcAddress(module.value, "ExplorerExtensionsGetMessageHook"));
    auto sid = ProcessSid(GetCurrentProcess());
    if (sid.empty()) Fail(L"Cannot identify user", ERROR_ACCESS_DENIED);
    Hooks hooks;
    for (;;) {
        {
            SettingsLock lock;
            if (!ReadSettings().Any()) {
                hooks.Clear();
                Log(L"loader.stopped");
                // Release the singleton while holding the settings lock: a new
                // configuration cannot race the final decision to exit.
                if (!ReleaseMutex(singleton.get())) Log(L"ReleaseMutex(loader)", GetLastError());
                return 0;
            }
        }
        PollExplorer(hooks, module.value, callback, sid);
        DWORD result = MsgWaitForMultipleObjects(0, nullptr, FALSE, 250, QS_ALLINPUT);
        if (result == WAIT_FAILED) Fail(L"MsgWaitForMultipleObjects");
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) return 0;
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
}
void MigrateLegacy() {
    if (ReadBool(LegacyControlPath, L"Enabled")) {
        Key legacy; legacy.Create(LegacyControlPath); SetDword(legacy.value, L"Enabled", 0);
    }
    Key run;
    if (run.Open(RunPath, KEY_SET_VALUE)) {
        LSTATUS error = RegDeleteValueW(run.value, LegacyRunName);
        if (error != ERROR_SUCCESS && error != ERROR_FILE_NOT_FOUND) Fail(L"Remove legacy startup", error);
    }
    auto sid = ProcessSid(GetCurrentProcess());
    auto prefix = std::wstring(L"Local\\NilesoftShell.DriveNavigationFilter.");
    Handle stop(OpenEventW(EVENT_MODIFY_STATE, FALSE, (prefix + L"Stop." + sid).c_str()));
    if (stop && !SetEvent(stop.get())) Fail(L"SetEvent(legacy loader)");
    Handle old(OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, (prefix + L"Loader." + sid).c_str()));
    if (old) {
        DWORD wait = WaitForSingleObject(old.get(), 10000);
        if (wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED) ReleaseMutex(old.get());
        else Fail(L"Legacy loader did not stop; settings retained; restart Explorer before retry", wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError());
    }
    Log(L"legacy.retired", 0, L"legacy code may remain pinned until Explorer exits");
}
void LaunchBackground() {
    auto path = ModulePath();
    std::wstring command = L"\"" + path + L"\" --background";
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION child{};
    if (!CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_UNICODE_ENVIRONMENT,
                        nullptr, ExecutableDirectory().c_str(), &startup, &child)) Fail(L"CreateProcessW(loader)");
    Handle process(child.hProcess), thread(child.hThread);
    if (WaitForSingleObject(process.get(), 500) == WAIT_OBJECT_0) {
        DWORD code = 0;
        if (!GetExitCodeProcess(process.get(), &code)) Fail(L"GetExitCodeProcess(loader)");
        if (code) Fail(L"Loader initialization failed", code);
    }
}
void WaitDisabled(const Settings& settings) {
    ULONGLONG deadline = GetTickCount64() + 10000;
    for (;;) {
        {
            SettingsLock lock;
            auto current = ReadSettings();
            if (current.drive != settings.drive || current.popup != settings.popup) {
                Log(L"settings.superseded");
                return;
            }
        }
        bool pending = false;
        Key statuses;
        if (statuses.Open((std::wstring(ControlPath) + L"\\Processes").c_str())) {
            auto sid = ProcessSid(GetCurrentProcess());
            for (DWORD index = 0;; ++index) {
                wchar_t name[32]{}; DWORD length = 32;
                LSTATUS error = RegEnumKeyExW(statuses.value, index, name, &length, nullptr, nullptr, nullptr, nullptr);
                if (error == ERROR_NO_MORE_ITEMS) break;
                if (error != ERROR_SUCCESS) continue;
                DWORD pid = wcstoul(name, nullptr, 10);
                Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
                if (!process || !IsExplorer(process.get(), pid, sid)) continue;
                Key key; if (!key.Open(StatusPath(pid).c_str())) continue;
                ULONGLONG start = 0; DWORD bytes = sizeof(start);
                if (RegGetValueW(key.value, nullptr, L"ProcessStart", RRF_RT_REG_QWORD, nullptr, &start, &bytes) != ERROR_SUCCESS ||
                    start != ProcessStart(process.get())) continue;
                for (const auto* feature : {L"Drive", L"Popup"}) {
                    if ((feature[0] == L'D' && settings.drive) || (feature[0] == L'P' && settings.popup)) continue;
                    wchar_t state[64]{}; bytes = sizeof(state);
                    auto value = std::wstring(feature) + L"State";
                    if (RegGetValueW(key.value, nullptr, value.c_str(), RRF_RT_REG_SZ, nullptr, state, &bytes) != ERROR_SUCCESS) continue;
                    if (wcscmp(state, L"Disabled")) pending = true;
                }
            }
        }
        if (!pending) return;
        if (GetTickCount64() >= deadline) Fail(L"Settings saved; Explorer has not acknowledged disable", ERROR_TIMEOUT);
        Sleep(50);
    }
}
void Configure(Settings settings, const wchar_t* feature = nullptr, bool enabled = false) {
    Module check;
    if (settings.Any() || (feature && enabled)) LoadExtension(check);
    {
        SettingsLock lock;
        if (feature) {
            settings = ReadSettings(true);
            if (wcscmp(feature, L"drive-navigation") == 0) settings.drive = enabled;
            else if (wcscmp(feature, L"virtual-desktop-popups") == 0) settings.popup = enabled;
            else Fail(L"Unknown feature", ERROR_INVALID_PARAMETER);
        }
        MigrateLegacy();
        Key control, run;
        control.Create(ControlPath); run.Create(RunPath);
        ValueSnapshot oldDrive(control.value, DriveSetting), oldPopup(control.value, PopupSetting), oldRun(run.value, RunName);
        try {
            SetDword(control.value, DriveSetting, settings.drive ? 1 : 0);
            SetDword(control.value, PopupSetting, settings.popup ? 1 : 0);
            if (settings.Any()) SetString(run.value, RunName, L"\"" + ModulePath() + L"\" --background");
            else {
                LSTATUS error = RegDeleteValueW(run.value, RunName);
                if (error != ERROR_SUCCESS && error != ERROR_FILE_NOT_FOUND) Fail(L"RegDeleteValueW(startup)", error);
            }
        } catch (...) {
            auto original = std::current_exception();
            try { oldDrive.Restore(); oldPopup.Restore(); oldRun.Restore(); }
            catch (...) { Fail(L"Settings rollback failed", ERROR_WRITE_FAULT); }
            std::rethrow_exception(original);
        }
    }
    Log(L"settings.saved", 0, std::wstring(L"drive=") + (settings.drive ? L"1" : L"0") + L" popup=" + (settings.popup ? L"1" : L"0"));
    if (settings.Any()) LaunchBackground();
    WaitDisabled(settings);
    if (!settings.Any()) {
        Handle loader(OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, ObjectName(L"Loader").c_str()));
        if (loader) {
            ULONGLONG deadline = GetTickCount64() + 10000;
            for (;;) {
                DWORD wait = WaitForSingleObject(loader.get(), 50);
                if (wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED) { ReleaseMutex(loader.get()); break; }
                if (wait == WAIT_FAILED) Fail(L"WaitForSingleObject(loader shutdown)");
                { SettingsLock lock; if (ReadSettings().Any()) break; }
                if (GetTickCount64() >= deadline) Fail(L"Loader did not stop", ERROR_TIMEOUT);
            }
        }
    }
}
void ShowStatus() {
    Settings settings;
    { SettingsLock lock; settings = ReadSettings(true); }
    std::wstring text = std::wstring(L"Explorer Extensions ") + Version +
        L"\n驱动器导航过滤：" + (settings.drive ? L"已请求启用" : L"已关闭") +
        L"\n关闭虚拟桌面名称提示：" + (settings.popup ? L"已请求启用" : L"已关闭") + L"\n";
    Key statuses;
    if (statuses.Open((std::wstring(ControlPath) + L"\\Processes").c_str())) {
        auto sid = ProcessSid(GetCurrentProcess());
        for (DWORD index = 0;; ++index) {
            wchar_t name[32]{}; DWORD length = 32;
            LSTATUS error = RegEnumKeyExW(statuses.value, index, name, &length, nullptr, nullptr, nullptr, nullptr);
            if (error == ERROR_NO_MORE_ITEMS) break;
            if (error != ERROR_SUCCESS) continue;
            DWORD pid = wcstoul(name, nullptr, 10);
            Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
            if (!process || !IsExplorer(process.get(), pid, sid)) continue;
            Key key; if (!key.Open(StatusPath(pid).c_str())) continue;
            ULONGLONG start = 0; DWORD bytes = sizeof(start);
            if (RegGetValueW(key.value, nullptr, L"ProcessStart", RRF_RT_REG_QWORD, nullptr, &start, &bytes) != ERROR_SUCCESS || start != ProcessStart(process.get())) continue;
            text += L"\nExplorer PID " + std::to_wstring(pid);
            for (const auto* feature : {L"Drive", L"Popup"}) {
                wchar_t state[64]{}; bytes = sizeof(state);
                auto value = std::wstring(feature) + L"State";
                RegGetValueW(key.value, nullptr, value.c_str(), RRF_RT_REG_SZ, nullptr, state, &bytes);
                DWORD code = 0; bytes = sizeof(code); value = std::wstring(feature) + L"Error";
                RegGetValueW(key.value, nullptr, value.c_str(), RRF_RT_REG_DWORD, nullptr, &code, &bytes);
                text += L"\n" + std::wstring(feature) + L": " + state + L" (" + std::to_wstring(code) + L")";
            }
        }
    }
    text += L"\n\n日志：" + LogDirectory() + L"\n停用后 DLL 保留到 Explorer 退出。";
    MessageBoxW(nullptr, text.c_str(), Title, MB_OK | MB_ICONINFORMATION);
}
INT_PTR CALLBACK Dialog(HWND window, UINT message, WPARAM w, LPARAM) {
    try {
        if (message == WM_INITDIALOG) {
            SettingsLock lock; auto settings = ReadSettings(true);
            CheckDlgButton(window, DriveBox, settings.drive ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(window, PopupBox, settings.popup ? BST_CHECKED : BST_UNCHECKED);
            return TRUE;
        }
        if (message == WM_CLOSE) { EndDialog(window, 0); return TRUE; }
        if (message != WM_COMMAND) return FALSE;
        switch (LOWORD(w)) {
        case IDCANCEL: EndDialog(window, 0); break;
        case IDOK:
            Configure({IsDlgButtonChecked(window, DriveBox) == BST_CHECKED, IsDlgButtonChecked(window, PopupBox) == BST_CHECKED});
            MessageBoxW(window, L"配置已保存。启用状态和符号准备结果请查看“状态”。", Title, MB_OK); break;
        case 103: Configure({}); CheckDlgButton(window, DriveBox, BST_UNCHECKED); CheckDlgButton(window, PopupBox, BST_UNCHECKED); break;
        case 104: ShowStatus(); break;
        case 105: {
            auto path = LogDirectory();
            auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(window, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
            if (result <= 32) Fail(L"ShellExecuteW(log directory)", static_cast<DWORD>(result));
            break;
        }
        default: return FALSE;
        }
        return TRUE;
    } catch (const Failure& error) {
        Log(L"ui.failed", error.code, error.message);
        MessageBoxW(window, (error.message + L"\nError: " + std::to_wstring(error.code)).c_str(), Title, MB_OK | MB_ICONERROR);
    } catch (...) { MessageBoxW(window, L"未预期的错误，请查看日志。", Title, MB_OK | MB_ICONERROR); }
    return TRUE;
}
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    bool quiet = false;
    try {
        CheckPlatform();
        int count = 0;
        LPWSTR* raw = CommandLineToArgvW(GetCommandLineW(), &count);
        if (!raw) Fail(L"CommandLineToArgvW");
        std::vector<std::wstring> args(raw, raw + count);
        LocalFree(raw);
        auto action = count > 1 ? args[1] : L"";
        quiet = count > 1;
        if (action == L"--resolve-popup" && count == 4) {
            HANDLE output = reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(wcstoull(args[2].c_str(), nullptr, 10)));
            if (GetFileType(output) != FILE_TYPE_PIPE) Fail(L"Invalid helper pipe", ERROR_INVALID_HANDLE);
            Log(L"symbols.helper.target", 0, L"target_pid=" + args[3]);
            auto result = PopupSymbols::Resolve();
            DWORD written = 0;
            if (!WriteFile(output, &result, sizeof(result), &written, nullptr) || written != sizeof(result)) Fail(L"WriteFile(symbol result)");
            return 0;
        }
        if (action == L"--diagnose" && count == 2) {
            auto result = PopupSymbols::Resolve();
            Log(L"diagnose.completed", result.error, PopupSymbols::IdentityKey(result.identity));
            return result.error ? 1 : 0;
        }
        if (action == L"--background" && count == 2) { LogEnvironment(); return Background(); }
        if (action == L"--status" && count == 2) { ShowStatus(); return 0; }
        if (count == 1) {
            INT_PTR result = DialogBoxParamW(instance, MAKEINTRESOURCEW(1), nullptr, Dialog, 0);
            if (result == -1) Fail(L"DialogBoxParamW");
            return 0;
        }
        if ((action == L"--enable" || action == L"--disable") && (count == 3 || (action == L"--disable" && count == 2))) {
            Configure({}, count == 3 ? args[2].c_str() : nullptr, action == L"--enable");
            return 0;
        }
        Fail(L"Use --enable/--disable drive-navigation|virtual-desktop-popups, --disable, --status or --diagnose", ERROR_INVALID_PARAMETER);
    } catch (const Failure& error) {
        Log(L"host.failed", error.code, error.message);
        if (!quiet) MessageBoxW(nullptr, (error.message + L"\nError: " + std::to_wstring(error.code)).c_str(), Title, MB_OK | MB_ICONERROR);
        return 1;
    } catch (...) {
        Log(L"host.exception", ERROR_UNHANDLED_EXCEPTION);
        if (!quiet) MessageBoxW(nullptr, L"未预期的错误，请查看日志。", Title, MB_OK | MB_ICONERROR);
        return 1;
    }
}
