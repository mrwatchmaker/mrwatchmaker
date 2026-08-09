@echo off
REM mrwatchmaker.exe 와 같은 폴더에 DLL 복사 (MSYS2: bash packaging/copy_dlls_mingw.sh 권장)
cd /d "%~dp0.."
if exist "C:\msys64\msys2_shell.cmd" (
  C:\msys64\msys2_shell.cmd -mingw64 -defterm -no-start -c "cd /c/mrwatchmaker && bash packaging/copy_dlls_mingw.sh"
) else (
  echo MSYS2 가 없습니다. C:\msys64\msys2_shell.cmd 설치 후 다시 실행하세요.
  exit /b 1
)
