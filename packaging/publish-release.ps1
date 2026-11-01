param(
    [string]$Version = "",
    [string]$Repo = "Rtyu3221cc/AutoRun_Codex",
    [string]$NotesFile = ""
)

# Upload a built win64 portable zip (from release.cmd) to a GitHub Release.
# Requires git credential manager credentials for github.com (git push must work).
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File packagingpublish-release.ps1
#   ... -Version 0.1.9 -NotesFile release_release-notes-0.1.9.md

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

if (-not $Version) {
    $mainCpp = Join-Path $root 'main.cpp'
    $text = Get-Content -Raw -Encoding UTF8 $mainCpp
    $m = [regex]::Match($text, 'setApplicationVersion\(QStringLiteral\("([0-9][0-9A-Za-z.\-]*)"\)\)')
    if (-not $m.Success) { throw "cannot detect version from main.cpp; pass -Version x.y.z" }
    $Version = $m.Groups[1].Value
}
if (-not $NotesFile) {
    $NotesFile = Join-Path $root "release\_release-notes-$Version.md"
}

$zipPath = Join-Path $root "release\AutoRun_Codex-v$Version-win64.zip"
if (-not (Test-Path $zipPath)) { throw "zip not found: $zipPath (run release.cmd first)" }

# token from git credential manager (never printed)
$nl = [char]10
$credInput = 'protocol=https' + $nl + 'host=github.com' + $nl + $nl
$cred = $credInput | git credential fill
$token = $null
foreach ($line in $cred) { if ($line -like 'password=*') { $token = $line.Substring(9) } }
if (-not $token) { throw 'no GitHub credential found for github.com (run git push once)' }

$notes = "AutoRun Codex v$Version"
if (Test-Path $NotesFile) {
    $notes = [string](Get-Content -Raw -Encoding UTF8 $NotesFile)
}

Add-Type -AssemblyName System.Net.Http
$client = [System.Net.Http.HttpClient]::new()
$client.DefaultRequestHeaders.Authorization = [System.Net.Http.Headers.AuthenticationHeaderValue]::new('Bearer', $token)
$client.DefaultRequestHeaders.UserAgent.ParseAdd('AutoRunCodex-publish-release')
$client.DefaultRequestHeaders.Accept.ParseAdd('application/vnd.github+json')
$client.Timeout = [TimeSpan]::FromMinutes(10)
$apiBase = "https://api.github.com/repos/$Repo"

function Get-Body($resp) {
    return $resp.Content.ReadAsStringAsync().GetAwaiter().GetResult()
}

$rel = $null
$getResp = $client.GetAsync("$apiBase/releases/tags/v$Version").GetAwaiter().GetResult()
if ($getResp.IsSuccessStatusCode) {
    $rel = (Get-Body $getResp) | ConvertFrom-Json
    Write-Output ("release exists: id=" + $rel.id + " url=" + $rel.html_url)
} else {
    Write-Output ("creating release v$Version ...")
    $payload = @{
        tag_name = "v$Version"
        target_commitish = 'main'
        name = "AutoRun Codex v$Version"
        body = $notes
        draft = $false
        prerelease = $false
    } | ConvertTo-Json -Depth 4
    $jsonBytes = [System.Text.Encoding]::UTF8.GetBytes($payload)
    $content = [System.Net.Http.ByteArrayContent]::new($jsonBytes)
    $content.Headers.ContentType = [System.Net.Http.Headers.MediaTypeHeaderValue]::Parse('application/json; charset=utf-8')
    $resp = $client.PostAsync("$apiBase/releases", $content).GetAwaiter().GetResult()
    $body = Get-Body $resp
    Write-Output ("create http status: " + [int]$resp.StatusCode)
    if (-not $resp.IsSuccessStatusCode) {
        Write-Output ("create failed: " + $body.Substring(0, [Math]::Min(800, $body.Length)))
        throw 'release creation failed'
    }
    $rel = $body | ConvertFrom-Json
    Write-Output ("release created: id=" + $rel.id + " url=" + $rel.html_url)
}

$assetName = "AutoRun_Codex-v$Version-win64.zip"
$existing = $rel.assets | Where-Object { $_.name -eq $assetName }
if ($existing) {
    Write-Output ("asset already exists: " + $existing.browser_download_url + " (skip upload)")
    $client.Dispose()
    Write-Output 'RELEASE_OK'
    return
}

$fs = [System.IO.File]::OpenRead($zipPath)
$upContent = [System.Net.Http.StreamContent]::new($fs)
$upContent.Headers.ContentType = [System.Net.Http.Headers.MediaTypeHeaderValue]::Parse('application/zip')
$uploadUrl = "https://uploads.github.com/repos/$Repo/releases/" + $rel.id + "/assets?name=" + $assetName
$upResp = $client.PostAsync($uploadUrl, $upContent).GetAwaiter().GetResult()
$upBody = Get-Body $upResp
Write-Output ("upload http status: " + [int]$upResp.StatusCode)
if (-not $upResp.IsSuccessStatusCode) {
    Write-Output ("upload failed: " + $upBody.Substring(0, [Math]::Min(800, $upBody.Length)))
    $fs.Dispose(); $client.Dispose()
    throw 'asset upload failed'
}
$asset = $upBody | ConvertFrom-Json
Write-Output ("asset: " + $asset.name + " size=" + $asset.size + " url=" + $asset.browser_download_url)
$fs.Dispose(); $client.Dispose()
Write-Output 'RELEASE_OK'
