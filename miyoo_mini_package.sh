#!/bin/sh
if [ "${UNION_PLATFORM}" != "miyoomini" ]; then 
    echo "Must be run from cross-compile shell"
    exit 1
fi

cd $(dirname "$0")

VERSION=$1
if [ -z "$VERSION" ]; then 
    echo "Version arg is required, e.g. '1.0'"
    exit 1
fi
echo "Version v${VERSION}"

make clean
make -j

STAGE_ROOT=/tmp/staging
STAGE_APP=$STAGE_ROOT/App/PixelReader

echo "Staging to $STAGE_ROOT"
rm -rf $STAGE_ROOT

mkdir -p $STAGE_APP
mkdir -p $STAGE_APP/lib
mkdir -p $STAGE_APP/resources/fonts

cp -v resources/fonts/*.ttf $STAGE_APP/resources/fonts
cp -v resources/fonts/*.txt $STAGE_APP/resources/fonts
cp -v resources/icon/icon.png $STAGE_APP/icon.png

cat resources/config.json | sed "s/VERSION/${VERSION}/" | tee $STAGE_APP/config.json

cp -v resources/launch.sh   $STAGE_APP/
cp -v reader.cfg            $STAGE_APP/
cp -v README.md             $STAGE_APP

cp -v cross-compile/miyoo-mini/lib/* $STAGE_APP/lib
cp -v build/reader                   $STAGE_APP/

FILENAME="pixel_reader_v${VERSION}.zip"

(cd $STAGE_ROOT && zip -r $FILENAME App)

cp -v $STAGE_ROOT/$FILENAME build/