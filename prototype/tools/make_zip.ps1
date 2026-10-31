# Publishes the Release build to Desktop\Game Builds\Crisp (owner, 2026-10-31): Versions\Crisp-v<version>\
# (unzipped), Zips\Crisp-v<version>.zip, and Latest\crisp.exe (the one to pin: updated in place, your config, stats,
# inventory and replays there stay). The work is done by Desktop\Game Builds\publish-crisp.ps1. Build Release first.
# Usage (from the repo root): powershell -ExecutionPolicy Bypass -File prototype\tools\make_zip.ps1
$ErrorActionPreference = "Stop"
$publish = Join-Path ([Environment]::GetFolderPath("Desktop")) "Game Builds\publish-crisp.ps1"
if (-not (Test-Path $publish)) { throw "missing $publish (the Game Builds folder on the Desktop)" }
& $publish
