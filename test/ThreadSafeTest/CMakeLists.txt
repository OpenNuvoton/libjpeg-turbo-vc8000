cmake_minimum_required(VERSION 2.6)

project(ThreadSafeTest)

include_directories("${LIBJPEG_INSTALL}/include")

link_directories("${LIBJPEG_INSTALL}/lib")

add_executable(ThreadSafeTest main.cc)

target_link_libraries(ThreadSafeTest turbojpeg pthread)
