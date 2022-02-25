cmake_minimum_required(VERSION 2.6)

project(JpegDecodeRaw)

include_directories("${LIBJPEG_INSTALL}/include")

link_directories("${LIBJPEG_INSTALL}/lib")

add_executable(JpegDecodeRaw main.cc)

target_link_libraries(JpegDecodeRaw ${LIBJPEG_INSTALL}/lib/libjpeg.a)

