$ErrorActionPreference = "Continue"
$vswhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vswhere) {
    $vsPath = & $vswhere -latest -requires Microsoft.Component.MSBuild -property installationPath
    if ($vsPath) {
        $vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
        if (Test-Path $vcvars) {
            Write-Host "Found VS at: $vsPath"
            Write-Host "Running vcvars..."
            & cmd /c " `"$vcvars`" && cmake --build build --config Release 2>&1"
        }
    }
}
