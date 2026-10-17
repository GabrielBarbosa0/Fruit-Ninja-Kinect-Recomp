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
$materializedXex = Join-Path ([IO.Path]::GetDirectoryName($XexPath)) 'default.uncompressed.xex'
$graphCache = Join-Path $OutputDirectory '.graph-windows-v2'
$legacyXenonRoot = Join-Path $repoRoot 'tools/XenonRecomp'

foreach ($required in @($XexPath, $XenonRoot, $hintsPath)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required path does not exist: $required"
    }
}

$legacyImportPatch = Join-Path $repoRoot 'patches/xenonrecomp-preserve-import-thunks.patch'
Push-Location $legacyXenonRoot
try {
    $savedErrorPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    & git apply --check $legacyImportPatch 2>$null
    $canApplyLegacyPatch = $LASTEXITCODE -eq 0
    $ErrorActionPreference = $savedErrorPreference
    if ($canApplyLegacyPatch) {
        & git apply $legacyImportPatch
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to apply legacy XenonRecomp import patch: $legacyImportPatch"
        }
    } else {
        $ErrorActionPreference = 'Continue'
        & git apply --reverse --check $legacyImportPatch 2>$null
        $legacyPatchAlreadyApplied = $LASTEXITCODE -eq 0
        $ErrorActionPreference = $savedErrorPreference
        if (-not $legacyPatchAlreadyApplied) {
            throw "Legacy XenonRecomp import patch does not match this checkout: $legacyImportPatch"
        }
    }
} finally {
    $ErrorActionPreference = 'Stop'
    Pop-Location
}

# libc++ on Windows may expose file_time_type::rep as a wider integral type,
# making std::to_string(count()) ambiguous. Keep the local runtime checkout
# reproducible by applying our narrow portability patch exactly once.
$compatPatches = @(
    (Join-Path $repoRoot 'patches/xenon-recomp-llvm-mingw.patch'),
    (Join-Path $repoRoot 'patches/xenon-recomp-llvm-mingw-dbghelp.patch'),
    (Join-Path $repoRoot 'patches/xenon-recomp-windows-graph-promotion.patch'),
    (Join-Path $repoRoot 'patches/xenon-recomp-legacy-hint-parser.patch'),
    (Join-Path $repoRoot 'patches/xenon-recomp-partial-codegen.patch'),
    (Join-Path $repoRoot 'patches/xenon-recomp-top-level-import-handoff.patch'),
    (Join-Path $repoRoot 'patches/xenon-recomp-fruit-ninja-boot-exports.patch'),
    (Join-Path $repoRoot 'patches/xenon-recomp-d3d12-algorithm-include.patch'),
    (Join-Path $repoRoot 'patches/xenon-recomp-d3d12-mingw-headers.patch'),
    (Join-Path $repoRoot 'patches/xenon-recomp-no-dxc-coverage.patch'),
    (Join-Path $repoRoot 'patches/xenon-recomp-runtime-host-safe-exit.patch'),
    (Join-Path $repoRoot 'patches/xenon-recomp-dxc-mingw-iids.patch'),
    (Join-Path $repoRoot 'patches/xenon-recomp-named-semaphore.patch')
)
Push-Location $XenonRoot
try {
    foreach ($compatPatch in $compatPatches) {
        $savedErrorPreference = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        & git apply --check $compatPatch 2>$null
        $canApplyPatch = $LASTEXITCODE -eq 0
        $ErrorActionPreference = $savedErrorPreference
        if ($canApplyPatch) {
            & git apply $compatPatch
            if ($LASTEXITCODE -ne 0) {
                throw "Failed to apply Xenon-Recomp compatibility patch: $compatPatch"
            }
        } else {
            $ErrorActionPreference = 'Continue'
            & git apply --reverse --check $compatPatch 2>$null
            $patchAlreadyApplied = $LASTEXITCODE -eq 0
            $ErrorActionPreference = $savedErrorPreference
            if (-not $patchAlreadyApplied) {
                throw "Xenon-Recomp compatibility patch does not match this checkout: $compatPatch"
            }
        }
    }
} finally {
    $ErrorActionPreference = 'Stop'
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

$legacyBuild = Join-Path $repoRoot 'tools/XenonRecomp/build-fnk2'
& $cmake.FullName --build $legacyBuild --target XenonUtils -j $Jobs
if ($LASTEXITCODE -ne 0) {
    throw 'Failed to rebuild legacy XenonUtils after applying the import-preservation patch.'
}

$materializerBuild = Join-Path $repoRoot 'build/native-materializer'
& $cmake.FullName -S $repoRoot -B $materializerBuild -G Ninja `
    "-DCMAKE_MAKE_PROGRAM=$($ninja.FullName)" `
    "-DCMAKE_CXX_COMPILER=$($compiler.FullName)" `
    -DCMAKE_BUILD_TYPE=Release `
    -DFNK_BUILD_TESTS=OFF `
    -DFNK_BUILD_CAMERA=OFF `
    -DFNK_BUILD_POSE=OFF `
    -DFNK_BUILD_REVERSE_TOOLS=ON
if ($LASTEXITCODE -ne 0) { throw 'Failed to configure the XEX materializer.' }
& $cmake.FullName --build $materializerBuild --target fnk_xex_materialize -j $Jobs
if ($LASTEXITCODE -ne 0) { throw 'Failed to build the XEX materializer.' }
& (Join-Path $materializerBuild 'fnk_xex_materialize.exe') $XexPath $materializedXex
if ($LASTEXITCODE -ne 0) { throw 'Failed to materialize the uncompressed XEX.' }

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
$savedErrorPreference = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
$driverOutput = & $driver $materializedXex $OutputDirectory --hints $hintsPath `
    --graph-cache $graphCache --jobs $Jobs --allow-partial --quiet 2>&1
$driverExitCode = $LASTEXITCODE
$ErrorActionPreference = $savedErrorPreference
$driverOutput | Set-Content -LiteralPath (Join-Path $OutputDirectory 'generation-report.txt')
$generationStatusPath = Join-Path $OutputDirectory 'generation-status.json'
$generationComplete = $false
if (Test-Path -LiteralPath $generationStatusPath) {
    $generationComplete = [bool]((Get-Content -LiteralPath $generationStatusPath -Raw | ConvertFrom-Json).complete)
}
# recomp-driver deliberately returns a nonzero status when the report still
# contains unresolved sites. In --allow-partial mode those sites are handed
# to the runtime fallback; generation-status.json is the authoritative signal
# that the transactional project emission itself completed.
if ($driverExitCode -ne 0 -and -not $generationComplete) {
    throw 'Modern recompilation source generation failed.'
}
if ($driverExitCode -ne 0) {
    Write-Host "Generated partial module; unresolved guest sites remain for runtime fallback."
}

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
Write-Host "Runtime XEX: $materializedXex"
Write-Host 'NUI bridge injected into Xenon_BindCompiledRegistry.'
