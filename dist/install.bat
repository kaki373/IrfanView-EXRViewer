@echo off
setlocal EnableExtensions
title IrfanView EXR Layer Plugin - Installer

set "MARKER=IrfanView_EXR_Layer_Plugin_MARKER_kaki373"

REM --- test hook: EXR_INSTALL_TEST_DIR overrides the Plugins folder and skips
REM     elevation / prompts. Used only by the automated installer tests; a
REM     normal end user never sets this. ---
if defined EXR_INSTALL_TEST_DIR (
  set "PLUG=%EXR_INSTALL_TEST_DIR%"
  set "TESTMODE=1"
  goto :havedir
)

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
echo (You may leave IrfanView open - it keeps using the old plugin until you
echo  restart it - but closing it first is cleaner.)
echo.
pause

set "IVDIR=%ProgramFiles%\IrfanView"
if not exist "%IVDIR%\i_view64.exe" (
  echo.
  echo [!] 64-bit IrfanView not found at "%IVDIR%".
  echo     If it is installed elsewhere, copy EXR.dll into its \Plugins folder
  echo     manually (see README.txt).
  echo.
  pause
  exit /b 1
)
set "PLUG=%IVDIR%\Plugins"

:havedir
if not exist "%PLUG%" mkdir "%PLUG%"
set "EXR=%PLUG%\EXR.dll"
set "BACKUP=%PLUG%\EXR_original_backup.dll"

if not exist "%EXR%" goto :copynew

REM A stock backup already exists -> the current EXR.dll is discardable (an old
REM copy of ours, or a re-run); keep the existing backup untouched.
if exist "%BACKUP%" goto :discard

REM No backup yet: is the current EXR.dll a previous copy of THIS plugin, or the
REM genuine stock IrfanView plugin? Only the genuine stock gets backed up.
findstr /m /c:"%MARKER%" "%EXR%" >nul 2>&1
if not errorlevel 1 goto :discard

REM Genuine stock, no backup yet -> preserve it (rename works even if loaded).
ren "%EXR%" "EXR_original_backup.dll" 2>nul
if exist "%EXR%" goto :locked
echo Backed up the stock EXR.dll  -^>  EXR_original_backup.dll
goto :copynew

:discard
REM Move the current EXR.dll aside by RENAME (works even if IrfanView still has
REM it loaded; a plain delete would fail on a loaded DLL).
ren "%EXR%" "EXR_replaced_%RANDOM%%RANDOM%.dll" 2>nul
if exist "%EXR%" goto :locked

:copynew
copy /y "%~dp0EXR.dll" "%EXR%" >nul
if errorlevel 1 goto :copyfail

REM best-effort cleanup of renamed-aside copies (may be locked until IrfanView
REM exits; harmless if left behind)
del /q "%PLUG%\EXR_replaced_*.dll" >nul 2>&1

REM verify the freshly-installed DLL really is ours
findstr /m /c:"%MARKER%" "%EXR%" >nul 2>&1
if errorlevel 1 goto :verifyfail

echo.
echo Installed OK:  %EXR%
if exist "%BACKUP%" echo Stock backup:  %BACKUP%  (kept for uninstall)
echo.
echo Open an .exr in IrfanView, then:
echo    Ctrl+Alt+Right / Left  = next / previous layer (same window)
if not defined TESTMODE ( echo. & pause )
exit /b 0

:locked
echo [!] Could not update EXR.dll. Close IrfanView completely and re-run.
if not defined TESTMODE pause
exit /b 1

:copyfail
echo [!] Copy failed. Close IrfanView completely (and check permissions), re-run.
if not defined TESTMODE pause
exit /b 1

:verifyfail
echo [!] Post-install check failed: the installed EXR.dll is not the expected one.
if not defined TESTMODE pause
exit /b 1
