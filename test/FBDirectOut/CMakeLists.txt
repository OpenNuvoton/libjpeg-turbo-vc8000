cmake_minimum_required(VERSION 2.6)

project(FBDirectOut)

include_directories("${LIBJPEG_INSTALL}/include")

link_directories("${LIBJPEG_INSTALL}/lib")

add_executable(FBDirectOut main.cc)

target_link_libraries(FBDirectOut ${LIBJPEG_INSTALL}/lib/libjpeg.a)

