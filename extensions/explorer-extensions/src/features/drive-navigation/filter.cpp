#include "runtime.h"
#include "enum-filter.h"
#include "slot-patch.h"
#include "features.h"
#include "diagnostics.h"
#include "native-aliases.h"

namespace DriveNavFilter {
using namespace ExplorerRuntime;

// This immutable PIDL is retained with the pinned DLL until Explorer exits.
PIDLIST_ABSOLUTE computer = nullptr;
std::atomic<DWORD> filteredCount{0};

std::uint32_t ReadHiddenDriveMask() {
    std::uint32_t mask = 0;
    constexpr wchar_t path[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Desktop\\NameSpace";
    for (HKEY root : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
        NativeRegistryKey entries;
        LSTATUS status = RegOpenKeyExW(root, path, 0, KEY_READ, &entries.value);
        if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND) continue;
        if (status != ERROR_SUCCESS) { Log(L"RegOpenKeyExW(desktop namespace)", status); continue; }
        mask |= RegisteredDriveMask(entries.value, HKEY_CLASSES_ROOT);
    }
    return mask;
}
bool ShouldHide(PCUITEMID_CHILD child, std::uint32_t mask) {
    if (!mask) return false;
    auto absolute = ILCombine(computer, child);
    if (!absolute) return false;
    wchar_t path[MAX_PATH]{};
    bool resolved = SHGetPathFromIDListW(absolute, path) != FALSE;
    CoTaskMemFree(absolute);
    int index = resolved ? DriveIndex(path) : -1;
    return index >= 0 && (mask & (std::uint32_t{1} << index));
}
bool IsThisPC(IShellFolder* folder) {
    IPersistFolder2* persist = nullptr;
    if (FAILED(folder->QueryInterface(IID_IPersistFolder2,
                                     reinterpret_cast<void**>(&persist)))) return false;
    PIDLIST_ABSOLUTE current = nullptr;
    HRESULT hr = persist->GetCurFolder(&current);
    persist->Release();
    bool same = SUCCEEDED(hr) && current && ILIsEqual(current, computer);
    CoTaskMemFree(current);
    return same;
}

using EnumFunction = HRESULT (STDMETHODCALLTYPE*)(IShellFolder*, HWND, SHCONTF, IEnumIDList**);
std::atomic<EnumFunction> originalEnum{nullptr};
void** patchedSlot = nullptr; // Only the worker reads/writes this variable.

HRESULT STDMETHODCALLTYPE EnumHook(IShellFolder* folder, HWND owner, SHCONTF flags,
                                   IEnumIDList** result) {
    auto original = originalEnum.load();
    if (!original) return E_UNEXPECTED;
    HRESULT hr = original(folder, owner, flags, result);
    if (!enabled.load() || FAILED(hr) || !result || !*result ||
        !(flags & SHCONTF_NAVIGATION_ENUM) || !IsThisPC(folder)) return hr;
    auto mask = ReadHiddenDriveMask();
    if (!mask) return hr;
    auto wrapped = new (std::nothrow) Enumerator(*result, mask);
    if (wrapped) { *result = wrapped; ++filteredCount; }
    return hr;
}

// Change a single aligned data pointer, not machine instructions. If another
// extension changed the slot meanwhile, never overwrite its newer pointer.
struct PageProtection {
    using Previous = DWORD;
    std::uint32_t MakeWritable(void** slot, Previous& previous) {
        MEMORY_BASIC_INFORMATION page{};
        if (!VirtualQuery(slot, &page, sizeof(page))) return GetLastError();
        if (page.Type != MEM_IMAGE || page.State != MEM_COMMIT) return ERROR_NOT_SUPPORTED;
        DWORD executable = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        DWORD writable = (page.Protect & executable) ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE;
        return VirtualProtect(slot, sizeof(void*), writable, &previous) ? 0 : GetLastError();
    }
    std::uint32_t Restore(void** slot, Previous previous) {
        DWORD unused = 0;
        return VirtualProtect(slot, sizeof(void*), previous, &unused) ? 0 : GetLastError();
    }
};
DWORD ExchangeSlot(void** slot, void* expected, void* replacement) {
    PageProtection protection;
    return ExchangePointer(protection, slot, expected, replacement);
}
DWORD InstallFilter() {
    if (patchedSlot) { enabled.store(true); return ERROR_SUCCESS; }
    IShellFolder* desktop = nullptr;
    IShellFolder* folder = nullptr;
    HRESULT hr = computer ? S_OK : SHGetSpecialFolderLocation(nullptr, CSIDL_DRIVES, &computer);
    if (SUCCEEDED(hr)) hr = SHGetDesktopFolder(&desktop);
    if (SUCCEEDED(hr)) hr = desktop->BindToObject(computer, nullptr, IID_IShellFolder,
                                                reinterpret_cast<void**>(&folder));
    DWORD error = static_cast<DWORD>(hr);
    if (SUCCEEDED(hr) && folder) {
        // Public COM ABI: IUnknown 0..2, ParseDisplayName 3, EnumObjects 4.
        void** slot = &(*reinterpret_cast<void***>(folder))[4];
        void* previous = ReadSlot(slot);
        originalEnum.store(reinterpret_cast<EnumFunction>(previous));
        error = ExchangeSlot(slot, previous, reinterpret_cast<void*>(EnumHook));
        // Even a protection-restoration failure may have published our pointer.
        // Keep the original and slot intact for a later safe stop/retry.
        if (ReadSlot(slot) == reinterpret_cast<void*>(EnumHook)) {
            patchedSlot = slot;
        }
        if (error == ERROR_SUCCESS) enabled.store(true);
    }
    if (folder) folder->Release();
    if (desktop) desktop->Release();
    return error;
}
void DisableFilter() {
    // Retain the installed entry and original pointer. Published COM wrappers
    // and callbacks may outlive disable, so code remains pinned and forwards.
    enabled.store(false);
}
DWORD FilteredCount() { return filteredCount.load(); }
void RefreshTree() {
    if (computer) SHChangeNotify(SHCNE_UPDATEDIR, SHCNF_IDLIST | SHCNF_FLUSHNOWAIT, computer, nullptr);
}

} // namespace DriveNavFilter
