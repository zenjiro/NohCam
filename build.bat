@echo off
setlocal enabledelayedexpansion

echo Searching for Visual Studio instance...
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.Component.MSBuild -property installationPath`) do (
  set "VS_PATH=%%i"
)

if not defined VS_PATH (
  echo Error: Could not find Visual Studio installation.
  exit /b 1
)

set "VCVARS=%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
  echo Error: Could not find vcvars64.bat at %VCVARS%
  exit /b 1
)

echo Loading Visual Studio environment...
call "%VCVARS%"

set "ROOT_DIR=%~dp0"
set "TOOLCHAIN=%ROOT_DIR%vcpkg\scripts\buildsystems\vcpkg.cmake"

if not exist build (
  mkdir build
)

cd build
echo Configuring with CMake...
cmake .. -DCMAKE_TOOLCHAIN_FILE="%TOOLCHAIN%" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 (
  echo Error: CMake configuration failed.
  exit /b 1
)

echo Compiling...
cmake --build . --config Release
if errorlevel 1 (
  echo Error: Build failed.
  exit /b 1
)

echo.
echo ===================================
echo BUILD SUCCESSFUL
echo ===================================
cd ..
exit /b 0
