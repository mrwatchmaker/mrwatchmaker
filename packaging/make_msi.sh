#!/bin/bash

if test ! -d "$1"
then
	echo Usage: $0 resources-dir
	exit 1
fi

DIR=`dirname "${BASH_SOURCE[0]}"`
ABSDIR=`cd "$DIR"; pwd`

RESOURCES=`cd "$1"; pwd`

TARGET="$ABSDIR/../msi"

cd "$ABSDIR"/..

VERSION=`cat version`

rm -rf "$TARGET"
mkdir -p "$TARGET"

cd "$TARGET"
../configure
make

# mrwatchmaker.exe 만 사용 (tg.exe 제거)
cp "$ABSDIR/mrwatchmaker.wxs" "$TARGET"
cp "$ABSDIR/LICENSE.rtf" "$TARGET"
cp "$ABSDIR/../README.md" "$TARGET"
cp "$ABSDIR/../LICENSE" "$TARGET"
cp "$ABSDIR/../icons/tg-document.ico" "$TARGET"
cp -r "$RESOURCES"/* "$TARGET"
heat dir "$RESOURCES" -srd -gg -sreg -dr INSTALLDIR -cg Resources -out "$TARGET/Resources.wxs"

candle mrwatchmaker.wxs Resources.wxs
light -out MrWatchmaker_${VERSION}.msi -ext WixUIExtension mrwatchmaker.wixobj Resources.wixobj
