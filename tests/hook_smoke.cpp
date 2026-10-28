// Runs only in its own test process. Never attaches to Explorer or switches desktops.
#include "../extensions/explorer-extensions/src/features/desktop_popup.cpp"
#include <cstdio>
#include <thread>
#include <vector>

namespace {
std::atomic<unsigned> calls{0}, badArguments{0};
void* const Self = reinterpret_cast<void*>(static_cast<ULONG_PTR>(0x12345678));
void* const Desktop = reinterpret_cast<void*>(static_cast<ULONG_PTR>(0x23456789));
__declspec(noinline) HRESULT STDMETHODCALLTYPE TargetOne(void* self, void* desktop) {
    calls.fetch_add(1);
    if (self != Self || desktop != Desktop) badArguments.fetch_add(1);
    return E_ACCESSDENIED;
}
__declspec(noinline) HRESULT STDMETHODCALLTYPE TargetTwo(void* self, void* desktop, int type) {
    calls.fetch_add(1);
    if (self != Self || desktop != Desktop || type != 77) badArguments.fetch_add(1);
    return E_ABORT;
}
void Require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
}
int main() {
    using namespace DesktopPopup;
    Require(MH_Initialize() == MH_OK, "initialize");
    for (bool two : {false, true}) {
        void* entry = two ? reinterpret_cast<void*>(&TargetTwo) : reinterpret_cast<void*>(&TargetOne);
        void* replacement = two ? reinterpret_cast<void*>(&HookTwo) : reinterpret_cast<void*>(&HookOne);
        void* trampoline = nullptr;
        Require(MH_CreateHook(entry, replacement, &trampoline) == MH_OK, "create actual x64 hook");
        original.store(trampoline);
        Require(MH_EnableHook(entry) == MH_OK, "enable actual x64 hook");
        One volatile one = &TargetOne;
        Two volatile second = &TargetTwo;
        auto invoke = [&] { return two ? second(Self, Desktop, 77) : one(Self, Desktop); };
        desired.store(true); suppress.store(true);
        auto before = calls.load();
        Require(invoke() == S_OK && calls.load() == before, "enabled skips original");
        desired.store(false);
        Require(invoke() == (two ? E_ABORT : E_ACCESSDENIED), "disabled returns original HRESULT");
        Require(calls.load() == before + 1 && badArguments.load() == 0, "this, desktop and switch type forwarded");
        std::atomic<bool> stop{false};
        std::atomic<unsigned> badResults{0};
        std::vector<std::thread> threads;
        for (int i = 0; i < 4; ++i) threads.emplace_back([&] {
            while (!stop.load()) {
                auto result = invoke();
                if (result != S_OK && result != (two ? E_ABORT : E_ACCESSDENIED)) badResults.fetch_add(1);
            }
        });
        for (int i = 0; i < 20000; ++i) desired.store((i % 2) != 0);
        desired.store(false);
        stop.store(true);
        for (auto& thread : threads) thread.join();
        Require(badArguments.load() == 0 && badResults.load() == 0 && activeCallbacks.load() == 0, "concurrent toggles drain correctly");
        Require(MH_DisableHook(entry) == MH_OK, "remove only after test callers joined");
        Require(MH_RemoveHook(entry) == MH_OK, "release test trampoline after drain");
    }
    Require(MH_Uninitialize() == MH_OK, "test shutdown");
    std::puts("PASS: both instance signatures, original HRESULT/arguments, actual x64 detours, concurrent toggles");
    return 0;
}
