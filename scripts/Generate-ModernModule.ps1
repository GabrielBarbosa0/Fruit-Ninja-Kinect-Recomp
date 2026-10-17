[CmdletBinding()]
param(
    [string]$XexPath,
    [string]$OutputDirectory,
    [string]$XenonRoot,
    [string]$ToolBuildDirectory,
    [ValidateRange(1, 128)]
    [int]$Jobs = 8,
    [switch]$BuildModule
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))

if (-not $XexPath) {
    $XexPath = Join-Path $repoRoot 'artifacts/extracted/fruit-ninja-kinect/default.xex'
}
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $repoRoot 'artifacts/generated-modern'
}
if (-not $XenonRoot) {
    $XenonRoot = Join-Path $repoRoot 'tools/Xenon-Recomp'
}
if (-not $ToolBuildDirectory) {
    $ToolBuildDirectory = Join-Path $repoRoot 'build/xenon-modern-tools'
}

$XexPath = [IO.Path]::GetFullPath($XexPath)
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$XenonRoot = [IO.Path]::GetFullPath($XenonRoot)
$ToolBuildDirectory = [IO.Path]::GetFullPath($ToolBuildDirectory)
$hintsPath = Join-Path $repoRoot 'config/FNK.modern.hints'

foreach ($required in @($XexPath, $XenonRoot, $hintsPath)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required path does not exist: $required"
    }
}

# libc++ on Windows may expose file_time_type::rep as a wider integral type,
# making std::to_string(count()) ambiguous. Keep the local runtime checkout
# reproducible by applying our narrow portability patch exactly once.
$compatPatch = Join-Path $repoRoot 'patches/xenon-recomp-llvm-mingw.patch'
Push-Location $XenonRoot
try {
    & git apply --check $compatPatch 2>$null
    if ($LASTEXITCODE -eq 0) {
        & git apply $compatPatch
        if ($LASTEXITCODE -ne 0) { throw 'Failed to apply Xenon-Recomp compatibility patch.' }
    } else {
        & git apply --reverse --check $compatPatch 2>$null
        if ($LASTEXITCODE -ne 0) {
            throw 'Xenon-Recomp compatibility patch does not match this checkout.'
        }
    }
} finally {
    Pop-Location
}

$cmake = Get-ChildItem (Join-Path $repoRoot 'tools/toolchain/cmake') `
    -Filter cmake.exe -File -Recurse | Select-Object -First 1
$ninja = Get-ChildItem (Join-Path $repoRoot 'tools/toolchain/ninja') `
    -Filter ninja.exe -File -Recurse | Select-Object -First 1
$compiler = Get-ChildItem (Join-Path $repoRoot 'tools/toolchain/llvm-mingw') `
    -Filter clang++.exe -File -Recurse | Select-Object -First 1
if (-not $cmake -or -not $ninja -or -not $compiler) {
    throw 'Portable CMake, Ninja, or clang++ was not found under tools/toolchain.'
}

$env:Path = "$($compiler.Directory.FullName);$env:Path"
& $cmake.FullName -S $XenonRoot -B $ToolBuildDirectory -G Ninja `
    "-DCMAKE_MAKE_PROGRAM=$($ninja.FullName)" `
    "-DCMAKE_CXX_COMPILER=$($compiler.FullName)" `
    -DCMAKE_BUILD_TYPE=Release `
    -DXENON_BUILD_TESTS=OFF `
    -DXENON_BUILD_BENCHMARKS=OFF `
    -DXENON_BUILD_LAUNCHER=OFF `
    -DXENON_BUILD_RUNTIME_HOST=OFF `
    -DXENON_ENABLE_GRAPHICS=OFF `
    -DXENON_ENABLE_AUDIO=OFF `
    -DXENON_ENABLE_INPUT=OFF `
    -DXENON_ENABLE_NETWORK=OFF `
    -DXENON_ENABLE_VULKAN=OFF `
    -DXENON_ENABLE_D3D12=OFF `
    -DXENON_ENABLE_DXC=OFF `
    -DXENON_DEPENDENCY_MODE=SYSTEM `
    -DXENON_AUTO_BOOTSTRAP_DEPS=OFF `
    -DXENON_FETCH_MISSING_DEPS=OFF
