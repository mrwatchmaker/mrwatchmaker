#!/bin/bash
# mrwatchmaker.exe + 회사에서 쓸 파일들을 한 폴더에 모음 (회사 가져갈 패키지)

DIR=`dirname "${BASH_SOURCE[0]}"`
ABSDIR=`cd "$DIR"; pwd`
ROOT="$ABSDIR/.."
DEST="$ROOT/회사가져갈_MrWatchmaker"

cd "$ROOT"
test -f config.status || { ./configure; make; }
make

rm -rf "$DEST"
mkdir -p "$DEST"

cp mrwatchmaker.exe "$DEST/" 2>/dev/null || cp mrwatchmaker "$DEST/mrwatchmaker.exe" 2>/dev/null || true
cp README.md "$DEST/"
cp LICENSE "$DEST/"
test -f port.conf && cp port.conf "$DEST/" || echo "5" > "$DEST/port.conf"
echo "COM 포트 번호만 적힌 파일. 기본 5 → COM5. 회사 PC COM이 다르면 숫자만 수정 (예: 3 → COM3)" > "$DEST/port.conf.txt"

cat > "$DEST/README_회사용.txt" << 'EOF'
MrWatchmaker 회사용 폴더
========================
1. mrwatchmaker.exe 더블클릭으로 실행
2. 서보(Feetech) COM 포트: exe와 같은 폴더에 port.conf 파일 만들고, 숫자만 적기 (예: 5 → COM5)
3. 12V 전원 + USB 연결 후 사용
EOF

echo "완료: $DEST"
ls -la "$DEST"
