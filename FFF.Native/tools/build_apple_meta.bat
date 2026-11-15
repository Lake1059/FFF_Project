@echo off
REM Build apple_meta_probe.exe standalone (Release x64).
REM Pure math header + minimal HEIC parser; NO ffmpeg linkage required.
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul

set ROOT=C:\PLAN\3FCompare\_wt_gainmap\FFF.Native

if not exist "%ROOT%\tools\build_out" mkdir "%ROOT%\tools\build_out"

cl /O2 /EHsc /MD /std:c++17 /D_CRT_SECURE_NO_WARNINGS /DNDEBUG ^
   "%ROOT%\tools\apple_meta_probe.cpp" ^
   /Fe:"%ROOT%\tools\build_out\apple_meta_probe.exe" ^
   /Fo:"%ROOT%\tools\build_out\\"

echo EXIT=%ERRORLEVEL%
