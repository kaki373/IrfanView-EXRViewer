# Building from source

The plugin statically links **OpenEXR 3.3 + Imath** (built with the static CRT,
`/MT`). You need those static libraries before running `build.bat`.

## 1. Get static OpenEXR libraries

Either option produces an install prefix containing `include\` and `lib\`.

### Option A — vcpkg (simplest)
```bat
git clone https://github.com/microsoft/vcpkg
vcpkg\bootstrap-vcpkg.bat
vcpkg\vcpkg install openexr:x64-windows-static
```
The `x64-windows-static` triplet builds everything with `/MT`. The prefix is
`vcpkg\installed\x64-windows-static` — set `OPENEXR_ROOT` to it.

### Option B — from source (CMake)
Build Imath then OpenEXR as static, `/MT` libraries. Key CMake flags:
```
-DBUILD_SHARED_LIBS=OFF
-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded
-DCMAKE_POLICY_DEFAULT_CMP0091=NEW    (required: their cmake_minimum is <3.15,
                                       so CMP0091 defaults to OLD and otherwise
                                       ignores the runtime-library setting)
```
OpenEXR 3.x vendors libdeflate for ZIP; point `FETCHCONTENT_SOURCE_DIR_DEFLATE`
at a libdeflate source tree if you have no network. Install both to a common
prefix and set `OPENEXR_ROOT` to it.

## 2. Build

```bat
set OPENEXR_ROOT=C:\path\to\openexr-install
build.bat
```

`build.bat` auto-detects VS 2022 (Build Tools / Community / Pro / Enterprise).
Output: `dist\EXR.dll`.

## 3. Note on library names

`build.bat` links `OpenEXR-3_3.lib`, `Imath-3_1.lib`, etc. Those version
suffixes depend on the OpenEXR/Imath versions you built. If yours differ, adjust
the `.lib` names in `build.bat` to match what's in `%OPENEXR_ROOT%\lib`.

## Verifying the result

`dumpbin /dependents dist\EXR.dll` should list only `USER32.dll` and
`KERNEL32.dll` — proof the CRT and all of OpenEXR/Imath are statically linked
(no VC++ runtime dependency).
