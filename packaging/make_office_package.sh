#!/bin/bash
# (구) 회사용 폴더 대신 프로젝트 루트에 DLL 복사
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR/.."
test -f config.status || ./configure
make
bash packaging/copy_dlls_mingw.sh
