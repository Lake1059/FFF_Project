@echo off
REM ===========================================================================
REM Build gainmap_probe.exe standalone (Release x64).
REM
REM Paths derive from this script's own location (%~dp0 = FFF.Native\tools\).
REM Override any of these by setting them in the environment BEFORE calling:
REM   FFF_ROOT        repo root   (default: two levels up from this .bat)
REM   FFF_FFMPEG_DIR  ffmpeg dir  (default: %FFF_ROOT%\third_party\ffmpeg)
REM   VCROOT          MSVC toolchain dir (default: VS18 Community 14.51.36231)
REM   SDK_VERSION     Windows 10 SDK version (default: 10.0.26100.0)
REM   FFF_LIBUNWIND_DLL  explicit path to libunwind.dll to copy (optional)
REM
REM This machine's vcvars64.bat does NOT register the Windows SDK include/lib,
REM so we set INCLUDE/LIB explicitly instead of calling vcvars.
REM ===========================================================================
setlocal

REM --- Repo root (two levels up from FFF.Native\tools\) ---
if not defined FFF_ROOT for %%I in ("%~dp0..\..") do set "FFF_ROOT=%%~fI"
if not defined FFF_FFMPEG_DIR set "FFF_FFMPEG_DIR=%FFF_ROOT%\third_party\ffmpeg"
set "ROOT=%FFF_ROOT%\FFF.Native"

REM --- Toolchain defaults (override via env) ---
if not defined VCROOT set "VCROOT=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231"
if not defined SDK_VERSION set "SDK_VERSION=10.0.26100.0"
set "SDKINC=%ProgramFiles(x86)%\Windows Kits\10\Include\%SDK_VERSION%"
set "SDKLIB=%ProgramFiles(x86)%\Windows Kits\10\Lib\%SDK_VERSION%"

set "INCLUDE=%VCROOT%\include;%VCROOT%\ATLMFC\include;%SDKINC%\ucrt;%SDKINC%\um;%SDKINC%\shared;%SDKINC%\winrt"
set "LIB=%VCROOT%\lib\x64;%VCROOT%\ATLMFC\lib\x64;%SDKLIB%\ucrt\x64;%SDKLIB%\um\x64"
set "PATH=%VCROOT%\bin\Hostx64\x64;%PATH%"

set "INC=/I%ROOT% /I%ROOT%\3FR /I%FFF_FFMPEG_DIR%\include /I%FFF_ROOT%\third_party\libraw\include"
set "LIBS=avcodec-63.lib avformat-63.lib avutil-61.lib swscale-10.lib swresample-7.lib"
set "LIBPATH=/LIBPATH:%FFF_FFMPEG_DIR%\lib\x64"

if not exist "%ROOT%\tools\build_out" mkdir "%ROOT%\tools\build_out"

REM Copy runtime DLLs next to the probe exe.
xcopy /Y /Q "%FFF_FFMPEG_DIR%\lib\x64\*.dll" "%ROOT%\tools\build_out\" >nul
if defined FFF_LIBUNWIND_DLL (
    if exist "%FFF_LIBUNWIND_DLL%" copy /Y "%FFF_LIBUNWIND_DLL%" "%ROOT%\tools\build_out\" >nul
) else (
    REM libunwind.dll ships next to ffmpeg on this machine's sibling build; copy if found.
    if exist "%FFF_ROOT%\..\3fp\build\libunwind.dll" copy /Y "%FFF_ROOT%\..\3fp\build\libunwind.dll" "%ROOT%\tools\build_out\" >nul
)

cl /O2 /EHsc /MD /std:c++17 /D_CRT_SECURE_NO_WARNINGS /DNDEBUG %INC% ^
   "%ROOT%\tools\gainmap_probe.cpp" "%ROOT%\3FP\Core\GainMapProbe.cpp" "%ROOT%\3FP\Core\GainMapJpeg.cpp" ^
   /Fe:"%ROOT%\tools\build_out\gainmap_probe.exe" ^
   /Fo:"%ROOT%\tools\build_out\\" ^
   /link %LIBPATH% %LIBS%

echo EXIT=%ERRORLEVEL%
endlocal
