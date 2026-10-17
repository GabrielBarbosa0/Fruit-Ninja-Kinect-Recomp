[CmdletBinding()]
param(
    [ValidateRange(1, 128)]
    [int]$Jobs = 8,
    [string]$BuildDirectory
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $BuildDirectory) {
    $BuildDirectory = Join-Path $repoRoot 'build/xenon-runtime-host'
}
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
$xenonRoot = Join-Path $repoRoot 'tools/Xenon-Recomp'

$cmake = Get-ChildItem (Join-Path $repoRoot 'tools/toolchain/cmake') `
    -Filter cmake.exe -File -Recurse | Select-Object -First 1
$ninja = Get-ChildItem (Join-Path $repoRoot 'tools/toolchain/ninja') `
    -Filter ninja.exe -File -Recurse | Select-Object -First 1
$compiler = Get-ChildItem (Join-Path $repoRoot 'tools/toolchain/llvm-mingw') `
    -Filter clang++.exe -File -Recurse | Select-Object -First 1
if (-not $cmake -or -not $ninja -or -not $compiler) {
    throw 'Portable CMake, Ninja, or clang++ was not found under tools/toolchain.'
}
if (-not (Test-Path -LiteralPath $xenonRoot)) {
    throw "Modern Xenon-Recomp checkout was not found: $xenonRoot"
}

# The runtime and generated module exchange C++ ExecutionContext objects, so
# both must use the same LLVM/MinGW libc++ ABI. SDL2 supplies the native game
# window and D3D12 presents Xenos output. Audio remains disabled because the
# committed MSVC FFmpeg/XMA import libraries require OLDNAMES, which the
# LLVM-MinGW ABI used by the generated module does not provide. The runtime's
# DXC adapter supplies explicit COM IIDs so shader compilation works without
# MinGW's missing __uuidof specializations.
# AUTO dependency mode provisions Xenon's pinned SDL2 revision when absent.
$env:Path = "$($compiler.Directory.FullName);$env:Path"
& $cmake.FullName -S $xenonRoot -B $BuildDirectory -G Ninja `
    "-DCMAKE_MAKE_PROGRAM=$($ninja.FullName)" `
    "-DCMAKE_CXX_COMPILER=$($compiler.FullName)" `
    -DCMAKE_BUILD_TYPE=Release `
    -DXENON_BUILD_TESTS=OFF `
    -DXENON_BUILD_BENCHMARKS=OFF `
    -DXENON_BUILD_LAUNCHER=OFF `
    -DXENON_BUILD_RUNTIME_HOST=ON `
    -DXENON_ENABLE_GRAPHICS=ON `
    -DXENON_ENABLE_AUDIO=OFF `
    -DXENON_ENABLE_INPUT=ON `
    -DXENON_INPUT_ENABLE_SDL2=OFF `
    -DXENON_INPUT_ENABLE_SDL3=OFF `
    -DXENON_INPUT_ENABLE_XINPUT=ON `
    -DXENON_ENABLE_NETWORK=OFF `
    -DXENON_ENABLE_VULKAN=OFF `
    -DXENON_ENABLE_D3D12=ON `
    -DXENON_ENABLE_DXC=ON `
    -DXENON_DEPENDENCY_MODE=AUTO `
    -DXENON_AUTO_BOOTSTRAP_DEPS=ON `
    -DXENON_FETCH_MISSING_DEPS=ON
if ($LASTEXITCODE -ne 0) { throw 'Failed to configure xenon_runtime_host.' }

& $cmake.FullName --build $BuildDirectory --target xenon_runtime_host -j $Jobs
if ($LASTEXITCODE -ne 0) { throw 'Failed to build xenon_runtime_host.' }

$hostPath = Join-Path $BuildDirectory 'runtime_host/xenon_runtime_host.exe'
if (-not (Test-Path -LiteralPath $hostPath)) {
    throw "xenon_runtime_host was not produced at $hostPath"
}
Write-Host "Runtime host: $hostPath"
