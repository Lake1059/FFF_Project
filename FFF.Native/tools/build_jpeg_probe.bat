@echo off
REM Build jpeg_gainmap_probe.exe standalone (Release x64). No external deps.
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul

set ROOT=C:\PLAN\3FCompare\_wt_gainmap\FFF.Native
set INC=/I"%ROOT%"

if not exist "%ROOT%\tools\build_out_jpeg" mkdir "%ROOT%\tools\build_out_jpeg"

cl /O2 /EHsc /MD /std:c++17 /D_CRT_SECURE_NO_WARNINGS /DNDEBUG %INC% ^
   "%ROOT%\tools\jpeg_gainmap_probe.cpp" "%ROOT%\3FP\Core\GainMapJpeg.cpp" ^
   /Fe:"%ROOT%\tools\build_out_jpeg\jpeg_gainmap_probe.exe" ^
   /Fo:"%ROOT%\tools\build_out_jpeg\\" ^
   /link

echo EXIT=%ERRORLEVEL%
