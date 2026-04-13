@echo off
set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
set "TOOLCHAIN=D:/git/NohCam/vcpkg/scripts/buildsystems/vcpkg.cmake"

call "%VCVARS%"
if errorlevel 1 exit /b 1

if not exist build mkdir build
cd build
if errorlevel 1 exit /b 1

cmake .. -DCMAKE_TOOLCHAIN_FILE=%TOOLCHAIN% -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b 1

cmake --build . --config Release
if errorlevel 1 exit /b 1

echo BUILD_SUCCESS
