cmake_minimum_required(VERSION 3.15)
project(DataCompression)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set optimization flags for Release builds
# For MSVC: /O2 is equivalent to aggressive optimization
# For GCC/Clang: -O3 for maximum optimization
if(MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "/O2 /arch:AVX2")
else()
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -mavx2 -mbmi -mbmi2 -msse4.2")
endif()

include_directories(${CMAKE_SOURCE_DIR})

# Common source sets
set(CORE_SOURCES
        bitstream.h
        huffman.cpp huffman.h
        lz.cpp lz.h
        lz_huffman.cpp lz_huffman.h
        compressor.cpp compressor.h)

set(HUFFMAN_ONLY_SOURCES
        bitstream.h
        huffman.cpp huffman.h)

add_executable(DataCompression main.cpp ${CORE_SOURCES})

# Tests using the full compressor API
add_executable(TestBlockCompressor tests/test_block_compressor.cpp ${CORE_SOURCES})
add_executable(TestBenchmark tests/test_benchmark.cpp ${CORE_SOURCES})
target_compile_definitions(TestBenchmark PRIVATE
        PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

# Tests using LZ+Huffman chunk-level API (token-based multi-stream)
add_executable(TestLZHuffmanMultiStream tests/test_lz_huffman_multistream.cpp ${CORE_SOURCES})

# Tests using LZ only
add_executable(TestLZ tests/test_lz.cpp lz.cpp lz.h)

# Tests using LZ+Huffman as separate layers (not the integrated pipeline)
add_executable(TestLZHuffmanLayers tests/test_lz_huffman_layers.cpp
        lz.cpp lz.h
        huffman.cpp huffman.h
        bitstream.h)

# Diagnostic overhead tool
add_executable(DiagOverhead diag_overhead.cpp ${CORE_SOURCES})

# Tests using Huffman only
add_executable(TestHuffmanEdgeCases tests/test_huffman_edge_cases.cpp ${HUFFMAN_ONLY_SOURCES})
add_executable(TestHuffmanMetrics tests/test_huffman_metrics.cpp ${HUFFMAN_ONLY_SOURCES})
add_executable(TestHuffmanParallel tests/test_huffman_parallel.cpp ${HUFFMAN_ONLY_SOURCES})

