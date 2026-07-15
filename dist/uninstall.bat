@echo off
setlocal EnableExtensions
title IrfanView EXR Layer Plugin - Uninstaller

REM --- test hook (see install.bat) ---
if defined EXR_INSTALL_TEST_DIR (
  set "PLUG=%EXR_INSTALL_TEST_DIR%"
  set "TESTMODE=1"
  goto :havedir
)

net session >nul 2>&1
if %errorlevel% neq 0 (
  powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
  exit /b
)

echo (You may leave IrfanView open; closing it first is cleaner.)
echo.
pause

set "PLUG=%ProgramFiles%\IrfanView\Plugins"

:havedir
if not exist "%PLUG%" (
  echo IrfanView Plugins folder not found at "%PLUG%".
  if not defined TESTMODE pause
  exit /b 1
)
set "EXR=%PLUG%\EXR.dll"
set "BACKUP=%PLUG%\EXR_original_backup.dll"

REM Move our EXR.dll aside by RENAME (works even if IrfanView still has it
REM loaded; a plain delete would fail on a loaded DLL).
if not exist "%EXR%" goto :restore
ren "%EXR%" "EXR_removed_%RANDOM%%RANDOM%.dll" 2>nul
if exist "%EXR%" goto :locked

:restore
if exist "%BACKUP%" (
  ren "%BACKUP%" "EXR.dll" 2>nul
  if not exist "%EXR%" goto :restorefail
  echo Restored the original stock EXR.dll.
) else (
  echo Removed the plugin. No stock backup was present.
  echo ^(Reinstall IrfanView's PlugIns pack if you need EXR support.^)
)

del /q "%PLUG%\EXR_removed_*.dll" >nul 2>&1
echo Done.
if not defined TESTMODE pause
exit /b 0

:locked
echo [!] Could not remove EXR.dll. Close IrfanView completely and re-run.
if not defined TESTMODE pause
exit /b 1

:restorefail
echo [!] Could not restore the stock EXR.dll (is one already there / locked?).
if not defined TESTMODE pause
exit /b 1
