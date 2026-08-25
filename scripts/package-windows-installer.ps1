param(
    [string]$Version = "v0.1.0-alpha.1",
    [string]$InputZip = "",
    [string]$OutputExe = ""
)

$ErrorActionPreference = "Stop"

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$OutputRoot = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot "out"))
if (-not $InputZip) {
    $InputZip = Join-Path $OutputRoot "kachakachaCAD-$Version-windows-x64.zip"
}
if (-not $OutputExe) {
    $OutputExe = Join-Path $OutputRoot "kachakachaCAD-$Version-windows-x64-setup.exe"
}
$InputZip = [System.IO.Path]::GetFullPath($InputZip)
$OutputExe = [System.IO.Path]::GetFullPath($OutputExe)
$OutputPrefix = $OutputRoot.TrimEnd('\') + '\'

if (-not $InputZip.StartsWith($OutputPrefix, [System.StringComparison]::OrdinalIgnoreCase) -or
    -not $OutputExe.StartsWith($OutputPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Installer input and output must be inside $OutputRoot"
}
if (-not (Test-Path -LiteralPath $InputZip)) {
    throw "Portable ZIP was not found: $InputZip"
}

$IExpress = Join-Path $env:WINDIR "System32\iexpress.exe"
if (-not (Test-Path -LiteralPath $IExpress)) {
    throw "Windows IExpress was not found: $IExpress"
}

$WorkDir = Join-Path $OutputRoot "installer-$Version"
if (Test-Path -LiteralPath $WorkDir) {
    Remove-Item -LiteralPath $WorkDir -Recurse -Force
}
New-Item -ItemType Directory -Path $WorkDir | Out-Null

$ArchiveName = [System.IO.Path]::GetFileName($InputZip)
Copy-Item -LiteralPath $InputZip -Destination (Join-Path $WorkDir $ArchiveName) -Force

$LauncherName = "install-portable.cmd"
$Launcher = @"
@echo off
setlocal
set "INSTALL_ROOT=%LOCALAPPDATA%\kachakachaCAD\$Version"
echo Preparing kachakachaCAD $Version...
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -Command "`$ErrorActionPreference='Stop'; [void](New-Item -ItemType Directory -Path '%INSTALL_ROOT%' -Force); Expand-Archive -LiteralPath '%~dp0$ArchiveName' -DestinationPath '%INSTALL_ROOT%' -Force"
if errorlevel 1 (
    echo Failed to extract the application.
    pause
    exit /b 1
)
set "APP=%INSTALL_ROOT%\kachakachaCAD\kachakacha_cad.exe"
set "PROJECT=%INSTALL_ROOT%\kachakachaCAD\review-model.kcd"
if not exist "%APP%" (
    echo kachakacha_cad.exe was not found after extraction.
    pause
    exit /b 1
)
start "" "%APP%" --project "%PROJECT%"
exit /b 0
"@
$LauncherPath = Join-Path $WorkDir $LauncherName
$Launcher | Set-Content -LiteralPath $LauncherPath -Encoding ASCII

$SedPath = Join-Path $WorkDir "installer.sed"
$Sed = @"
[Version]
Class=IEXPRESS
SEDVersion=3

[Options]
PackagePurpose=InstallApp
ShowInstallProgramWindow=0
HideExtractAnimation=0
UseLongFileName=1
InsideCompressed=0
CAB_FixedSize=0
CAB_ResvCodeSigning=0
RebootMode=N
InstallPrompt=%InstallPrompt%
DisplayLicense=%DisplayLicense%
FinishMessage=%FinishMessage%
TargetName=%TargetName%
FriendlyName=%FriendlyName%
AppLaunched=%AppLaunched%
PostInstallCmd=%PostInstallCmd%
AdminQuietInstCmd=%AdminQuietInstCmd%
UserQuietInstCmd=%UserQuietInstCmd%
SourceFiles=SourceFiles

[Strings]
InstallPrompt=
DisplayLicense=
FinishMessage=
TargetName=$OutputExe
FriendlyName=kachakachaCAD $Version Windows 64-bit
AppLaunched=cmd.exe /d /c $LauncherName
PostInstallCmd=<None>
AdminQuietInstCmd=
UserQuietInstCmd=
FILE0="$ArchiveName"
FILE1="$LauncherName"

[SourceFiles]
SourceFiles0=$WorkDir\

[SourceFiles0]
%FILE0%=
%FILE1%=
"@
$Sed | Set-Content -LiteralPath $SedPath -Encoding ASCII

if (Test-Path -LiteralPath $OutputExe) {
    Remove-Item -LiteralPath $OutputExe -Force
}
$Process = Start-Process -FilePath $IExpress -ArgumentList @("/N", $SedPath) -Wait -PassThru
if ($Process.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $OutputExe)) {
    throw "IExpress failed to create the installer"
}

$Hash = (Get-FileHash -LiteralPath $OutputExe -Algorithm SHA256).Hash.ToLowerInvariant()
Write-Host "Installer: $OutputExe"
Write-Host "SHA256: $Hash"
