#!/bin/bash
PROG_NAME="TJDecode"
PROG_BUILD=${PROG_NAME}_target_build
LIBJPEG_INSTALL=${1}

echo $LIBJPEG_INSTALL

source /usr/local/oecore-x86_64/environment-setup-aarch64-poky-linux

mkdir $PROG_BUILD
cd $PROG_BUILD

cmake -DLIBJPEG_INSTALL=$LIBJPEG_INSTALL \
        ../
make VERBOSE=1
