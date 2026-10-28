#include "features.h"
#include "symbols.h"
#include "diagnostics.h"
#include <MinHook.h>
#include <atomic>
#include <limits>

namespace DesktopPopup {
using namespace ExplorerRuntime;
using namespace PopupSymbols;
namespace {
std::atomic<bool> desired{false}, suppress{false}, initializing{false}, installed{false};
std::atomic<std::uint64_t> generation{0}, attempted{std::numeric_limits<std::uint64_t>::max()};
std::atomic<unsigned> activeCallbacks{0};
std::atomic<void*> original{nullptr};
SRWLOCK stateLock = SRWLOCK_INIT;
struct StateGuard {
    StateGuard() { AcquireSRWLockExclusive(&stateLock); }
    ~StateGuard() { ReleaseSRWLockExclusive(&stateLock); }
};
HMODULE ownModule = nullptr; // Written by the single coordinator before scheduling work.
HMODULE twinui = nullptr;  // Retained with the pinned host for the lifetime of its callbacks.
void* target = nullptr;
bool created = false;
Signature createdSignature = Signature::Unknown;
struct CallbackScope {
    CallbackScope() { activeCallbacks.fetch_add(1); }
    ~CallbackScope() { activeCallbacks.fetch_sub(1); }
};
// x64 instance ABI: the first register is always the implicit this pointer.
using One = HRESULT (STDMETHODCALLTYPE*)(void*, void*);
using Two = HRESULT (STDMETHODCALLTYPE*)(void*, void*, int);
HRESULT STDMETHODCALLTYPE HookOne(void* self, void* desktop) {
    CallbackScope scope;
    if (desired.load() && suppress.load()) return S_OK;
    return reinterpret_cast<One>(original.load())(self, desktop);
}
HRESULT STDMETHODCALLTYPE HookTwo(void* self, void* desktop, int switchType) {
    CallbackScope scope;
    if (desired.load() && suppress.load()) return S_OK;
    return reinterpret_cast<Two>(original.load())(self, desktop, switchType);
}
DWORD WINAPI Initialize(void* value) noexcept {
    const auto current = static_cast<std::uint64_t>(reinterpret_cast<ULONG_PTR>(value));
    try {
        if (!desired.load() || generation.load() != current) Fail(L"Popup enable cancelled", ERROR_CANCELLED);
        DWORD shellPid = 0;
        HWND shell = GetShellWindow();
        if (!shell || !GetWindowThreadProcessId(shell, &shellPid) || shellPid != GetCurrentProcessId())
            Fail(L"Popup feature requires the current shell Explorer process", ERROR_NOT_SUPPORTED);
        if (!twinui) {
            twinui = LoadLibraryExW(L"twinui.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (!twinui) Fail(L"LoadLibraryExW(twinui)");
        }
        LogModule(twinui);
        const auto identity = ReadIdentity(twinui);
        Log(L"popup.module.identity", 0, IdentityKey(identity));
        Result result = ResolveInHelper(ownModule, desired, generation, current);
        if (!SameIdentity(identity, result.identity) || !ExecutableRva(twinui, result.rva))
            Fail(L"Helper/module identity or address mismatch", ERROR_REVISION_MISMATCH);
        if (result.signature != Signature::Desktop && result.signature != Signature::DesktopAndSwitchType)
            Fail(L"Unconfirmed popup signature", ERROR_NOT_SUPPORTED);
        StateGuard guard;
        if (!desired.load() || generation.load() != current) Fail(L"Popup enable cancelled", ERROR_CANCELLED);
        MH_STATUS status = MH_Initialize();
        Log(L"hook.MH_Initialize", static_cast<DWORD>(status));
        if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) Fail(L"MH_Initialize", static_cast<DWORD>(status));
        auto resolvedTarget = reinterpret_cast<BYTE*>(twinui) + result.rva;
        if (created && (target != resolvedTarget || createdSignature != result.signature))
            Fail(L"Hook target/signature changed within a loaded module", ERROR_REVISION_MISMATCH);
        if (!created) {
            void* trampoline = nullptr;
            auto hook = result.signature == Signature::Desktop ? reinterpret_cast<void*>(&HookOne) : reinterpret_cast<void*>(&HookTwo);
            status = MH_CreateHook(resolvedTarget, hook, &trampoline);
            Log(L"hook.MH_CreateHook", static_cast<DWORD>(status));
            if (status != MH_OK) Fail(L"MH_CreateHook", static_cast<DWORD>(status));
            target = resolvedTarget;
            original.store(trampoline);
            created = true;
            createdSignature = result.signature;
        }
        status = MH_EnableHook(target);
        Log(L"hook.MH_EnableHook", static_cast<DWORD>(status));
        if (status != MH_OK && status != MH_ERROR_ENABLED) Fail(L"MH_EnableHook", static_cast<DWORD>(status));
        installed.store(true);
        suppress.store(true);
        PublishStatus(L"Popup", L"Enabled", 0);
        Log(L"popup.installed", 0, result.signature == Signature::Desktop ?
            L"HRESULT(this, IVirtualDesktop*)" : L"HRESULT(this, IVirtualDesktop*, VirtualDesktopSwitchType:int)");
    } catch (const Failure& failure) {
        Log(L"popup.inactive", failure.code, failure.message);
        StateGuard guard;
        if (generation.load() == current) {
            suppress.store(false);
            PublishStatus(L"Popup", failure.code == ERROR_CANCELLED ? L"Disabled" : L"Unavailable", failure.code);
        }
    } catch (...) {
        Log(L"popup.exception", ERROR_UNHANDLED_EXCEPTION);
        StateGuard guard;
        if (generation.load() == current) {
            suppress.store(false);
            PublishStatus(L"Popup", L"Unavailable", ERROR_UNHANDLED_EXCEPTION);
        }
    }
    initializing.store(false);
    return 0;
}
}
void Request(bool enabled, HMODULE module) noexcept {
    StateGuard guard;
    bool previous = desired.exchange(enabled);
    if (previous != enabled) {
        generation.fetch_add(1);
        suppress.store(false);
        Log(enabled ? L"popup.enable.requested" : L"popup.disabled");
        PublishStatus(L"Popup", enabled ? L"Preparing" : L"Disabled", 0);
    }
    if (!enabled) return;
    if (installed.load()) {
        if (!suppress.exchange(true)) {
            Log(L"popup.enabled");
            PublishStatus(L"Popup", L"Enabled", 0);
        }
        return;
    }
    auto current = generation.load();
    if (attempted.load() == current || initializing.exchange(true)) return;
    attempted.store(current);
    ownModule = module;
    if (!QueueUserWorkItem(Initialize, reinterpret_cast<void*>(static_cast<ULONG_PTR>(current)), WT_EXECUTELONGFUNCTION)) {
        DWORD code = GetLastError();
        initializing.store(false);
        PublishStatus(L"Popup", L"Unavailable", code);
        Log(L"QueueUserWorkItem(popup)", code);
    }
}
void DeferUnload() noexcept {
    Request(false, ownModule);
    // Zero active callbacks is insufficient: a thread can already have fetched
    // our entry address. Keep both code and MinHook trampolines mapped.
    Log(L"popup.unload.deferred", ERROR_BUSY,
        L"active_callbacks=" + std::to_wstring(activeCallbacks.load()) +
        L"; initializing=" + std::to_wstring(initializing.load()) +
        L"; pinned code and any published trampoline retained until Explorer exits");
}
}
