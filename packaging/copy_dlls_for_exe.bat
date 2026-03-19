@echo off
REM mrwatchmaker.exe 실행에 필요한 DLL을 회사가져갈_MrWatchmaker 폴더로 복사
REM MSYS2 MinGW로 빌드한 경우 C:\msys64\mingw64\bin 에서 복사

setlocal
set DEST=%~dp0..\회사가져갈_MrWatchmaker
if "%~1" neq "" set DEST=%~1
if not exist "%DEST%" mkdir "%DEST%"

set MINGW_BIN=
if exist "C:\msys64\mingw64\bin\libcairo-2.dll" set MINGW_BIN=C:\msys64\mingw64\bin
if exist "C:\msys64\ucrt64\bin\libcairo-2.dll" set MINGW_BIN=C:\msys64\ucrt64\bin
if "%MINGW_BIN%"=="" (
  echo [안내] MinGW bin을 찾지 못했습니다. C:\msys64\mingw64\bin 또는 ucrt64\bin 을 확인하세요.
  echo MSYS2 MINGW64 셸에서 아래를 실행하면 필요한 DLL만 자동 복사됩니다:
  echo   cd tg-timer-0.5.0
  echo   bash packaging/copy_dlls_mingw.sh
  exit /b 1
)

echo DLL 복사 중: %MINGW_BIN% --^> %DEST%
set LIST=libcairo-2.dll libcairo-gobject-2.dll libgdk-3-0.dll libgtk-3-0.dll libglib-2.0-0.dll libgobject-2.0-0.dll libgio-2.0-0.dll libpango-1.0-0.dll libpangocairo-1.0-0.dll libpangowin32-1.0-0.dll libpangoft2-1.0-0.dll libgdk_pixbuf-2.0-0.dll libatk-1.0-0.dll libepoxy-0.dll libfontconfig-1.dll libfreetype-6.dll libharfbuzz-0.dll libgraphite2.dll libpixman-1-0.dll libpng16-16.dll zlib1.dll libportaudio.dll libfftw3f-3.dll libgcc_s_seh-1.dll libwinpthread-1.dll libstdc++-6.dll libexpat-1.dll libfribidi-0.dll libpcre2-8-0.dll libffi-8.dll libintl-8.dll libthai-0.dll libdatrie-1.dll libjpeg-8.dll libtiff-6.dll libwebp-7.dll libsharpyuv-0.dll libjbig-0.dll libLerc.dll libdeflate.dll libzstd.dll liblzma-5.dll libbrotlidec.dll libbrotlicommon.dll libbz2-1.dll libiconv-2.dll
for %%f in (%LIST%) do (
  if exist "%MINGW_BIN%\%%f" copy /y "%MINGW_BIN%\%%f" "%DEST%\" >nul 2>&1
)
echo 완료.
endlocal
