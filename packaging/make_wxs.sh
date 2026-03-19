#!/bin/bash
# mrwatchmaker.exe 전용 패키징용 wxs 생성 (tg.exe 제거)

DIR=`dirname "${BASH_SOURCE[0]}"`
ABSDIR=`cd "$DIR"; pwd`

cd "$DIR"

VERSION=`cat ../version`
VERSIONX=`cat ../version | sed "s/\\([0-9]*\\.[0-9]*\\.[0-9]*\\).*/\\1/"`

cat mrwatchmaker.wxs.template | sed \
's/#UUID#/<?php system("echo -n `uuidgen`");?>/g;'\
"s/#VERSION#/$VERSION/g;"\
"s/#VERSIONX#/$VERSIONX/g;"\
| php > mrwatchmaker.wxs
