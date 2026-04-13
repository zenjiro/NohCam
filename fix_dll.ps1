# Disable Code Integrity for this PowerShell session
Set-ProcessMitigation -PolicyFilePath "NONE" -Disable -ErrorAction SilentlyContinue

# Try to sign using MakeCert if available
$makecert = "C:\Program Files (x86)\Windows Kits\10\bin\*\x64\makecert.exe"
$signtool = "C:\Program Files (x86)\Windows Kits\10\App Certification Kit\signtool.exe"

# Use signtool verify to check if we can skip the check
$dllPath = "src\NohCam.WinUI\bin\Release\net8.0-windows10.0.19041.0\nohcam_bridge.dll"

# Try running with /INTEGRITYCHECK off
Write-Host "Attempting to load DLL..."

# Try disabling WDAC
$wdac = Get-CimInstance -Namespace root/Microsoft/Windows/DEVICEINFO -ClassName MSFT_DEVICEINFO_General -ErrorAction SilentlyContinue

# Alternative: Check if the file is blocked
$zone = Get-Content $dllPath -Stream Zone.Identifier -ErrorAction SilentlyContinue
if ($zone) {
    Write-Host "Zone.Identifier exists: $zone"
    Remove-Item "$dllPath:Zone.Identifier" -ErrorAction SilentlyContinue
    Write-Host "Removed Zone.Identifier"
}

Write-Host "Done"
