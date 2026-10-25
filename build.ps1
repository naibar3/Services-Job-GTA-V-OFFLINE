# Build ServicesJob.asi (C++ / ScriptHookV SDK in .\sdk) and copy it into the game folder.
#   build.ps1              compile + deploy to the game folder
#   build.ps1 -NoDeploy    compile only (bin\ServicesJob.asi)
#   build.ps1 -Remove      remove the .asi from the game folder
param(
    [switch]$NoDeploy,
    [switch]$Remove,
    [string]$Game = "I:\SteamLibrary\steamapps\common\Grand Theft Auto V",
    [string]$VcVars = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
)
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot

if ($Remove) {
    Remove-Item "$Game\ServicesJob.asi" -ErrorAction SilentlyContinue
    Write-Host "removed ServicesJob.asi from $Game"
    return
}

if (-not (Test-Path $VcVars)) { throw "vcvars64.bat not found: $VcVars" }
$bin = Join-Path $root "bin"
New-Item -ItemType Directory -Force $bin | Out-Null
$obj = Join-Path $bin "obj"
New-Item -ItemType Directory -Force $obj | Out-Null

$cmd = "`"$VcVars`" >nul && cd /d `"$root`" && cl /nologo /LD /O2 /MT /EHsc /std:c++17 /utf-8 /W3 /wd4244 /wd4267 /wd4996 /I sdk /I src /Fo:`"$obj\\`" src\main.cpp /Fe:`"$bin\ServicesJob.asi`" /link /LIBPATH:sdk\lib ScriptHookV.lib gdiplus.lib user32.lib ole32.lib"
cmd /c $cmd
if ($LASTEXITCODE -ne 0) { throw "build failed ($LASTEXITCODE)" }
Remove-Item "$bin\ServicesJob.exp", "$bin\ServicesJob.lib" -ErrorAction SilentlyContinue
Write-Host "built $bin\ServicesJob.asi"

if ($NoDeploy) { return }

# the old SHVDN (C#) version must not run next to the C++ one
$oldDll = "$Game\scripts\FreeroamJobs.dll"
if (Test-Path $oldDll) {
    New-Item -ItemType Directory -Force "$root\legacy_csharp\removed_from_game" | Out-Null
    Move-Item $oldDll "$root\legacy_csharp\removed_from_game\FreeroamJobs.dll" -Force
    Write-Host "moved old FreeroamJobs.dll out of the game's scripts folder"
}
Copy-Item "$bin\ServicesJob.asi" "$Game\ServicesJob.asi" -Force
Write-Host "deployed to $Game\ServicesJob.asi (restart the game to load it)"
