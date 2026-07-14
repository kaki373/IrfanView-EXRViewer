@echo off
setlocal EnableExtensions
title IrfanView EXR Layer Plugin - Uninstaller

net session >nul 2>&1
if %errorlevel% neq 0 (
  powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
  exit /b
)

echo Please CLOSE IrfanView before continuing.
pause

set "PLUG=%ProgramFiles%\IrfanView\Plugins"
if not exist "%PLUG%" (
  echo IrfanView Plugins folder not found at "%PLUG%".
  pause & exit /b 1
)

if exist "%PLUG%\EXR_original_backup.dll" (
  del /q "%PLUG%\EXR.dll" 2>nul
  ren "%PLUG%\EXR_original_backup.dll" "EXR.dll"
  echo Restored the original stock EXR.dll.
) else (
  del /q "%PLUG%\EXR.dll" 2>nul
  echo Removed custom EXR.dll. No stock backup was present.
  echo (If you need EXR support, reinstall IrfanView's PlugIns pack.)
)
echo Done.
pause
