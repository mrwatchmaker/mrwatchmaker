#!/bin/bash
# MrWatchmaker 단일 Setup.exe 설치 프로그램 생성 (NSIS)
#
# 사용 (MSYS2 MINGW64 셸):
#   bash packaging/make_installer.sh
#
# 순서: 빌드(make) → exe 옆 런타임 DLL 자동 복사(all-local 훅) → NSIS 로 Setup.exe 생성
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$DIR/.." && pwd)"
export PATH="/mingw64/bin:/usr/bin:$PATH"

VERSION="$(cat "$ROOT/version" | tr -d ' \r\n')"
OUT="$ROOT/MrWatchmaker_Setup_${VERSION}.exe"

command -v makensis >/dev/null 2>&1 || {
  echo "makensis 가 없습니다. 설치: pacman -S --needed mingw-w64-x86_64-nsis"
  exit 1
}

echo "==> 빌드 (make)"
( cd "$ROOT" && make )

if [[ ! -f "$ROOT/mrwatchmaker.exe" ]]; then
  echo "빌드 실패: mrwatchmaker.exe 가 없습니다."
  exit 1
fi

# Windows 경로로 변환 (NSIS 는 백슬래시 경로 선호)
ROOT_WIN="$(cygpath -w "$ROOT")"
OUT_WIN="$(cygpath -w "$OUT")"

echo "==> 설치 프로그램 생성 (NSIS)"
makensis \
  -DAPPVERSION="$VERSION" \
  -DSRCDIR="$ROOT_WIN" \
  -DOUTFILE="$OUT_WIN" \
  "$ROOT_WIN\\packaging\\installer.nsi"

echo "==> 완료: $OUT"
