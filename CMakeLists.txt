cmake_minimum_required(VERSION 3.10)
project(websift_cpp)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ZLIB REQUIRED)

add_executable(websift 
    src/main.cpp
    src/warc.cpp
    src/filters.cpp
)

target_link_libraries(websift ZLIB::ZLIB)

