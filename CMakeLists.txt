cmake_minimum_required(VERSION 3.15)
project(DataCompression)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set optimization flags for Release builds
# For MSVC: /O2 is equivalent to aggressive optimization
# For GCC/Clang: -O3 for maximum optimization
if(MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "/O2")
else()
    set(CMAKE_CXX_FLAGS_RELEASE "-O3")
endif()

include_directories(${CMAKE_SOURCE_DIR})

# Common source sets
set(CORE_SOURCES
        bitstream.cpp bitstream.h
        huffman.cpp huffman.h
        lz77.cpp lz77.h
        lz77_huffman.cpp lz77_huffman.h
        compressor.cpp compressor.h)

set(HUFFMAN_ONLY_SOURCES
        bitstream.cpp bitstream.h
        huffman.cpp huffman.h)

add_executable(DataCompression main.cpp ${CORE_SOURCES})

# Tests using the full compressor API
add_executable(TestBlockCompressor tests/test_block_compressor.cpp ${CORE_SOURCES})
add_executable(TestBenchmark tests/test_benchmark.cpp ${CORE_SOURCES})

# Tests using LZ77+Huffman chunk-level API (token-based multi-stream)
add_executable(TestLZ77HuffmanMultiStream tests/test_lz77_huffman_multistream.cpp
        lz77.cpp lz77.h
        lz77_huffman.cpp lz77_huffman.h
        huffman.cpp huffman.h
        bitstream.cpp bitstream.h)

# Tests using LZ77 only
add_executable(TestLZ77 tests/test_lz77.cpp lz77.cpp lz77.h)

# Tests using LZ77+Huffman as separate layers (not the integrated pipeline)
add_executable(TestLZ77HuffmanLayers tests/test_lz77_huffman_layers.cpp
        lz77.cpp lz77.h
        huffman.cpp huffman.h
        bitstream.cpp bitstream.h)

# Tests using Huffman only
add_executable(TestHuffmanEdgeCases tests/test_huffman_edge_cases.cpp ${HUFFMAN_ONLY_SOURCES})
add_executable(TestHuffmanMetrics tests/test_huffman_metrics.cpp ${HUFFMAN_ONLY_SOURCES})
add_executable(TestHuffmanParallel tests/test_huffman_parallel.cpp ${HUFFMAN_ONLY_SOURCES})
