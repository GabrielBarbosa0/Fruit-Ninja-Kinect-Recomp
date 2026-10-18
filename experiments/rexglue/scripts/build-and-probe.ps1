param(
    [ValidateRange(1, 120)]
    [int]$ProbeSeconds = 20,

    [switch]$IncrementalRuntimeHint
)

$ErrorActionPreference = 'Stop'

$experimentRoot = Split-Path -Parent $PSScriptRoot
$repositoryRoot = (Resolve-Path (Join-Path $experimentRoot '..\..')).Path
$toolchainRoot = Join-Path $repositoryRoot 'tools\toolchain'
$compilerBin = (Resolve-Path (Join-Path $toolchainRoot 'llvm-mingw\llvm-mingw-20261006-ucrt-x86_64\bin')).Path
$ninjaBin = (Resolve-Path (Join-Path $toolchainRoot 'ninja')).Path
$cmake = (Resolve-Path (Join-Path $toolchainRoot 'cmake\cmake-4.4.4-windows-x86_64\bin\cmake.exe')).Path
$rexglue = (Resolve-Path (Join-Path $repositoryRoot 'tools\rexglue-sdk\out\win-amd64\rexglue.exe')).Path
$buildRoot = Join-Path $repositoryRoot 'build\rexglue-game'
$gameRoot = (Resolve-Path (Join-Path $repositoryRoot 'artifacts\extracted\fruit-ninja-kinect')).Path
$generatedHeader = Join-Path $experimentRoot 'generated\default.uncompressed\fruit_ninja_kinect_recomp_init.h'
$generatedHeaderTime = if ($IncrementalRuntimeHint -and (Test-Path $generatedHeader)) {
    (Get-Item $generatedHeader).LastWriteTimeUtc
} else {
    $null
}

$env:PATH = "$compilerBin;$ninjaBin;$env:PATH"

Push-Location $experimentRoot
try {
    & $rexglue codegen 'fruit_ninja_kinect_recomp_manifest.toml'
    if ($LASTEXITCODE -ne 0) { throw "ReXGlue codegen failed with exit code $LASTEXITCODE" }
} finally {
    Pop-Location
}

# Runtime-only hints add a declaration used by the changed address bucket and
# registration tables, but do not alter already compiled function bodies. Keep
# the shared header timestamp stable so Ninja rebuilds only those changed units.
if ($null -ne $generatedHeaderTime) {
    (Get-Item $generatedHeader).LastWriteTimeUtc = $generatedHeaderTime
}

& $cmake --build $buildRoot --target fruit_ninja_kinect_recomp -j 8
if ($LASTEXITCODE -ne 0) { throw "Native build failed with exit code $LASTEXITCODE" }

$executable = Join-Path $buildRoot 'fruit_ninja_kinect_recomp.exe'
$process = Start-Process -FilePath $executable -WorkingDirectory $buildRoot `
    -ArgumentList @('--gpu_plugin=xenos', "--game_data_root=$gameRoot", '--log_level=debug') `
    -WindowStyle Hidden -PassThru

Start-Sleep -Seconds $ProbeSeconds
$wasAlive = -not $process.HasExited
if ($wasAlive) {
    Stop-Process -Id $process.Id
    $process.WaitForExit()
}

$log = Get-ChildItem (Join-Path $buildRoot 'logs') -Filter 'fruit_ninja_kinect_recomp_*.log' |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1
$fatal = Select-String -Path $log.FullName -Pattern '\[FATAL\]' | Select-Object -Last 1
$presentCount = (Select-String -Path $log.FullName -Pattern 'XELOG_GPU PRESENT').Count
$missingDeviceCount = (Select-String -Path $log.FullName -Pattern 'device not found').Count

[pscustomobject]@{
    ProcessId = $process.Id
    AliveAtTimeout = $wasAlive
    ExitCode = $process.ExitCode
    PresentedFrames = $presentCount
    MissingDeviceErrors = $missingDeviceCount
    Fatal = if ($fatal) { $fatal.Line } else { $null }
    Log = $log.FullName
} | Format-List
