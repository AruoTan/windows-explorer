#!/usr/bin/env bash
set -euo pipefail
project_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
zig_tool="${1:-zig}"
required="$(tr -d '\r\n' < "$project_root/toolchains/zig.version")"
[[ "$("$zig_tool" version)" == "$required" ]] || { printf 'Zig %s required\n' "$required" >&2; exit 1; }
source_dir="$project_root/extensions/explorer-extensions"
vendor_dir="$project_root/third_party/minhook"
export ZIG_GLOBAL_CACHE_DIR="${ZIG_GLOBAL_CACHE_DIR:-$project_root/.cache/zig}"
build_dir="$(mktemp -d /tmp/explorer-hook-tests.XXXXXX)"
trap 'rm -rf -- "$build_dir"' EXIT
for unit in buffer hook trampoline hde/hde64; do
    object="${unit//\//_}"
    "$zig_tool" cc -target x86_64-windows-gnu -O2 -c "$vendor_dir/src/$unit.c" -o "$build_dir/$object.o"
done
mkdir -p "$project_root/out/tests"
"$zig_tool" c++ -target x86_64-windows-gnu -std=c++23 -O2 -Wall -Wextra -Werror -static -DUNICODE -D_UNICODE \
    "-I$source_dir/include" "-I$vendor_dir/include" "$project_root/tests/hook_smoke.cpp" \
    "$source_dir/src/symbol_client.cpp" "$source_dir/src/module_identity.cpp" "$source_dir/src/diagnostics.cpp" \
    "$build_dir/"*.o -o "$project_root/out/tests/hook-smoke.exe" -lole32 -lshell32 -ladvapi32 -luser32 -luuid -lversion
printf 'Run on Windows: %s\n' "$project_root/out/tests/hook-smoke.exe"
