@echo off
chcp 65001 >nul
setlocal
title ServicesJob installer
echo ==========================================
echo  ServicesJob - installer
echo ==========================================
echo.
echo Drag your GTA V folder (the one that contains GTA5.exe) onto this window,
echo then press Enter.
echo.
set "GAME="
set /p GAME=Game folder:
set "GAME=%GAME:"=%"
if not exist "%GAME%\GTA5.exe" (
    echo.
    echo [ERROR] GTA5.exe was not found in:  %GAME%
    echo Make sure you picked the folder that contains GTA5.exe.
    pause
    exit /b 1
)
if not exist "%GAME%\ScriptHookV.dll" (
    echo.
    echo [WARNING] ScriptHookV.dll was not found in that folder.
    echo ServicesJob needs Script Hook V: http://www.dev-c.com/gtav/scripthookv/
    echo Installing anyway...
)
if not exist "%~dp0ServicesJob.asi" (
    echo.
    echo [ERROR] ServicesJob.asi is not next to this installer.
    pause
    exit /b 1
)
copy /Y "%~dp0ServicesJob.asi" "%GAME%\ServicesJob.asi" >nul
if errorlevel 1 (
    echo.
    echo [ERROR] Could not copy the file. Close the game and run this again
    echo ^(if the game is installed under Program Files, run as administrator^).
    pause
    exit /b 1
)
echo.
echo Installed: %GAME%\ServicesJob.asi
echo Start the game ^(story mode^) and press F9.
echo.
pause
