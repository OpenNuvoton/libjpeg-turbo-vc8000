#!/bin/bash

MACH=`uname -m`
PACKAGE_NAME=libjpeg-turbo_target.tar.gz
PACKAGE_FOLDER=libjpeg-turbo_target
TARGET_FOLDER=/usr

if [ "$MACH" == "aarch64" ]; then
	echo "OK, start install"
else
	echo "Target not match. Exit!"
	exit 1
fi

if [ ! -f "$PACKAGE_NAME" ]; then
	echo "Package not exist. Exit!"
	exit 2
fi

tar -xzvf $PACKAGE_NAME

cd $PACKAGE_FOLDER
cp -rf lib/* $TARGET_FOLDER/lib
cp -rf share/* $TARGET_FOLDER/share
cd ../
rm -rf $PACKAGE_FOLDER

echo "Install done!"
exit 0
