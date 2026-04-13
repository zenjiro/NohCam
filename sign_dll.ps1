$ErrorActionPreference = "Continue"

# Create a self-signed certificate
$cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject "CN=NohCam Dev" -CertStoreLocation Cert:\CurrentUser\My -NotAfter (Get-Date).AddYears(1)
Write-Host "Created certificate: $($cert.Thumbprint)"

# Export the certificate to PFX
$password = ConvertTo-SecureString -String "password123" -Force -AsPlainText
$pfxPath = "$env:TEMP\nohcam_dev.pfx"
Export-PfxCertificate -Cert $cert -FilePath $pfxPath -Password $password
Write-Host "Exported PFX to: $pfxPath"

# Sign the DLL
$signtool = "C:\Program Files (x86)\Windows Kits\10\App Certification Kit\signtool.exe"
$dllPath = "src\NohCam.WinUI\bin\Release\net8.0-windows10.0.19041.0\nohcam_bridge.dll"

& $signtool sign /f $pfxPath /p password123 /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 $dllPath
Write-Host "Signed DLL"

# Clean up
Remove-Item $pfxPath -Force -ErrorAction SilentlyContinue
Write-Host "Done"
