#include "symbols.h"
#include "diagnostics.h"
#include <dbghelp.h>
#include <winhttp.h>
#include <shlobj.h>
#include <cstring>

namespace PopupSymbols {
using namespace ExplorerRuntime;
namespace {
class Internet {
    HINTERNET handle_;
public:
    explicit Internet(HINTERNET value) : handle_(value) { if (!value) Fail(L"WinHTTP handle"); }
    ~Internet() { WinHttpCloseHandle(handle_); }
    HINTERNET get() const { return handle_; }
};
struct DbgSession {
    Handle handle{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
    bool initialized = false;
    ~DbgSession() { if (initialized && !SymCleanup(handle.get())) Log(L"SymCleanup", GetLastError()); }
};
void Download(const std::wstring& directory, const std::wstring& destination, const Identity& identity) {
    Internet session(WinHttpOpen(L"ExplorerExtensions/0.4.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                 WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!WinHttpSetTimeouts(session.get(), 5000, 5000, 10000, 10000)) Fail(L"WinHttpSetTimeouts");
    Internet connection(WinHttpConnect(session.get(), L"msdl.microsoft.com", INTERNET_DEFAULT_HTTPS_PORT, 0));
    auto path = L"/download/symbols/twinui.pdb/" + IdentityKey(identity) + L"/twinui.pdb";
    Internet request(WinHttpOpenRequest(connection.get(), L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    if (!WinHttpSetOption(request.get(), WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy))) Fail(L"WinHttpSetOption");
    if (!WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
        Fail(L"WinHttpSendRequest");
    if (!WinHttpReceiveResponse(request.get(), nullptr)) Fail(L"WinHttpReceiveResponse");
    DWORD status = 0, size = sizeof(status);
    if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                              WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX)) Fail(L"WinHttpQueryHeaders");
    Log(L"symbols.http", status, IdentityKey(identity));
    if (status != 200) Fail(L"Symbol server HTTP status", status);
    std::wstring temporary = directory + L"\\download-" + std::to_wstring(GetCurrentProcessId()) + L".tmp";
    try {
        {
            Handle file(CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
            if (!file) Fail(L"CreateFileW(PDB temporary)");
            BYTE buffer[64 * 1024];
            DWORD total = 0;
            for (;;) {
                DWORD read = 0;
                if (!WinHttpReadData(request.get(), buffer, sizeof(buffer), &read)) Fail(L"WinHttpReadData");
                if (!read) break;
                if (total > 256 * 1024 * 1024 - read) Fail(L"PDB size limit", ERROR_FILE_TOO_LARGE);
                DWORD written = 0;
                if (!WriteFile(file.get(), buffer, read, &written, nullptr) || written != read) Fail(L"WriteFile(PDB)");
                total += read;
            }
            if (!total) Fail(L"Empty PDB response", ERROR_INVALID_DATA);
            if (!FlushFileBuffers(file.get())) Fail(L"FlushFileBuffers(PDB)");
        }
        if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            Fail(L"MoveFileExW(PDB)");
    } catch (...) { DeleteFileW(temporary.c_str()); throw; }
}
struct Matches { DWORD64 base; Result* result; unsigned count = 0; bool invalid = false; };
BOOL CALLBACK FindSymbol(PSYMBOL_INFO info, ULONG, PVOID context) noexcept {
    auto& matches = *static_cast<Matches*>(context);
    // Full undecoration confirms access, instance/virtual ABI, return and parameter types.
    char name[2048]{};
    if (!UnDecorateSymbolName(info->Name, name, sizeof(name), UNDNAME_COMPLETE)) return TRUE;
    if (std::strstr(name, "CAudioFlyoutController::VirtualDesktopSwitched")) {
        try { Log(L"symbols.candidate", 0, std::wstring(name, name + std::strlen(name))); }
        catch (...) { matches.invalid = true; }
    }
    Signature signature = Signature::Unknown;
    if (std::strcmp(name, "public: virtual long __cdecl CAudioFlyoutController::VirtualDesktopSwitched(struct IVirtualDesktop *)") == 0 ||
        std::strcmp(name, "public: virtual long __cdecl CAudioFlyoutController::VirtualDesktopSwitched(struct IVirtualDesktop * __ptr64) __ptr64") == 0)
        signature = Signature::Desktop;
    if (std::strcmp(name, "public: virtual long __cdecl CAudioFlyoutController::VirtualDesktopSwitched(struct IVirtualDesktop *,enum VirtualDesktopSwitchType)") == 0 ||
        std::strcmp(name, "public: virtual long __cdecl CAudioFlyoutController::VirtualDesktopSwitched(struct IVirtualDesktop * __ptr64,enum VirtualDesktopSwitchType) __ptr64") == 0)
        signature = Signature::DesktopAndSwitchType;
    if (signature == Signature::Unknown) return TRUE;
    if (info->Address <= matches.base || info->Address - matches.base >= matches.result->identity.imageSize) {
        matches.invalid = true;
        return TRUE;
    }
    DWORD rva = static_cast<DWORD>(info->Address - matches.base);
    if (!matches.count || matches.result->rva != rva || matches.result->signature != signature) ++matches.count;
    matches.result->rva = rva;
    matches.result->signature = signature;
    return TRUE;
}
}
Result Resolve() {
    Result result;
    HMODULE module = nullptr;
    try {
        LogEnvironment();
        module = LoadLibraryExW(L"twinui.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module) Fail(L"LoadLibraryExW(twinui)");
        LogModule(module);
        result.identity = ReadIdentity(module);
        auto key = IdentityKey(result.identity);
        Log(L"symbols.identity", 0, key);
        auto directory = DataDirectory() + L"\\symbols\\twinui.pdb\\" + key;
        int error = SHCreateDirectoryExW(nullptr, directory.c_str(), nullptr);
        if (error != ERROR_SUCCESS && error != ERROR_ALREADY_EXISTS && error != ERROR_FILE_EXISTS)
            Fail(L"Create symbol cache", static_cast<DWORD>(error));
        auto file = directory + L"\\twinui.pdb";
        DWORD attributes = GetFileAttributesW(file.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            DWORD code = GetLastError();
            if (code != ERROR_FILE_NOT_FOUND && code != ERROR_PATH_NOT_FOUND) Fail(L"GetFileAttributesW(PDB)", code);
            Log(L"symbols.download.begin", 0, key);
            Download(directory, file, result.identity);
        } else if (attributes & FILE_ATTRIBUTE_DIRECTORY) { Fail(L"Invalid PDB cache file", ERROR_INVALID_DATA); }
        DbgSession session;
        if (!session.handle) Fail(L"CreateEventW(DbgHelp session)");
        SymSetOptions(SYMOPT_EXACT_SYMBOLS | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS | SYMOPT_IGNORE_NT_SYMPATH);
        if (!SymInitializeW(session.handle.get(), directory.c_str(), FALSE)) Fail(L"SymInitializeW");
        session.initialized = true;
        auto image = ModulePath(module);
        auto base = SymLoadModuleExW(session.handle.get(), nullptr, image.c_str(), L"twinui",
                                     reinterpret_cast<DWORD64>(module), result.identity.imageSize, nullptr, 0);
        if (!base) Fail(L"SymLoadModuleExW");
        // Force symbol load; deferred/export-only results are never accepted.
        Matches matches{base, &result};
        if (!SymEnumSymbols(session.handle.get(), base, "*VirtualDesktopSwitched*", FindSymbol, &matches)) Fail(L"SymEnumSymbols");
        IMAGEHLP_MODULEW64 info{};
        info.SizeOfStruct = sizeof(info);
        if (!SymGetModuleInfoW64(session.handle.get(), base, &info)) Fail(L"SymGetModuleInfoW64");
        if (info.SymType != SymPdb || info.PdbUnmatched || info.DbgUnmatched ||
            !IsEqualGUID(info.PdbSig70, result.identity.guid) || info.PdbAge != result.identity.age)
            Fail(L"PDB identity mismatch or non-PDB symbols", ERROR_REVISION_MISMATCH);
        Log(L"symbols.match", 0, key);
        if (matches.count != 1 || matches.invalid || !ExecutableRva(module, result.rva))
            Fail(L"Unconfirmed or ambiguous function signature", ERROR_NOT_SUPPORTED);
        Log(L"symbols.signature", 0, result.signature == Signature::Desktop ? L"HRESULT(this, IVirtualDesktop*)" :
            L"HRESULT(this, IVirtualDesktop*, VirtualDesktopSwitchType:int)");
        Log(L"symbols.rva", 0, std::to_wstring(result.rva));
        result.error = ERROR_SUCCESS;
    } catch (const Failure& failure) {
        result.error = failure.code ? failure.code : ERROR_GEN_FAILURE;
        Log(L"symbols.failed", result.error, failure.message);
        result.rva = 0;
        result.signature = Signature::Unknown;
    } catch (...) {
        result.error = ERROR_UNHANDLED_EXCEPTION;
        Log(L"symbols.exception", result.error);
    }
    if (module && !FreeLibrary(module)) Log(L"FreeLibrary(twinui)", GetLastError());
    return result;
}
}
