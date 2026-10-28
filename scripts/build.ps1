#Requires -Version 5.1
[CmdletBinding()]
param([string]$Zig = 'zig')
$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$requiredVersion = (Get-Content -LiteralPath (Join-Path $taskRoot 'toolchains/zig.version') -Raw).Trim()
$actualVersion = & $Zig 'version'
if ($LASTEXITCODE -ne 0 -or $actualVersion -ne $requiredVersion) { throw "Zig $requiredVersion is required; found $actualVersion" }
$source = Join-Path $taskRoot 'extensions/explorer-extensions'
$minhook = Join-Path $taskRoot 'third_party/minhook'
$output = Join-Path $taskRoot 'out/explorer-extensions'
$temporary = Join-Path ([IO.Path]::GetTempPath()) ('explorer-extensions-build-' + [guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($temporary)
try {
    $common = @('c++', '-target', 'x86_64-windows-gnu', '-std=c++23', '-O2', '-Wall', '-Wextra', '-Werror', '-DUNICODE', '-D_UNICODE', '-static', ('-I' + (Join-Path $source 'include')), ('-I' + (Join-Path $minhook 'include')))
    $dll = Join-Path $temporary 'ExplorerExtensions.dll'
    $exe = Join-Path $temporary 'ExplorerExtensions.exe'
    $resource = Join-Path $temporary 'loader.res'
    & $Zig 'rc' '/nologo' ('/I' + (Join-Path $source 'resources')) "/fo$resource" (Join-Path $source 'resources/loader.rc')
    if ($LASTEXITCODE -ne 0) { throw 'Resource compilation failed' }
    $objects = @()
    foreach ($unit in @('buffer', 'hook', 'trampoline', 'hde/hde64')) {
        $object = Join-Path $temporary ($unit.Replace('/', '_') + '.o')
        & $Zig 'cc' '-target' 'x86_64-windows-gnu' '-O2' '-c' (Join-Path $minhook "src/$unit.c") '-o' $object
        if ($LASTEXITCODE -ne 0) { throw "MinHook compilation failed: $unit" }
        $objects += $object
    }
    $shared = @((Join-Path $source 'src/diagnostics.cpp'), (Join-Path $source 'src/module_identity.cpp'))
    $dllSources = @((Join-Path $source 'src/dllmain.cpp'), (Join-Path $source 'src/features/drive-navigation/filter.cpp'), (Join-Path $source 'src/features/desktop_popup.cpp'), (Join-Path $source 'src/symbol_client.cpp'))
    & $Zig @common '-shared' @dllSources @shared @objects '-o' $dll '-lole32' '-lshell32' '-ladvapi32' '-luuid' '-luser32' '-lversion'
    if ($LASTEXITCODE -ne 0) { throw 'DLL compilation failed' }
    & $Zig @common '-municode' '-Wl,--subsystem,windows' (Join-Path $source 'src/host.cpp') (Join-Path $source 'src/symbol_resolver.cpp') @shared $resource '-o' $exe '-lshell32' '-ladvapi32' '-luser32' '-lole32' '-luuid' '-lversion' '-ldbghelp' '-lwinhttp'
    if ($LASTEXITCODE -ne 0) { throw 'Host compilation failed' }
    [void][IO.Directory]::CreateDirectory($output)
    foreach ($file in @($dll, $exe)) { [IO.File]::WriteAllBytes((Join-Path $output ([IO.Path]::GetFileName($file))), [IO.File]::ReadAllBytes($file)) }
    $hashes = foreach ($name in @('ExplorerExtensions.dll', 'ExplorerExtensions.exe')) {
        $hash = (Get-FileHash -LiteralPath (Join-Path $output $name) -Algorithm SHA256).Hash.ToLowerInvariant()
        "$hash  $name"
    }
    [IO.File]::WriteAllText((Join-Path $output 'SHA256SUMS'), (($hashes -join "`n") + "`n"), [Text.UTF8Encoding]::new($false))
    Write-Output "Built Windows x64 binaries: $output"
} finally { Remove-Item -LiteralPath $temporary -Recurse -Force -ErrorAction SilentlyContinue }
