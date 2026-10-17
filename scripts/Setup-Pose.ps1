[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$poseRoot = Join-Path $repositoryRoot 'tools\pose'
$downloadRoot = Join-Path $poseRoot 'downloads'
$modelRoot = Join-Path $poseRoot 'models'
$ortVersion = '1.23.2'
$ortArchiveName = "onnxruntime-win-x64-$ortVersion.zip"
$ortArchive = Join-Path $downloadRoot $ortArchiveName
$ortRoot = Join-Path $poseRoot "onnxruntime-win-x64-$ortVersion"
$modelPath = Join-Path $modelRoot 'movenet-singlepose-lightning.onnx'

$ortUrl = "https://github.com/microsoft/onnxruntime/releases/download/v$ortVersion/$ortArchiveName"
$modelUrl = 'https://huggingface.co/Xenova/movenet-singlepose-lightning/resolve/main/onnx/model.onnx?download=true'
$ortSha256 = '0B38DF9AF21834E41E73D602D90DB5CB06DBD1CA618948B8F1D66D607AC9F3CD'
$modelSha256 = '1AD4F8D6C2F776A9967DB3993C9CA740BC350104F9D37C151DC183FC29A464AD'

New-Item -ItemType Directory -Force -Path $downloadRoot, $modelRoot | Out-Null

function Get-VerifiedFile {
    param(
        [Parameter(Mandatory)] [string] $Uri,
        [Parameter(Mandatory)] [string] $Path,
        [Parameter(Mandatory)] [string] $ExpectedHash
    )

    if (-not (Test-Path -LiteralPath $Path) -or
        (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -ne $ExpectedHash) {
        Invoke-WebRequest -Uri $Uri -OutFile $Path
    }
    $actualHash = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
    if ($actualHash -ne $ExpectedHash) {
        throw "SHA-256 mismatch for $Path. Expected $ExpectedHash, got $actualHash."
    }
}

Get-VerifiedFile -Uri $ortUrl -Path $ortArchive -ExpectedHash $ortSha256
Get-VerifiedFile -Uri $modelUrl -Path $modelPath -ExpectedHash $modelSha256

if (-not (Test-Path -LiteralPath (Join-Path $ortRoot 'lib\onnxruntime.dll'))) {
    Expand-Archive -LiteralPath $ortArchive -DestinationPath $poseRoot -Force
}

Write-Host "ONNX Runtime $ortVersion and MoveNet are ready under tools\pose."
