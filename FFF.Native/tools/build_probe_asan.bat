@echo off
REM ===========================================================================
REM Build gainmap_probe.exe with AddressSanitizer (Debug).
REM
REM Paths derive from %~dp0. Override via env BEFORE calling:
REM   FFF_ROOT        repo root   (default: two levels up from this .bat)
REM   FFF_FFMPEG_DIR  ffmpeg dir  (default: %FFF_ROOT%\third_party\ffmpeg)
REM   SDK_VERSION     Windows 10 SDK version (default: 10.0.26100.0)
REM
REM NOTE: vcvars64.bat on some machines does NOT register the Windows SDK
REM include/lib paths. We therefore append them explicitly AFTER the call.
REM ===========================================================================
setlocal

if not defined FFF_ROOT for %%I in ("%~dp0..\..") do set "FFF_ROOT=%%~fI"
if not defined FFF_FFMPEG_DIR set "FFF_FFMPEG_DIR=%FFF_ROOT%\third_party\ffmpeg"
set "ROOT=%FFF_ROOT%\FFF.Native"

call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul

REM --- Append Windows SDK include/lib if vcvars did not register it ---
if not defined SDK_VERSION set "SDK_VERSION=10.0.26100.0"
set "SDKINC=%ProgramFiles(x86)%\Windows Kits\10\Include\%SDK_VERSION%"
set "SDKLIB=%ProgramFiles(x86)%\Windows Kits\10\Lib\%SDK_VERSION%"
set "INCLUDE=%SDKINC%\ucrt;%SDKINC%\um;%SDKINC%\shared;%SDKINC%\winrt;%INCLUDE%"
set "LIB=%SDKLIB%\ucrt\x64;%SDKLIB%\um\x64;%LIB%"

set "INC=/I"%ROOT%" /I"%ROOT%\3FR" /I"%FFF_FFMPEG_DIR%\include" /I"%FFF_ROOT%\third_party\libraw\include"
set "LIBS=avcodec-63.lib avformat-63.lib avutil-61.lib swscale-10.lib swresample-7.lib"
set "LIBPATH=/LIBPATH:"%FFF_FFMPEG_DIR%\lib\x64""

if not exist "%ROOT%\tools\build_out" mkdir "%ROOT%\tools\build_out"

cl /Od /Zi /EHsc /MDd /std:c++17 /fsanitize=address /D_CRT_SECURE_NO_WARNINGS %INC% ^
   "%ROOT%\tools\gainmap_probe.cpp" "%ROOT%\3FP\Core\GainMapProbe.cpp" ^
   /Fe:"%ROOT%\tools\build_out\gainmap_probe_asan.exe" ^
   /Fo:"%ROOT%\tools\build_out\\" ^
   /link %LIBPATH% %LIBS%

echo EXIT=%ERRORLEVEL%
endlocal
