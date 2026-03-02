cmake_minimum_required(VERSION 4.1)
project(DataCompression)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(DataCompression main.cpp
        compressor.cpp
        compressor.h
        bitstream.cpp
        bitstream.h
        huffman.cpp
        huffman.h)
