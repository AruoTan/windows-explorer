#include "runtime.h"
#include "features.h"
#include "diagnostics.h"
#include <atomic>

namespace {
using namespace ExplorerRuntime;
std::atomic<bool> running{false};
HMODULE ownModule = nullptr;
DWORD WINAPI Coordinator(void*) noexcept {
    bool com = false;
    HANDLE owner = nullptr;
    struct ReleaseOwner {
        HANDLE* value;
        ~ReleaseOwner() { if (*value) { ReleaseMutex(*value); CloseHandle(*value); } }
    } release{&owner};
    try {
        auto sid = ProcessSid(GetCurrentProcess());
        if (sid.empty() || !IsExplorer(GetCurrentProcess(), GetCurrentProcessId(), sid)) {
            running.store(false);
            return 0;
        }
        auto name = ObjectName(L"Host") + L"." + std::to_wstring(GetCurrentProcessId()) + L"." +
                    std::to_wstring(ProcessStart(GetCurrentProcess()));
        HANDLE candidate = CreateMutexW(nullptr, FALSE, name.c_str());
        if (!candidate) Fail(L"CreateMutexW(process host)");
        DWORD wait = WaitForSingleObject(candidate, 0);
        if (wait == WAIT_TIMEOUT) {
            CloseHandle(candidate);
            Log(L"host.duplicate.mapping.skipped", ERROR_ALREADY_EXISTS, L"another host mapping owns this Explorer; restart Explorer when moving/updating files");
            running.store(false);
            return 0;
        }
        if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) {
            DWORD error = GetLastError(); CloseHandle(candidate); Fail(L"WaitForSingleObject(process host)", error);
        }
        owner = candidate;
        LogEnvironment();
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) Fail(L"CoInitializeEx", static_cast<DWORD>(hr));
        com = SUCCEEDED(hr);
        bool lastDrive = false, driveAttempted = false;
        bool driveReady = false;
        DWORD published = DriveNavFilter::FilteredCount();
        PublishStatus(L"Popup", L"Disabled", 0);
        PublishStatus(L"Drive", L"Disabled", 0, published);
        for (;;) {
            Settings settings;
            {
                SettingsLock lock;
                settings = ReadSettings();
            }
            if (!settings.Any()) break;
            if (settings.drive && !driveAttempted) {
                DWORD error = DriveNavFilter::InstallFilter();
                driveAttempted = true;
                driveReady = error == ERROR_SUCCESS;
                Log(L"drive.install", error);
                PublishStatus(L"Drive", error ? L"Unavailable" : L"Enabled", error, DriveNavFilter::FilteredCount());
                if (!error) DriveNavFilter::RefreshTree();
            } else if (!settings.drive && lastDrive) {
                DriveNavFilter::DisableFilter();
                driveAttempted = false;
                driveReady = false;
                PublishStatus(L"Drive", L"Disabled", 0, DriveNavFilter::FilteredCount());
                if (lastDrive) { DriveNavFilter::RefreshTree(); Log(L"drive.disabled"); }
            }
            lastDrive = settings.drive;
            DWORD count = DriveNavFilter::FilteredCount();
            if (driveReady && count != published) {
                PublishStatus(L"Drive", L"Enabled", 0, count);
                published = count;
            }
            DesktopPopup::Request(settings.popup, ownModule);
            Sleep(100);
        }
    } catch (const Failure& error) { Log(L"coordinator.failed", error.code, error.message); }
    catch (...) { Log(L"coordinator.exception", ERROR_UNHANDLED_EXCEPTION); }
    DriveNavFilter::DisableFilter();
    DesktopPopup::DeferUnload();
    DriveNavFilter::RefreshTree();
    PublishStatus(L"Drive", L"Disabled", 0, DriveNavFilter::FilteredCount());
    Log(L"host.unload.deferred", ERROR_BUSY,
        L"DLL pinned: published COM wrappers, entry addresses and trampolines may still be referenced; exit Explorer to release");
    if (com) CoUninitialize();
    running.store(false);
    return 0;
}
void Start() {
    if (running.load() || !ControlEnabled()) return;
    bool expected = false;
    if (!running.compare_exchange_strong(expected, true)) return;
    // The WH_GETMESSAGE callback never downloads symbols or waits on settings.
    // The loader already validates SID/session/path before targeting this thread.
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                            reinterpret_cast<LPCWSTR>(&Start), &ownModule)) {
        running.store(false);
        return;
    }
    if (!QueueUserWorkItem(Coordinator, nullptr, WT_EXECUTELONGFUNCTION)) {
        Log(L"QueueUserWorkItem(coordinator)", GetLastError());
        running.store(false);
    }
}
}
extern "C" __declspec(dllexport) LRESULT CALLBACK ExplorerExtensionsGetMessageHook(int code, WPARAM w, LPARAM l) {
    if (code >= 0) {
        try { Start(); }
        catch (...) { running.store(false); }
    }
    return CallNextHookEx(nullptr, code, w, l);
}
// No initialization, waits, I/O, thread creation or teardown under the loader lock.
BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }
