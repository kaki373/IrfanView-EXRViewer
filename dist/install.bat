@echo off
setlocal EnableExtensions
title IrfanView EXR Layer Plugin - Installer

REM --- self-elevate to Administrator (needed to write into Program Files) ---
net session >nul 2>&1
if %errorlevel% neq 0 (
  echo Requesting administrator rights...
  powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
  exit /b
)

echo ==============================================
echo   IrfanView EXR multilayer plugin installer
echo ==============================================
echo.
echo Please CLOSE IrfanView before continuing.
pause

REM --- locate 64-bit IrfanView ---
set "IVDIR=%ProgramFiles%\IrfanView"
if not exist "%IVDIR%\i_view64.exe" (
  echo.
  echo [!] 64-bit IrfanView not found at "%IVDIR%".
  echo     This plugin is 64-bit only. If IrfanView is installed elsewhere,
  echo     copy EXR.dll manually into its \Plugins folder (see README.txt).
  echo.
  pause
  exit /b 1
)

set "PLUG=%IVDIR%\Plugins"
if not exist "%PLUG%" mkdir "%PLUG%"

REM --- back up the stock EXR.dll ONCE (keep the true original) ---
if exist "%PLUG%\EXR.dll" (
  if not exist "%PLUG%\EXR_original_backup.dll" (
    ren "%PLUG%\EXR.dll" "EXR_original_backup.dll"
    echo Backed up stock EXR.dll  -^>  EXR_original_backup.dll
  ) else (
    echo Existing backup found; leaving it intact.
    del /q "%PLUG%\EXR.dll" 2>nul
  )
)

REM --- install the custom plugin ---
copy /y "%~dp0EXR.dll" "%PLUG%\EXR.dll" >nul
if errorlevel 1 (
  echo [!] Copy failed. Is IrfanView still open? Close it and re-run.
  pause
  exit /b 1
)

echo.
echo Installed:  %PLUG%\EXR.dll
echo Backup:     %PLUG%\EXR_original_backup.dll  (do NOT delete - used as fallback)
echo.
echo DONE. Open an .exr in IrfanView, then:
echo    Ctrl+Alt+Right / Left  = next / previous layer (same window)
echo    switch to e.g. depth, then use IrfanView's next-image key to
echo    browse a sequence staying on that layer.
echo.
pause
