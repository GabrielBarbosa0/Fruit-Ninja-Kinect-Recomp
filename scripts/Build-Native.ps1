[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug'
)

$ErrorActionPreference = 'Stop'

$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$cmake = Get-ChildItem -LiteralPath (Join-Path $repositoryRoot 'tools\toolchain\cmake') `
    -Filter cmake.exe -File -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
$clang = Get-ChildItem -LiteralPath (Join-Path $repositoryRoot 'tools\toolchain\llvm-mingw') `
    -Filter clang++.exe -File -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
$ninja = Get-ChildItem -LiteralPath (Join-Path $repositoryRoot 'tools\toolchain\ninja') `
    -Filter ninja.exe -File -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1

if (-not $cmake -or -not $clang -or -not $ninja) {
    throw 'Portable CMake, LLVM-MinGW, or Ninja is missing under tools/toolchain. See docs/TOOLING.md.'
}

# Generated executables dynamically load the LLVM-MinGW C++ runtime. Keep its
# bin directory on PATH for both compiler probes and CTest execution.
$llvmBin = $clang.Directory.FullName
$env:Path = "$llvmBin;$($cmake.Directory.FullName);$($ninja.Directory.FullName);$env:Path"

$buildDirectory = Join-Path $repositoryRoot "build\native-$($Configuration.ToLowerInvariant())"
$configureArguments = @(
    '-S', $repositoryRoot,
    '-B', $buildDirectory,
    '-G', 'Ninja',
    "-DCMAKE_BUILD_TYPE=$Configuration",
    "-DCMAKE_MAKE_PROGRAM=$($ninja.FullName)",
    "-DCMAKE_CXX_COMPILER=$($clang.FullName)"
)

& $cmake.FullName @configureArguments
if ($LASTEXITCODE -ne 0) {
    throw "CMake configuration failed with exit code $LASTEXITCODE."
}

& $cmake.FullName --build $buildDirectory --parallel
if ($LASTEXITCODE -ne 0) {
    throw "Native build failed with exit code $LASTEXITCODE."
}

& $cmake.FullName --build $buildDirectory --target test
if ($LASTEXITCODE -ne 0) {
    throw "Native tests failed with exit code $LASTEXITCODE."
}

Write-Host "Native NUI library and tests passed ($Configuration)."
