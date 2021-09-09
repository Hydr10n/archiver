cmake_minimum_required(VERSION 3.8)

project(archiver VERSION 1.0)

set(CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_CXX_STANDARD 17)

find_package(xxHash CONFIG REQUIRED)

add_executable(archiver archiver.cpp main.cpp)
target_link_libraries(archiver xxHash::xxhash)