if ($LASTEXITCODE -ne 0) { throw 'Failed to configure Xenon-Recomp tools.' }

& $cmake.FullName --build $ToolBuildDirectory --target recomp-driver -j $Jobs
if ($LASTEXITCODE -ne 0) { throw 'Failed to build recomp-driver.' }

$driver = Join-Path $ToolBuildDirectory 'recomp-driver.exe'
if (-not (Test-Path -LiteralPath $driver)) {
    throw "recomp-driver was not produced at $driver"
}

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
& $driver $XexPath $OutputDirectory --hints $hintsPath --jobs $Jobs 2>&1 |
    Tee-Object -FilePath (Join-Path $OutputDirectory 'generation-report.txt')
if ($LASTEXITCODE -ne 0) { throw 'Modern recompilation source generation failed.' }

$moduleExportPath = Join-Path $OutputDirectory 'module_export.cpp'
$generatedCmakePath = Join-Path $OutputDirectory 'CMakeLists.txt'
if (-not (Test-Path -LiteralPath $moduleExportPath) -or
    -not (Test-Path -LiteralPath $generatedCmakePath)) {
    throw 'Generated project is missing module_export.cpp or CMakeLists.txt.'
}

$moduleExport = Get-Content -LiteralPath $moduleExportPath -Raw
if ($moduleExport -notmatch 'fnk/nui/xenon_module_bridge.hpp') {
    $moduleExport = $moduleExport -replace `
        '(#include "registry\.hpp"\r?\n)', `
        "`$1#include `"fnk/nui/xenon_module_bridge.hpp`"`r`n"
    $moduleExport = $moduleExport -replace `
        '(  xenon::recomp::bind_compiled_registry\(context\);)', `
        "`$1`r`n  (void)fnk::nui::bind_xenon_nui_module(context);"
    Set-Content -LiteralPath $moduleExportPath -Value $moduleExport -NoNewline
}

$generatedCmake = Get-Content -LiteralPath $generatedCmakePath -Raw
if ($generatedCmake -notmatch '# FNK_NUI_BRIDGE_BEGIN') {
    $cmakeRepoRoot = $repoRoot.Replace('\', '/')
    $bridgeBlock = @"

# FNK_NUI_BRIDGE_BEGIN
set(FNK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FNK_BUILD_CAMERA OFF CACHE BOOL "" FORCE)
set(FNK_BUILD_POSE OFF CACHE BOOL "" FORCE)
set(FNK_BUILD_XENON_RUNTIME_BRIDGE ON CACHE BOOL "" FORCE)
add_subdirectory("$cmakeRepoRoot" fnk-compat EXCLUDE_FROM_ALL)
target_link_libraries(xenon_game_module PRIVATE fnk_nui_xenon_runtime)
# FNK_NUI_BRIDGE_END
"@
    Add-Content -LiteralPath $generatedCmakePath -Value $bridgeBlock
}

if ($BuildModule) {
    $moduleBuild = Join-Path $OutputDirectory 'build'
    & $cmake.FullName -S $OutputDirectory -B $moduleBuild -G Ninja `
        "-DCMAKE_MAKE_PROGRAM=$($ninja.FullName)" `
        "-DCMAKE_CXX_COMPILER=$($compiler.FullName)" `
        -DCMAKE_BUILD_TYPE=Release `
        "-DXENON_RECOMP_ROOT=$($XenonRoot.Replace('\', '/'))"
    if ($LASTEXITCODE -ne 0) { throw 'Failed to configure the generated module.' }
    & $cmake.FullName --build $moduleBuild --target xenon_game_module -j $Jobs
    if ($LASTEXITCODE -ne 0) { throw 'Failed to build the generated module.' }
}

Write-Host "Modern generated project: $OutputDirectory"
Write-Host 'NUI bridge injected into Xenon_BindCompiledRegistry.'
