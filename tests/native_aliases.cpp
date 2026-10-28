// Uses isolated test registry keys; never writes Windows namespace registrations.
#include "../extensions/explorer-extensions/src/features/drive-navigation/native-aliases.h"
#include <cstdio>
#include <stdexcept>
#include <string>

namespace {
using namespace DriveNavFilter;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void Create(HKEY root, const wchar_t* name, NativeRegistryKey& key) {
    Require(RegCreateKeyExW(root, name, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &key.value, nullptr) == ERROR_SUCCESS, "create test key");
}
void String(HKEY key, const wchar_t* name, const wchar_t* value) {
    Require(RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value),
            static_cast<DWORD>((wcslen(value) + 1) * sizeof(wchar_t))) == ERROR_SUCCESS, "write test string");
}
struct RegistryFixture {
    std::wstring path = L"Software\\ExplorerExtensions\\Tests\\NativeAliases-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    ~RegistryFixture() { RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str()); }
};
void CheckAlias(HKEY classes, const wchar_t* id, const wchar_t* target, DWORD pinned, const wchar_t* type) {
    NativeRegistryKey alias, instance, bag;
    std::wstring path = std::wstring(L"CLSID\\") + id;
    Create(classes, path.c_str(), alias);
    Require(RegSetValueExW(alias.value, L"System.IsPinnedToNameSpaceTree", 0, REG_DWORD,
            reinterpret_cast<const BYTE*>(&pinned), sizeof(pinned)) == ERROR_SUCCESS, "write pin flag");
    Create(alias.value, L"Instance", instance);
    Create(instance.value, L"InitPropertyBag", bag);
    String(instance.value, L"CLSID", type);
    String(bag.value, L"TargetFolderPath", target);
}
}
int main() {
    using namespace DriveNavFilter;
    try {
        RegistryFixture fixture;
        NativeRegistryKey root, names, classes;
        Create(HKEY_CURRENT_USER, fixture.path.c_str(), root);
        Create(root.value, L"Namespace", names);
        Create(root.value, L"Classes", classes);
        constexpr wchar_t id[] = L"{84FC6D30-C323-4BEC-AD27-F12D37E12D19}";
        constexpr wchar_t folder[] = L"{0E5AAE11-A475-4C5B-AB00-C66DE400274E}";
        constexpr std::uint32_t driveC = std::uint32_t{1} << 2;
        Require(RegisteredDriveMask(names.value, classes.value) == 0, "empty namespace is transparent");
        CheckAlias(classes.value, id, L"C:\\", 1, folder);
        Require(RegisteredDriveMask(names.value, classes.value) == 0, "unregistered class is not hidden");
        { NativeRegistryKey registration; Create(names.value, id, registration); }
        Require(RegisteredDriveMask(names.value, classes.value) == driveC, "native pinned registration recognizes drive root");
        CheckAlias(classes.value, id, L"C:\\", 0, folder);
        Require(RegisteredDriveMask(names.value, classes.value) == 0, "unpinned registration is transparent");
        CheckAlias(classes.value, id, L"C:\\Data", 1, folder);
        Require(RegisteredDriveMask(names.value, classes.value) == 0, "child folder is not a root");
        CheckAlias(classes.value, id, L"\\\\x\\y", 1, folder);
        Require(RegisteredDriveMask(names.value, classes.value) == 0, "UNC target is not a drive root");
        CheckAlias(classes.value, id, L"C:\\", 1, id);
        Require(RegisteredDriveMask(names.value, classes.value) == 0, "unknown shell class is transparent");
        CheckAlias(classes.value, id, L"c:", 1, folder);
        Require(RegisteredDriveMask(names.value, classes.value) == driveC, "lowercase native drive is supported");
        Require(RegisteredAliasDrive(classes.value, L"invalid") == -1, "invalid CLSID is rejected");
        std::puts("PASS: native namespace registration, pin flag, drive roots, unknown classes and invalid data");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
