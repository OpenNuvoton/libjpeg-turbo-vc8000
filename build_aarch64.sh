#!/bin/bash
LIBJPEG_TURBO_SRC="libjpeg-turbo-2.1.2"
LIBJPEG_TURBO_BUILD="libjpeg-turbo_target_build"
LIBJPEG_TURBO_INSTALL="libjpeg-turbo_target_install"

source /usr/local/oecore-x86_64/environment-setup-aarch64-poky-linux

#Check source

if [ ! -d "$LIBJPEG_TURBO_SRC" ]; then
        echo "Downlod libjpeg_turbo"
        wget https://github.com/libjpeg-turbo/libjpeg-turbo/archive/refs/tags/2.1.2.tar.gz
        tar -xzvf 2.1.2.tar.gz
        rm -rf 2.1.2.tar.gz
fi

#copy patch files to source
cp patch/* $LIBJPEG_TURBO_SRC

#create build folder
mkdir $LIBJPEG_TURBO_BUILD
cd $LIBJPEG_TURBO_BUILD

#build libjpeg
cmake -G"Unix Makefiles" \
	-DCMAKE_INSTALL_PREFIX=../$LIBJPEG_TURBO_INSTALL \
	-DWITH_VC8000=1 \
	-DCMAKE_BUILD_TYPE=Release \
	../$LIBJPEG_TURBO_SRC

make VERBOSE=1 -j8
make install
cd ../

#build test code
LIBJPEG_TURBO_PATH=`pwd`/$LIBJPEG_TURBO_INSTALL

echo "build JpegDecodeRaw"
cd test/JpegDecodeRaw
./build_aarch64.sh $LIBJPEG_TURBO_PATH
cd ../../

echo "build TJDecode"
cd test/TJDecode
./build_aarch64.sh $LIBJPEG_TURBO_PATH
cd ../../

echo "build FBDirectOut"
cd test/FBDirectOut
./build_aarch64.sh $LIBJPEG_TURBO_PATH
cd ../../

echo "build ThreadSafeTest"
cd test/ThreadSafeTest
./build_aarch64.sh $LIBJPEG_TURBO_PATH
cd ../../
