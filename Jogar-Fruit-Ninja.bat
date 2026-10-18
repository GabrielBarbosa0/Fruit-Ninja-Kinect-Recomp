@echo off
setlocal

cd /d "%~dp0"

set "FNK_ROOT=%CD%"
set "FNK_BUILD=%FNK_ROOT%\build\rexglue-game"
set "FNK_EXE=%FNK_BUILD%\fruit_ninja_kinect_recomp.exe"
set "FNK_GAME_DATA=%FNK_ROOT%\artifacts\extracted\fruit-ninja-kinect"
set "FNK_LLVM=%FNK_ROOT%\tools\toolchain\llvm-mingw\llvm-mingw-20261006-ucrt-x86_64\bin"

if not exist "%FNK_EXE%" (
  echo [Fruit Ninja Kinect] Executavel nao encontrado:
  echo %FNK_EXE%
  echo.
  echo Compile o projeto antes de tentar iniciar o jogo.
  pause
  exit /b 1
)

if not exist "%FNK_GAME_DATA%" (
  echo [Fruit Ninja Kinect] Arquivos extraidos do jogo nao encontrados:
  echo %FNK_GAME_DATA%
  pause
  exit /b 1
)

if not exist "%FNK_LLVM%\libc++.dll" (
  echo [Fruit Ninja Kinect] Runtime LLVM nao encontrado:
  echo %FNK_LLVM%
  pause
  exit /b 1
)

if not defined FNK_CAMERA_DEVICE set "FNK_CAMERA_DEVICE=0"
set "PATH=%FNK_LLVM%;%FNK_BUILD%;%PATH%"

echo Iniciando Fruit Ninja Kinect...
echo Camera: dispositivo %FNK_CAMERA_DEVICE%
echo.

pushd "%FNK_BUILD%"
"%FNK_EXE%" --gpu_plugin=xenos "--game_data_root=%FNK_GAME_DATA%" --log_level=debug
set "FNK_EXIT_CODE=%ERRORLEVEL%"
popd

if not "%FNK_EXIT_CODE%"=="0" (
  echo.
  echo O jogo foi encerrado com o codigo %FNK_EXIT_CODE%.
  echo Consulte os logs em: %FNK_BUILD%\logs
  pause
)

exit /b %FNK_EXIT_CODE%
