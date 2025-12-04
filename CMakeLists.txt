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

add_executable(gopher_filter_cli
    src/gopher_cli.cpp
    src/filters.cpp
)

add_executable(gopher_filter_batch
    src/gopher_filter_batch.cpp
    src/filters.cpp
)

add_executable(extract_texts
    src/extract_texts.cpp
    src/warc.cpp
)
target_link_libraries(extract_texts ZLIB::ZLIB)

target_link_libraries(gopher_filter_batch ZLIB::ZLIB)

