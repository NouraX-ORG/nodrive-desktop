# SPDX-FileCopyrightText: 2026 Nourax
# SPDX-License-Identifier: GPL-2.0-or-later
#
# NoDrive: build the MSI in CI from a Craft build (.github/workflows/windows.yml).
#
# Input: a Craft root where "craft --package nextcloud-client" ran with the
# PortablePackager (admin/win/nodrive-craftmaster.ini) and the client was
# configured with -DBUILD_WIN_MSI=ON. Craft then leaves
#   <root>\build\nextcloud-client\archive\bin   the app, its DLLs, Qt plugins and QML
#   <root>\build\nextcloud-client\archive\i18n  the app translations
#   <root>\build\nextcloud-client\archive\msi   the generated MSI scripts (admin/win/msi)
# This script collects the app into one flat folder (the layout make-msi.bat
# expects: exe at the top, i18n\, sync-exclude.lst, <icon>.ico, licenses\),
# finds WiX 3 and the Windows SDK tools, runs make-msi.bat and copies the MSI
# to -OutDir. It never signs anything.

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string] $CraftRoot,
    [Parameter(Mandatory = $true)][string] $SourceDir,
    [Parameter(Mandatory = $true)][string] $OutDir,
    [string] $WorkDir = (Join-Path $env:RUNNER_TEMP 'nodrive-msi'),
    [string] $PackageName = 'nextcloud-client'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Find-First([string[]] $candidates) {
    foreach ($c in $candidates) { if ($c -and (Test-Path -LiteralPath $c)) { return (Resolve-Path -LiteralPath $c).Path } }
    return $null
}

$buildRoot = Join-Path $CraftRoot "build\$PackageName"
$archive = Join-Path $buildRoot 'archive'
$buildDir = Join-Path $buildRoot 'work\build'
if (-not (Test-Path -LiteralPath (Join-Path $archive 'bin'))) { throw "Craft archive not found: $archive\bin (did 'craft --package' run?)" }

# The MSI scripts: from the collected archive, else from the install image.
$msiSrc = Find-First @((Join-Path $archive 'msi'))
if (-not $msiSrc) {
    $img = Get-ChildItem -LiteralPath $buildRoot -Directory -Filter 'image-*' | Where-Object { $_.Name -notlike '*-dbg' } | Select-Object -First 1
    if ($img) { $msiSrc = Find-First @((Join-Path $img.FullName 'msi')) }
}
if (-not $msiSrc) { throw "No msi\ folder in $archive or the image dir: was the client configured with -DBUILD_WIN_MSI=ON?" }
Write-Host ":: MSI scripts: $msiSrc"

# Names come from the generated OEM.wxi, so this works for any branding.
$oem = Get-Content -LiteralPath (Join-Path $msiSrc 'OEM.wxi') -Raw
function WxiDefine([string] $name) {
    $m = [regex]::Match($oem, "<\?define\s+$name\s*=\s*""([^""]*)""\s*\?>")
    if (-not $m.Success) { throw "OEM.wxi: no define $name" }
    return $m.Groups[1].Value
}
$appExe = WxiDefine 'AppExe'
$appIcon = WxiDefine 'AppIcon'
Write-Host ":: AppExe=$appExe AppIcon=$appIcon"

# 1. Collect the app into one flat folder.
$collect = Join-Path $WorkDir 'collect'
if (Test-Path -LiteralPath $WorkDir) { Remove-Item -LiteralPath $WorkDir -Recurse -Force }
New-Item -ItemType Directory -Force -Path $collect | Out-Null
Copy-Item -Path (Join-Path $archive 'bin\*') -Destination $collect -Recurse -Force
if (Test-Path -LiteralPath (Join-Path $archive 'i18n')) {
    Copy-Item -LiteralPath (Join-Path $archive 'i18n') -Destination (Join-Path $collect 'i18n') -Recurse -Force
}
$exclude = Get-ChildItem -LiteralPath $archive -Recurse -File -Filter 'sync-exclude.lst' | Where-Object { $_.FullName -notlike "$collect*" } | Select-Object -First 1
if ($exclude) { Copy-Item -LiteralPath $exclude.FullName -Destination $collect -Force } else { Write-Warning 'sync-exclude.lst not found in the archive' }
$ico = Find-First @((Join-Path $buildDir "src\gui\$appIcon"))
if (-not $ico) {
    $found = Get-ChildItem -LiteralPath $buildDir -Recurse -File -Filter $appIcon -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($found) { $ico = $found.FullName }
}
if (-not $ico) { throw "App icon $appIcon not found in $buildDir" }
Copy-Item -LiteralPath $ico -Destination (Join-Path $collect $appIcon) -Force
# Debug symbols do not go into the installer.
Get-ChildItem -LiteralPath $collect -Recurse -File -Filter '*.pdb' | Remove-Item -Force

