# Packages build\Release\crisp.exe + README.md as Crisp-v<version>.zip on the Desktop, taking the
# version from project(... VERSION x.y.z) in prototype/CMakeLists.txt. Build Release first.
# Usage (from the repo root): powershell -File prototype\tools\make_zip.ps1
$ErrorActionPreference = "Stop"
$repo = Resolve-Path "$PSScriptRoot\..\.."
$cmake = Get-Content "$repo\prototype\CMakeLists.txt" -Raw
if ($cmake -notmatch 'project\(Crisp VERSION ([0-9.]+)') { throw "no VERSION in prototype/CMakeLists.txt" }
$version = $Matches[1]
$exe = "$repo\build\Release\crisp.exe"
if (-not (Test-Path $exe)) { throw "build Release first: $exe is missing" }
$zip = Join-Path ([Environment]::GetFolderPath("Desktop")) "Crisp-v$version.zip"
Compress-Archive -Path $exe, "$repo\prototype\README.md" -DestinationPath $zip -Force
Write-Output $zip
