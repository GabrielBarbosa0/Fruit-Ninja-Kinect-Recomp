[CmdletBinding()]
param(
    [ValidateRange(1, 128)]
    [int]$Jobs = 8,
    [ValidateRange(1, 3600)]
    [int]$ObserveSeconds = 15,
    [switch]$BuildHost,
    [switch]$KeepRunning,
    [switch]$VerboseGuest
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$hostBuild = Join-Path $repoRoot 'build/xenon-runtime-host'
$hostPath = Join-Path $hostBuild 'runtime_host/xenon_runtime_host.exe'
$xexPath = Join-Path $repoRoot 'artifacts/extracted/fruit-ninja-kinect/default.uncompressed.xex'
$modulePath = Join-Path $repoRoot 'artifacts/generated-modern/build/libxenon_game_module.dll'
$runtimeRoot = Join-Path $repoRoot 'artifacts/runtime-sessions'
$toolchainBin = (Get-ChildItem (Join-Path $repoRoot 'tools/toolchain/llvm-mingw') `
    -Filter clang++.exe -File -Recurse | Select-Object -First 1).Directory.FullName

if ($BuildHost -or -not (Test-Path -LiteralPath $hostPath)) {
    & (Join-Path $PSScriptRoot 'Build-RuntimeHost.ps1') -Jobs $Jobs -BuildDirectory $hostBuild
}
foreach ($required in @($hostPath, $xexPath, $modulePath, $toolchainBin)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required runtime input does not exist: $required"
    }
}

$sessionId = '{0}-{1}' -f (Get-Date -Format 'yyyyMMdd-HHmmss'), ([guid]::NewGuid().ToString('N').Substring(0, 8))
$sessionDir = Join-Path $runtimeRoot $sessionId
New-Item -ItemType Directory -Force -Path $sessionDir | Out-Null
$launchConfigPath = Join-Path $sessionDir 'launch-config.json'
$savePath = Join-Path $sessionDir 'saves'
$screenshotsPath = Join-Path $sessionDir 'screenshots'

$launchConfig = [ordered]@{
    configVersion = 1
    sessionId = $sessionId
    sessionDir = $sessionDir
    gameId = 'fruit-ninja-kinect'
    title = 'Fruit Ninja Kinect (recomp)'
    # Passing the loose materialized XEX makes the host load it while still
    # mounting its containing extracted directory as game:/.
    contentPath = $xexPath
    moduleId = 'dev.fnk.recomp'
    moduleName = 'Fruit Ninja Kinect generated module'
    modulePath = (Split-Path $modulePath -Parent)
    moduleVersion = '0.1.0-dev'
    moduleSettings = @{}
    runtimeApiRequirements = @{}
    nativeExtensionPath = $modulePath
    profileId = 'fnk-local'
    profileName = 'Player One'
    region = 'Auto (Global)'
    profileXuid = '1'
    renderer = 'None'
    shaderCache = $false
    shaderCacheMode = 'Disabled'
    inputBackend = 'None'
    inputPreferredDevice = 'Automatic'
    inputDeadzone = 0.10
    inputRumble = $false
    inputBackground = $true
    inputModuleApiVersion = 1
    inputUserSources = @()
    audioMasterVolume = 1.0
    audioMuteUnfocused = $false
    logVerbose = [bool]$VerboseGuest
    savePath = $savePath
    screenshotsPath = $screenshotsPath
    offline = $true
    headlessMode = $true
}
$launchConfig | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $launchConfigPath -Encoding UTF8

# libc++.dll and libunwind.dll are intentionally kept in the portable
# toolchain. Add that directory for both the host executable and game DLL.
$env:Path = "$toolchainBin;$env:Path"
$process = Start-Process -FilePath $hostPath `
    -ArgumentList @('--launch-config', $launchConfigPath) `
    -WorkingDirectory $repoRoot -PassThru -WindowStyle Hidden

$deadline = (Get-Date).AddSeconds($ObserveSeconds)
$statusPath = Join-Path $sessionDir 'status.json'
while (-not $process.HasExited -and (Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 100
    $process.Refresh()
}

if (-not $process.HasExited -and -not $KeepRunning) {
    New-Item -ItemType File -Force -Path (Join-Path $sessionDir 'stop.signal') | Out-Null
    if (-not $process.WaitForExit(7000)) {
        throw "Runtime did not honor stop.signal; process $($process.Id) is still active."
    }
}

Write-Host "Session: $sessionDir"
Write-Host "Runtime PID: $($process.Id)"
if (Test-Path -LiteralPath $statusPath) {
    $status = Get-Content -LiteralPath $statusPath -Raw | ConvertFrom-Json
    Write-Host "State: $($status.stateName)"
    if ($status.lastError) { Write-Host "Last error: $($status.lastError)" }
    if ($status.unresolvedImports) {
        Write-Host "Unresolved imports: $($status.unresolvedImports.Count)"
    }
} else {
    Write-Warning 'The runtime did not publish status.json.'
}
if ($process.HasExited) {
    Write-Host "Exit code: $($process.ExitCode)"
} else {
    Write-Host 'Runtime remains active because -KeepRunning was specified.'
}
Write-Host "Log: $(Join-Path $sessionDir 'log.txt')"

