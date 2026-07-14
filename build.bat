@echo off
setlocal EnableExtensions
REM Build EXR.dll (64-bit, static CRT) from src\exrplugin.cpp.
REM Output: dist\EXR.dll  (imports only USER32/KERNEL32 - no VC++ runtime dep)

cd /d "%~dp0"

REM --- ensure the 64-bit MSVC toolchain is on PATH ---
where cl >nul 2>&1
if errorlevel 1 call :find_vcvars || exit /b 1

if not exist dist mkdir dist

cl /O2 /EHsc /nologo /std:c++17 /MT /LD ^
   /DTINYEXR_IMPLEMENTATION /DTINYEXR_USE_MINIZ=0 /DTINYEXR_USE_STB_ZLIB=1 ^
   /I third_party ^
   src\exrplugin.cpp ^
   /Fe:dist\EXR.dll ^
   /link /DEF:src\exrplugin.def user32.lib gdi32.lib kernel32.lib

if errorlevel 1 (
  echo [!] Build failed.
  exit /b 1
)

del /q exrplugin.obj dist\EXR.exp dist\EXR.lib 2>nul
echo.
echo Built: dist\EXR.dll
exit /b 0

:find_vcvars
for %%E in (BuildTools Community Professional Enterprise) do (
  if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat" (
    call "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat" >nul
    exit /b 0
  )
  if exist "%ProgramFiles%\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat" (
    call "%ProgramFiles%\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat" >nul
    exit /b 0
  )
)
echo [!] Could not find vcvars64.bat. Install "Desktop development with C++"
echo     ^(Visual Studio 2022 or Build Tools^) and re-run.
exit /b 1
