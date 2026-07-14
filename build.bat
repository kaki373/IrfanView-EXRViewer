@echo off
setlocal EnableExtensions
REM Build EXR.dll (64-bit, static CRT) from src\exrplugin.cpp.
REM Output: dist\EXR.dll  (imports only USER32/KERNEL32 - no VC++ runtime dep)
REM
REM Requires static OpenEXR 3.3 + Imath libraries. Point OPENEXR_ROOT at the
REM install prefix (it must contain include\ and lib\). See BUILD.md.

cd /d "%~dp0"

if "%OPENEXR_ROOT%"=="" (
  echo [!] Set OPENEXR_ROOT to your static OpenEXR install prefix. See BUILD.md.
  echo     e.g.  set OPENEXR_ROOT=C:\exr_build\install
  exit /b 1
)
set "EXRINC=%OPENEXR_ROOT%\include"
set "EXRLIB=%OPENEXR_ROOT%\lib"

REM --- ensure the 64-bit MSVC toolchain is on PATH ---
where cl >nul 2>&1
if errorlevel 1 call :find_vcvars || exit /b 1

if not exist dist mkdir dist

REM NOTE: the OpenEXR .lib version suffixes (3_3 / 3_1) depend on the OpenEXR /
REM Imath version you built. Adjust if yours differ (see the names in %EXRLIB%).
cl /O2 /EHsc /nologo /std:c++17 /MT /LD ^
   /I "%EXRINC%" /I "%EXRINC%\OpenEXR" /I "%EXRINC%\Imath" ^
   src\exrplugin.cpp ^
   /Fe:dist\EXR.dll ^
   /link /DEF:src\exrplugin.def /LIBPATH:"%EXRLIB%" ^
   OpenEXRUtil-3_3.lib OpenEXR-3_3.lib OpenEXRCore-3_3.lib ^
   IlmThread-3_3.lib Iex-3_3.lib Imath-3_1.lib ^
   advapi32.lib user32.lib gdi32.lib kernel32.lib

if errorlevel 1 ( echo [!] Build failed. & exit /b 1 )

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
echo [!] Could not find vcvars64.bat. Install "Desktop development with C++".
exit /b 1
