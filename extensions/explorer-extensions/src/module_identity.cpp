#include "symbols.h"
#include <cstring>

namespace PopupSymbols {
using namespace ExplorerRuntime;
Identity ReadIdentity(HMODULE module) {
    auto base = reinterpret_cast<const BYTE*>(module);
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 || dos->e_lfanew > 0x100000)
        Fail(L"Invalid module DOS header", ERROR_BAD_EXE_FORMAT);
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) Fail(L"Invalid module PE header", ERROR_BAD_EXE_FORMAT);
    auto imageSize = nt->OptionalHeader.SizeOfImage;
    auto debug = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
    if (!debug.VirtualAddress || debug.VirtualAddress >= imageSize || debug.Size > imageSize - debug.VirtualAddress)
        Fail(L"Missing CodeView directory", ERROR_NOT_FOUND);
    auto entries = reinterpret_cast<const IMAGE_DEBUG_DIRECTORY*>(base + debug.VirtualAddress);
    Identity identity{};
    bool found = false;
    for (DWORD i = 0; i < debug.Size / sizeof(*entries); ++i) {
        const auto& item = entries[i];
        if (item.Type != IMAGE_DEBUG_TYPE_CODEVIEW || item.SizeOfData < 25 ||
            item.AddressOfRawData >= imageSize || item.SizeOfData > imageSize - item.AddressOfRawData) continue;
        const BYTE* record = base + item.AddressOfRawData;
        if (std::memcmp(record, "RSDS", 4) != 0) continue;
        if (found) Fail(L"Ambiguous CodeView records", ERROR_INVALID_DATA);
        std::memcpy(&identity.guid, record + 4, sizeof(GUID));
        std::memcpy(&identity.age, record + 20, sizeof(DWORD));
        const char* name = reinterpret_cast<const char*>(record + 24);
        auto end = static_cast<const char*>(std::memchr(name, 0, item.SizeOfData - 24));
        if (!end) Fail(L"Invalid PDB filename", ERROR_INVALID_DATA);
        std::string filename(name, end);
        auto slash = filename.find_last_of("\\/");
        if (slash != std::string::npos) filename.erase(0, slash + 1);
        if (_stricmp(filename.c_str(), "twinui.pdb") != 0) Fail(L"Unexpected PDB filename", ERROR_INVALID_DATA);
        found = true;
    }
    if (!found) Fail(L"Missing RSDS identity", ERROR_NOT_FOUND);
    identity.imageSize = imageSize;
    return identity;
}
std::wstring IdentityKey(const Identity& value) {
    const auto& g = value.guid;
    wchar_t key[64]{};
    swprintf_s(key, L"%08lX%04X%04X%02X%02X%02X%02X%02X%02X%02X%02X%lX", g.Data1, g.Data2, g.Data3,
               g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7], value.age);
    return key;
}
bool SameIdentity(const Identity& left, const Identity& right) {
    return IsEqualGUID(left.guid, right.guid) && left.age == right.age && left.imageSize == right.imageSize;
}
bool ExecutableRva(HMODULE module, DWORD rva) {
    auto base = reinterpret_cast<const BYTE*>(module);
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
    if (!rva || rva >= nt->OptionalHeader.SizeOfImage) return false;
    auto sections = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        auto& s = sections[i];
        if (rva >= s.VirtualAddress && rva - s.VirtualAddress < s.Misc.VirtualSize)
            return (s.Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
    }
    return false;
}
}
