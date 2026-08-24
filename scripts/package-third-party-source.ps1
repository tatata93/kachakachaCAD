param(
    [string]$QtVersion = "6.9.2",
    [string]$OutputDir = "out\third-party-source",
    [string]$VcpkgRoot = "$env:USERPROFILE\vcpkg"
)

$ErrorActionPreference = "Stop"

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$OutputRoot = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot "out"))
$ResolvedOutput = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $OutputDir))
$OutputPrefix = $OutputRoot.TrimEnd('\') + '\'

if (-not $ResolvedOutput.StartsWith($OutputPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Source output must be a child directory of $OutputRoot"
}
if (-not (Test-Path $VcpkgRoot)) {
    throw "vcpkg was not found: $VcpkgRoot"
}
if (Test-Path $ResolvedOutput) {
    Remove-Item -LiteralPath $ResolvedOutput -Recurse -Force
}
New-Item -ItemType Directory -Path $ResolvedOutput -Force | Out-Null

$Cache = Join-Path $OutputRoot "source-cache"
New-Item -ItemType Directory -Path $Cache -Force | Out-Null
$QtMinor = ([Version]$QtVersion).ToString(2)
$QtArchiveName = "qtbase-everywhere-src-$QtVersion.zip"
$QtArchive = Join-Path $Cache $QtArchiveName
$QtUrl = "https://download.qt.io/official_releases/qt/$QtMinor/$QtVersion/submodules/$QtArchiveName"
if (-not (Test-Path $QtArchive)) {
    Write-Host "Downloading Qt source: $QtUrl"
    Invoke-WebRequest -Uri $QtUrl -OutFile $QtArchive
}
if ((Get-FileHash -LiteralPath $QtArchive -Algorithm SHA256).Hash -ne
    "97D59C78E40B4DDD018738D285A12AFC320B57F8265A3F760353739A3619CCDB") {
    throw "Qt source archive hash did not match the audited Qt $QtVersion package"
}
Copy-Item -LiteralPath $QtArchive -Destination (Join-Path $ResolvedOutput $QtArchiveName) -Force

$VcpkgDownloads = Join-Path $VcpkgRoot "downloads"
$SourceArchives = [ordered]@{
    "Open-Cascade-SAS-OCCT-V8_0_1.tar.gz" = "0D6913EAE4BCC09A3653CECED6DDA1AEC11C35A1513D4C06762C9B002092C68A"
    "freetype-freetype-VER-2-14-3.tar.gz" = "DC49DE6B01A266EEF4876A4DD34D9842C475D3E28FF2EFF63BD2FB760AB56261"
    "google-brotli-v1.2.0.tar.gz" = "816C96E8E8F193B40151DAD7E8FF37B1221D019DBCB9C35CD3FADBFE6477DFEC"
    "bzip2-1.0.8.tar.gz" = "AB5A03176EE106D3F0FA90E381DA478DDAE405918153CCA248E682CD0C4A2269"
    "pnggroup-libpng-v1.6.58.tar.gz" = "A9D4DF463D36A6E5F9C29BD6F4967312D17E996C1854F3511F833924EB1993CF"
    "madler-zlib-v1.3.2.tar.gz" = "B99A0B86C0BA9360EC7E78C4F1E43B1CBDF1E6936C8FA0F6835C0CD694A495A1"
}
foreach ($archiveName in $SourceArchives.Keys) {
    $archive = Join-Path $VcpkgDownloads $archiveName
    if (-not (Test-Path $archive)) {
        throw "Required vcpkg source archive was not found: $archive"
    }
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $SourceArchives[$archiveName]) {
        throw "Source archive hash did not match the audited package: $archiveName"
    }
    Copy-Item -LiteralPath $archive -Destination (Join-Path $ResolvedOutput $archiveName) -Force
}

$BuildInfo = Join-Path $ResolvedOutput "vcpkg-build-info"
New-Item -ItemType Directory -Path $BuildInfo -Force | Out-Null
foreach ($packageName in @("opencascade", "freetype", "brotli", "bzip2", "libpng", "zlib")) {
    $port = Join-Path $VcpkgRoot "ports\$packageName"
    if (-not (Test-Path $port)) {
        throw "Required vcpkg port was not found: $port"
    }
    Copy-Item -LiteralPath $port -Destination $BuildInfo -Recurse -Force
}
$Triplet = Join-Path $VcpkgRoot "triplets\x64-windows.cmake"
if (-not (Test-Path $Triplet)) {
    throw "vcpkg triplet was not found: $Triplet"
}
Copy-Item -LiteralPath $Triplet -Destination (Join-Path $BuildInfo "x64-windows.cmake") -Force
Copy-Item -LiteralPath (Join-Path $RepoRoot "CMakeLists.txt") -Destination (Join-Path $BuildInfo "kachakachaCAD-CMakeLists.txt") -Force

$VcpkgRevision = & git -c "safe.directory=$VcpkgRoot" -C $VcpkgRoot rev-parse HEAD
if ($LASTEXITCODE -ne 0) {
    throw "Could not determine the vcpkg revision"
}
$Manifest = @(
    "kachakachaCAD third-party corresponding source",
    "Generated: $([DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ'))",
    "Qt: $QtVersion",
    "Open CASCADE Technology: 8.0.1",
    "FreeType: 2.14.3",
    "Brotli: 1.2.0",
    "bzip2: 1.0.8",
    "libpng: 1.6.58",
    "zlib: 1.3.2",
    "vcpkg revision: $VcpkgRevision",
    "",
    "The archives contain the corresponding library source. The vcpkg-build-info",
    "directory contains the port files and triplet used to build the shipped DLLs."
)
$Manifest | Set-Content -LiteralPath (Join-Path $ResolvedOutput "README.txt") -Encoding UTF8

$Hashes = Get-ChildItem -LiteralPath $ResolvedOutput -File | Get-FileHash -Algorithm SHA256
$Hashes | ForEach-Object { "$($_.Hash.ToLowerInvariant())  $([System.IO.Path]::GetFileName($_.Path))" } |
    Set-Content -LiteralPath (Join-Path $ResolvedOutput "SHA256SUMS.txt") -Encoding ASCII

$ArchivePath = Join-Path $OutputRoot "kachakachaCAD-third-party-source.zip"
if (Test-Path $ArchivePath) {
    Remove-Item -LiteralPath $ArchivePath -Force
}
Compress-Archive -LiteralPath $ResolvedOutput -DestinationPath $ArchivePath -CompressionLevel Optimal
Write-Host "Third-party source: $ArchivePath"
