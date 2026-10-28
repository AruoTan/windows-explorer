#!/usr/bin/env bash
set -euo pipefail
task_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
zig_tool="${1:-zig}"
required_version="$(tr -d '\r\n' < "$task_root/toolchains/zig.version")"
actual_version="$("$zig_tool" version)"
if [[ "$actual_version" != "$required_version" ]]; then
    printf 'Zig %s is required; found %s\n' "$required_version" "$actual_version" >&2
    exit 1
fi
source_dir="$task_root/extensions/explorer-extensions"
output_dir="$task_root/out/explorer-extensions"
minhook_dir="$task_root/third_party/minhook"
# Keep caches writable in restricted Linux devcontainers.
export ZIG_GLOBAL_CACHE_DIR="${ZIG_GLOBAL_CACHE_DIR:-$task_root/.cache/zig}"
build_dir="$(mktemp -d /tmp/explorer-extensions-build.XXXXXX)"
trap 'rm -rf -- "$build_dir"' EXIT
common=(-target x86_64-windows-gnu -std=c++23 -O2 -Wall -Wextra -Werror -DUNICODE -D_UNICODE -static "-I$source_dir/include" "-I$minhook_dir/include")
"$zig_tool" rc /nologo "/I$source_dir/resources" "/fo$build_dir/loader.res" \
    "$source_dir/resources/loader.rc"
for unit in buffer hook trampoline hde/hde64; do
    object="${unit//\//_}"
    "$zig_tool" cc -target x86_64-windows-gnu -O2 -c "$minhook_dir/src/$unit.c" -o "$build_dir/$object.o"
done
shared=("$source_dir/src/diagnostics.cpp" "$source_dir/src/module_identity.cpp")
"$zig_tool" c++ "${common[@]}" -shared "$source_dir/src/dllmain.cpp" \
    "$source_dir/src/features/drive-navigation/filter.cpp" "$source_dir/src/features/desktop_popup.cpp" \
    "$source_dir/src/symbol_client.cpp" "${shared[@]}" "$build_dir/"*.o \
    -o "$build_dir/ExplorerExtensions.dll" -lole32 -lshell32 -ladvapi32 -luuid -luser32 -lversion
"$zig_tool" c++ "${common[@]}" -municode -Wl,--subsystem,windows "$source_dir/src/host.cpp" \
    "$source_dir/src/symbol_resolver.cpp" "${shared[@]}" "$build_dir/loader.res" \
    -o "$build_dir/ExplorerExtensions.exe" -lshell32 -ladvapi32 -luser32 -lole32 -luuid -lversion -ldbghelp -lwinhttp
mkdir -p -- "$output_dir"
cp -- "$build_dir/ExplorerExtensions.dll" "$build_dir/ExplorerExtensions.exe" "$output_dir/"
cd -- "$output_dir"
sha256sum ExplorerExtensions.dll ExplorerExtensions.exe > SHA256SUMS
printf 'Built Windows x64 binaries: %s\n' "$output_dir"
