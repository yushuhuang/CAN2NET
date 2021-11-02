cmake_minimum_required(VERSION 3.0)

project(CAN2NET)

set(CMAKE_CXX_STANDARD 11)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS "-Wall -Wextra")
set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

find_package(Threads REQUIRED)
find_library(LIBRT rt)

file(GLOB SOURCES "src/*.cpp")

add_executable(can2net ${SOURCES})
target_include_directories(can2net PRIVATE include)
target_link_libraries(can2net pthread rt)