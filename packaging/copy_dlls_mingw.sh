#!/bin/bash
# MSYS2 MinGW 셸에서 실행: mrwatchmaker.exe 가 의존하는 DLL을 회사가져갈_MrWatchmaker 로 복사

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$DIR/.." && pwd)"
DEST="${1:-$ROOT/../회사가져갈_MrWatchmaker}"
EXE="$ROOT/mrwatchmaker.exe"

mkdir -p "$DEST"

if [[ ! -f "$EXE" ]]; then
  echo "먼저 make 로 mrwatchmaker.exe 를 빌드하세요."
  exit 1
fi

echo "의존 DLL 복사: $EXE => $DEST"
ldd "$EXE" | grep '=> /' | awk '{print $3}' | while read -r dll; do
  if [[ -f "$dll" ]]; then
    cp -f "$dll" "$DEST/"
  fi
done
echo "완료: $DEST"
ls -la "$DEST"/*.dll 2>/dev/null | wc -l
echo "개 DLL 복사됨"
