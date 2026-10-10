[CmdletBinding()]
param(
  [string]$PlatformIOPath = 'pio'
)

$ErrorActionPreference = 'Stop'
$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$safeRoot = $repoRoot.Replace('\', '/')
$headerPath = Join-Path $repoRoot 'include\Version.h'
$header = [System.IO.File]::ReadAllText($headerPath)
$versionMatch = [regex]::Match(
  $header,
  'constexpr\s+char\s+FIRMWARE_VERSION_TEXT\[\]\s*=\s*"([0-9]+\.[0-9]+\.[0-9]+)"\s*;'
)
if (-not $versionMatch.Success) {
  throw 'Could not read the firmware version from include/Version.h.'
}
$version = $versionMatch.Groups[1].Value

$changes = & git -c "safe.directory=$safeRoot" -C $repoRoot status --porcelain --untracked-files=all
if ($LASTEXITCODE -ne 0) {
  throw 'Could not check the Git worktree.'
}
if ($changes) {
  throw 'Release packaging requires a clean, committed Git worktree.'
}

Push-Location $repoRoot
try {
  & $PlatformIOPath run -e megaatmega2560
  if ($LASTEXITCODE -ne 0) {
    throw 'PlatformIO build failed; no release assets were packaged.'
  }
} finally {
  Pop-Location
}

$builtHex = Join-Path $repoRoot '.pio\build\megaatmega2560\firmware.hex'
if (-not (Test-Path -LiteralPath $builtHex -PathType Leaf)) {
  throw 'PlatformIO did not produce firmware.hex.'
}

$baseName = "cellq-mega2560-v$version"
$hexName = "$baseName.hex"
$checksumName = "$baseName.sha256"
$outputDir = Join-Path $repoRoot 'dist'
$releaseHex = Join-Path $outputDir $hexName
$releaseChecksum = Join-Path $outputDir $checksumName
if ((Test-Path -LiteralPath $releaseHex) -or
    (Test-Path -LiteralPath $releaseChecksum)) {
  throw "Release assets already exist in $outputDir; they will not be overwritten."
}

New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
Copy-Item -LiteralPath $builtHex -Destination $releaseHex
$hash = (Get-FileHash -LiteralPath $releaseHex -Algorithm SHA256).Hash.ToLowerInvariant()
$checksumLine = "$hash  $hexName`n"
$utf8WithoutBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($releaseChecksum, $checksumLine, $utf8WithoutBom)

Write-Output "Firmware: $releaseHex"
Write-Output "Checksum: $releaseChecksum"
Write-Output "SHA-256: $hash"
