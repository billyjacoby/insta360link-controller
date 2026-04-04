cmake_minimum_required(VERSION 3.14)
project(linkctl C)

set(CMAKE_C_STANDARD 11)

add_executable(linkctl linkctl.c)

install(TARGETS linkctl DESTINATION bin)
