@echo off
REM ===========================================================================
REM Build jpeg_gainmap_probe.exe standalone (Release x64). No external deps.
REM
REM Paths derive from %~dp0. Override via env BEFORE calling:
REM   FFF_ROOT        repo root   (default: two levels up from this .bat)
REM   SDK_VERSION     Windows 10 SDK version (default: 10.0.26100.0)
REM
REM NOTE: vcvars64.bat on some machines does NOT register the Windows SDK
REM include/lib paths. We therefore append them explicitly AFTER the call.
REM ===========================================================================
setlocal

if not defined FFF_ROOT for %%I in ("%~dp0..\..") do set "FFF_ROOT=%%~fI"
set "ROOT=%FFF_ROOT%\FFF.Native"

call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul

REM --- Append Windows SDK include/lib if vcvars did not register it ---
if not defined SDK_VERSION set "SDK_VERSION=10.0.26100.0"
set "SDKINC=%ProgramFiles(x86)%\Windows Kits\10\Include\%SDK_VERSION%"
set "SDKLIB=%ProgramFiles(x86)%\Windows Kits\10\Lib\%SDK_VERSION%"
set "INCLUDE=%SDKINC%\ucrt;%SDKINC%\um;%SDKINC%\shared;%SDKINC%\winrt;%INCLUDE%"
set "LIB=%SDKLIB%\ucrt\x64;%SDKLIB%\um\x64;%LIB%"

set "INC=/I"%ROOT%""

if not exist "%ROOT%\tools\build_out_jpeg" mkdir "%ROOT%\tools\build_out_jpeg"

cl /O2 /EHsc /MD /std:c++17 /D_CRT_SECURE_NO_WARNINGS /DNDEBUG %INC% ^
   "%ROOT%\tools\jpeg_gainmap_probe.cpp" "%ROOT%\3FP\Core\GainMapJpeg.cpp" ^
   /Fe:"%ROOT%\tools\build_out_jpeg\jpeg_gainmap_probe.exe" ^
   /Fo:"%ROOT%\tools\build_out_jpeg\\" ^
   /link

echo EXIT=%ERRORLEVEL%
endlocal
