param(
    [string]$Version = ""
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

if (-not $Version) {
    $mainCpp = Join-Path $root 'main.cpp'
    $text = Get-Content -Raw -Encoding UTF8 $mainCpp
    $m = [regex]::Match($text, 'setApplicationVersion\(QStringLiteral\("([0-9][0-9A-Za-z.\-]*)"\)\)')
    if (-not $m.Success) {
        throw "cannot detect version from main.cpp; pass -Version x.y.z"
    }
    $Version = $m.Groups[1].Value
}

$dist = Join-Path $root 'dist'
if (-not (Test-Path (Join-Path $dist 'AutoRun_Codex.exe'))) {
    throw "dist\AutoRun_Codex.exe not found; run build.cmd then deploy.cmd first"
}

$stageName = "AutoRun_Codex-v$Version-win64"
$releaseRoot = Join-Path $root 'release'
$stage = Join-Path $releaseRoot $stageName
if (Test-Path $releaseRoot) {
    Remove-Item $releaseRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $stage -Force | Out-Null

Get-ChildItem $dist -Force | Where-Object { $_.Name -notlike '*.old-*' } | ForEach-Object {
    Copy-Item $_.FullName -Destination (Join-Path $stage $_.Name) -Recurse -Force
}

$readmeTemplate = Get-Content -Raw -Encoding UTF8 (Join-Path $PSScriptRoot 'README.txt')
$readme = $readmeTemplate.Replace('__VERSION__', $Version)
Set-Content -Path (Join-Path $stage 'README.txt') -Value $readme -Encoding UTF8

$zip = Join-Path $releaseRoot "$stageName.zip"
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip -CompressionLevel Optimal -Force
$hash = (Get-FileHash $zip -Algorithm SHA256).Hash
Set-Content -Path "$zip.sha256" -Value ("$hash  $stageName.zip") -Encoding ASCII
$sizeMB = [math]::Round((Get-Item $zip).Length / 1MB, 1)

Write-Output "version : $Version"
Write-Output "folder  : $stage"
Write-Output "zip     : $zip ($sizeMB MB)"
Write-Output "sha256  : $hash"
