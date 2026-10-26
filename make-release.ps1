# Builds ServicesJob.asi and creates the two release packages in dist\:
#   ServicesJob-v<ver>-install.zip   for players  (asi + install.bat + READMEs/manual + font notice)
#   ServicesJob-v<ver>-source.zip    for developers (sources, docs, build scripts; NO Script Hook V SDK)
param([string]$Version = "1.1")
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$utf8bom = New-Object System.Text.UTF8Encoding($true)

& "$root\build.ps1" -NoDeploy
if ($LASTEXITCODE -ne 0 -and $null -ne $LASTEXITCODE) { throw "build failed" }
$asi = "$root\bin\ServicesJob.asi"
if (-not (Test-Path $asi)) { throw "bin\ServicesJob.asi not found" }

$dist = "$root\dist"
New-Item -ItemType Directory -Force $dist | Out-Null
$userDir = "$dist\ServicesJob-v$Version-install"
$srcDir = "$dist\ServicesJob-v$Version-source"
foreach ($d in $userDir, $srcDir) { if (Test-Path $d) { Remove-Item $d -Recurse -Force }; New-Item -ItemType Directory -Force $d | Out-Null }

# text files get CRLF line endings and a UTF-8 BOM so Notepad shows Thai correctly
function Copy-Text($from, $to) {
    $t = [IO.File]::ReadAllText($from)
    $t = $t -replace "`r?`n", "`r`n"
    [IO.File]::WriteAllText($to, $t, $utf8bom)
}

# ---- players
Copy-Item $asi "$userDir\ServicesJob.asi"
Copy-Text "$root\docs\README-TH.txt" "$userDir\README-TH.txt"
Copy-Text "$root\docs\USER-MANUAL-TH.md" "$userDir\USER-MANUAL-TH.md"
Copy-Text "$root\src\FONT_NOTICE.txt" "$userDir\FONT_NOTICE.txt"
$bat = [IO.File]::ReadAllText("$root\docs\install.bat") -replace "`r?`n", "`r`n"
[IO.File]::WriteAllText("$userDir\install.bat", $bat, (New-Object System.Text.UTF8Encoding($false)))

# ---- developers (no SDK: its licence forbids redistribution)
New-Item -ItemType Directory -Force "$srcDir\src", "$srcDir\docs", "$srcDir\sdk" | Out-Null
Copy-Item "$root\src\*" "$srcDir\src" -Recurse
Copy-Item "$root\docs\*" "$srcDir\docs" -Recurse
Copy-Item "$root\sdk\README.md" "$srcDir\sdk\README.md"
foreach ($f in "build.ps1", "make-release.ps1", "README.md", "CHANGELOG.md", ".gitignore") { Copy-Item "$root\$f" "$srcDir\$f" }
if (Test-Path "$root\simulator.html") { Copy-Item "$root\simulator.html" "$srcDir\docs\simulator.html" }

# ---- zip
foreach ($pair in @(@($userDir, "$dist\ServicesJob-v$Version-install.zip"), @($srcDir, "$dist\ServicesJob-v$Version-source.zip"))) {
    if (Test-Path $pair[1]) { Remove-Item $pair[1] -Force }
    Compress-Archive -Path "$($pair[0])\*" -DestinationPath $pair[1] -CompressionLevel Optimal
}

Write-Host ""
Write-Host "Release packages:"
Get-ChildItem $dist -Filter "*.zip" | ForEach-Object { "  {0}  ({1:N0} KB)" -f $_.FullName, ($_.Length / 1KB) }
