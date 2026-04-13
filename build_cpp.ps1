$ErrorActionPreference = "Continue"
$vswhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vswhere) {
    $vsPath = & $vswhere -latest -requires Microsoft.Component.MSBuild -property installationPath
    if ($vsPath) {
        $vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
        if (Test-Path $vcvars) {
            Write-Host "Found VS at: $vsPath"
            Write-Host "Building project..."
            
            # Use a command string that runs vcvars, then sets up cmake, then builds
            $cmd = "call `"$vcvars`" && " +
                   "if not exist build mkdir build && " +
                   "cd build && " +
                   "cmake .. -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_BUILD_TYPE=Release && " +
                   "cmake --build . --config Release"
            
            & cmd /c $cmd
        }
    }
}
