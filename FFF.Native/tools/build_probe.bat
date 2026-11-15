@echo off
REM Build gainmap_probe.exe standalone (Release x64).
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul

set ROOT=C:\PLAN\3FCompare\_wt_gainmap\FFF.Native
set INC=/I"%ROOT%" /I"%ROOT%\3FR" /I"%ROOT%\..\third_party\ffmpeg\include" /I"%ROOT%\..\third_party\libraw\include"
set LIBS=avcodec-63.lib avformat-63.lib avutil-61.lib swscale-10.lib swresample-7.lib
set LIBPATH=/LIBPATH:"%ROOT%\..\third_party\ffmpeg\lib\x64"

if not exist "%ROOT%\tools\build_out" mkdir "%ROOT%\tools\build_out"

cl /O2 /EHsc /MD /std:c++17 /D_CRT_SECURE_NO_WARNINGS /DNDEBUG %INC% ^
   "%ROOT%\tools\gainmap_probe.cpp" "%ROOT%\3FP\Core\GainMapProbe.cpp" "%ROOT%\3FP\Core\GainMapJpeg.cpp" ^
   /Fe:"%ROOT%\tools\build_out\gainmap_probe.exe" ^
   /Fo:"%ROOT%\tools\build_out\\" ^
   /link %LIBPATH% %LIBS%

echo EXIT=%ERRORLEVEL%
