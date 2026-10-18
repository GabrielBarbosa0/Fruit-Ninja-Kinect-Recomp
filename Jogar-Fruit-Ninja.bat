@echo off
setlocal
cd /d "%~dp0"

set "FNK_LAUNCHER=%~dp0scripts\FruitNinjaLauncher.ps1"
if not exist "%FNK_LAUNCHER%" (
  echo [Fruit Ninja Kinect] Launcher nao encontrado:
  echo %FNK_LAUNCHER%
  pause
  exit /b 1
)

start "Fruit Ninja Kinect Launcher" powershell.exe -NoProfile -ExecutionPolicy Bypass -STA -WindowStyle Hidden -File "%FNK_LAUNCHER%"
