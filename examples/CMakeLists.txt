cmake_minimum_required(VERSION 3.16)
project(bit_fields_examples CXX)

# Set C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add include directory for bit_fields headers
include_directories(${CMAKE_SOURCE_DIR}/include)

# List all example files

set(EXAMPLE_SOURCES
    basic_usage.cpp
    constexpr_usage.cpp
)

# Add more examples here as needed
# list(APPEND EXAMPLE_SOURCES another_example.cpp)

foreach(example_file ${EXAMPLE_SOURCES})
    get_filename_component(example_name ${example_file} NAME_WE)
    add_executable(${example_name} ${example_file})
    target_include_directories(${example_name} PRIVATE ${CMAKE_SOURCE_DIR}/include)
endforeach()
