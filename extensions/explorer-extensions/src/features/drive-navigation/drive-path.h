#pragma once
#include <string_view>

namespace DriveNavFilter {
// A native drive root only: no folders, UNC paths, namespace IDs or aliases.
inline int DriveIndex(std::wstring_view path) {
    if (path.size() != 2 && path.size() != 3) return -1;
    wchar_t letter = path[0];
    if (letter >= L'a' && letter <= L'z') letter -= L'a' - L'A';
    if (letter < L'A' || letter > L'Z' || path[1] != L':') return -1;
    if (path.size() == 3 && path[2] != L'\\' && path[2] != L'/') return -1;
    return letter - L'A';
}
}
