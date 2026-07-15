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
  echo     If IrfanView is installed elsewhere, copy EXR.dll into its
  echo     \Plugins folder manually (see README.txt).
  echo.
  pause
  exit /b 1
)

set "PLUG=%IVDIR%\Plugins"
if not exist "%PLUG%" mkdir "%PLUG%"
set "EXR=%PLUG%\EXR.dll"
set "BACKUP=%PLUG%\EXR_original_backup.dll"
set "MARKER=IrfanView_EXR_Layer_Plugin_MARKER_kaki373"

if not exist "%EXR%" goto :install

if exist "%BACKUP%" (
  REM A stock backup already exists from a prior install -> keep it, just
  REM replace whatever EXR.dll is there now (old stock backup stays intact).
  echo Updating over an existing install; keeping the existing stock backup.
  del /q "%EXR%"
  goto :install
)

REM No backup yet: is the current EXR.dll a previous copy of THIS plugin, or
REM the genuine stock IrfanView plugin? Only the genuine stock gets backed up.
findstr /m /c:"%MARKER%" "%EXR%" >nul 2>&1
if %errorlevel%==0 (
  echo Existing EXR.dll is a previous copy of this plugin; replacing it.
  echo   ^(No genuine stock backup was present - nothing to preserve.^)
  del /q "%EXR%"
) else (
  ren "%EXR%" "EXR_original_backup.dll"
  echo Backed up the stock EXR.dll  -^>  EXR_original_backup.dll
)

:install
copy /y "%~dp0EXR.dll" "%EXR%" >nul
if errorlevel 1 (
  echo [!] Copy failed. Is IrfanView still open? Close it and re-run.
  pause
  exit /b 1
)

echo.
echo Installed:  %EXR%
if exist "%BACKUP%" echo Backup:     %BACKUP%  ^(kept for uninstall^)
echo.
echo DONE. Open an .exr in IrfanView, then:
echo    Ctrl+Alt+Right / Left  = next / previous layer (same window)
echo.
pause