# Licence texts ship with the app (GPL-2.0-or-later section 1; LGPL and the other
# bundled libraries). nodrive\licenses\ is filled by the shared rebrand tool once
# it is on main; until then the GPL text from the repo is shipped.
$lic = Join-Path $collect 'licenses'
New-Item -ItemType Directory -Force -Path $lic | Out-Null
Copy-Item -LiteralPath (Join-Path $SourceDir 'COPYING') -Destination (Join-Path $lic 'COPYING.txt') -Force
$shared = Join-Path $SourceDir 'nodrive\licenses'
if (Test-Path -LiteralPath $shared) { Copy-Item -Path (Join-Path $shared '*') -Destination $lic -Recurse -Force }

if (-not (Test-Path -LiteralPath (Join-Path $collect $appExe))) { throw "$appExe is missing from the collected app folder ($collect)" }
$files = Get-ChildItem -LiteralPath $collect -Recurse -File
Write-Host (":: Collected {0} files, {1:N1} MB in {2}" -f $files.Count, (($files | Measure-Object Length -Sum).Sum / 1MB), $collect)

# 2. Tools: WiX 3, uuidgen and the MSI SDK scripts (WiSubStg.vbs, WiLangId.vbs).
$wix = $env:WIX
if (-not $wix -or -not (Test-Path -LiteralPath (Join-Path $wix 'bin\heat.exe'))) {
    $wixDir = Get-ChildItem -Path "${env:ProgramFiles(x86)}\WiX Toolset v3*" -Directory -ErrorAction SilentlyContinue | Sort-Object Name -Descending | Select-Object -First 1
    if (-not $wixDir) { throw 'WiX Toolset 3 not found (expected on the windows-2022 runner image)' }
    $wix = $wixDir.FullName
}
$env:WIX = $wix.TrimEnd('\') + '\'
Write-Host ":: WIX=$env:WIX"

$kits = "${env:ProgramFiles(x86)}\Windows Kits\10\bin"
$uuidgen = Get-ChildItem -Path $kits -Recurse -File -Filter 'uuidgen.exe' -ErrorAction SilentlyContinue |
    Where-Object { $_.DirectoryName -like '*\x64' } | Sort-Object FullName -Descending | Select-Object -First 1
if (-not $uuidgen) { throw "uuidgen.exe not found under $kits" }
$env:PATH = "$($uuidgen.DirectoryName);$env:PATH"
Write-Host ":: uuidgen=$($uuidgen.FullName)"

foreach ($pair in @(@('WISUBSTG', 'WiSubStg.vbs'), @('WILANGID', 'WiLangId.vbs'))) {
    $f = Get-ChildItem -Path $kits -Recurse -File -Filter $pair[1] -ErrorAction SilentlyContinue | Sort-Object FullName -Descending | Select-Object -First 1
    if ($f) { Set-Item -Path "env:$($pair[0])" -Value $f.FullName; Write-Host ":: $($pair[0])=$($f.FullName)" }
    else { Write-Warning "$($pair[1]) not found: the MSI will be English only" }
}

# 3. Run make-msi.bat in a copy of the msi folder (it writes its output next to itself).
$msiWork = Join-Path $WorkDir 'msi'
Copy-Item -LiteralPath $msiSrc -Destination $msiWork -Recurse -Force
Push-Location $msiWork
try {
    & cmd.exe /d /c "make-msi.bat `"$collect`""
    if ($LASTEXITCODE -ne 0) { throw "make-msi.bat failed with exit code $LASTEXITCODE" }
} finally { Pop-Location }

$msis = @(Get-ChildItem -LiteralPath $msiWork -File -Filter '*.msi')
if ($msis.Count -ne 1) { throw "Expected one MSI in $msiWork, found $($msis.Count)" }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
Copy-Item -LiteralPath $msis[0].FullName -Destination $OutDir -Force
Write-Host (":: MSI: {0} ({1:N1} MB)" -f $msis[0].Name, ($msis[0].Length / 1MB))
if ($env:GITHUB_OUTPUT) {
    "msi-name=$($msis[0].Name)" | Out-File -FilePath $env:GITHUB_OUTPUT -Encoding utf8 -Append
    "collect-dir=$collect" | Out-File -FilePath $env:GITHUB_OUTPUT -Encoding utf8 -Append
}
