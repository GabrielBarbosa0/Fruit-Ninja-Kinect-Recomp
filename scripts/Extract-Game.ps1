[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
    [string]$PackagePath,

    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\artifacts\extracted\fruit-ninja-kinect')
)

$ErrorActionPreference = 'Stop'

$extractor = Join-Path $PSScriptRoot '..\tools\Extract-STFS\Extract-STFS.ps1'
if (-not (Test-Path -LiteralPath $extractor -PathType Leaf)) {
    throw 'Extract-STFS is missing. Clone https://github.com/jasonulbright/Extract-STFS to tools/Extract-STFS.'
}

$package = (Resolve-Path -LiteralPath $PackagePath).Path
$output = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $output -Force | Out-Null

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $extractor -Path $package -OutputDir $output
if ($LASTEXITCODE -ne 0) {
    throw "STFS extraction failed with exit code $LASTEXITCODE."
}

$xex = Join-Path $output 'default.xex'
if (-not (Test-Path -LiteralPath $xex -PathType Leaf)) {
    throw 'Extraction completed, but default.xex was not found.'
}

$hash = Get-FileHash -Algorithm SHA256 -LiteralPath $xex
Write-Host "Extracted game to: $output"
Write-Host "default.xex SHA-256: $($hash.Hash)"
