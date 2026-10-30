param([Parameter(Mandatory=$true)][string]$BuildDir,
      [Parameter(Mandatory=$true)][string]$OutputDir)
$ErrorActionPreference = "Stop"
$manifest = Join-Path $BuildDir 'compiler-runtime-release.txt'
cmake "-DMANIFEST=$manifest" "-DOUTPUT=$OutputDir" -P (Join-Path $PSScriptRoot 'copy-compiler-runtime.cmake')
if ($LASTEXITCODE -ne 0) { throw "Compiler runtime deployment failed" }
