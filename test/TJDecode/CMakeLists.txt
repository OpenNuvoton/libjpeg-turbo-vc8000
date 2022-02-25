cmake_minimum_required(VERSION 2.6)

project(TJDecode)

include_directories("${LIBJPEG_INSTALL}/include")

link_directories("${LIBJPEG_INSTALL}/lib")

add_executable(TJDecode main.cc)

target_link_libraries(TJDecode ${LIBJPEG_INSTALL}/lib/libturbojpeg.a)

