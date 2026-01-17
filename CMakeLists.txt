cmake_minimum_required(VERSION 3.20)
project(bit_fields VERSION 1.0.0 LANGUAGES CXX)

# Header-only library
add_library(bit_fields INTERFACE)
add_library(bit_fields::bit_fields ALIAS bit_fields)

target_include_directories(bit_fields INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(bit_fields INTERFACE cxx_std_20)

# Examples (optional)
option(BIT_FIELDS_BUILD_EXAMPLES "Build examples" OFF)

if(BIT_FIELDS_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Tests (optional)
option(BIT_FIELDS_BUILD_TESTS "Build tests" OFF)

if(BIT_FIELDS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Install
include(GNUInstallDirs)

install(TARGETS bit_fields
    EXPORT bit_fieldsTargets
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT bit_fieldsTargets
    FILE bit_fieldsTargets.cmake
    NAMESPACE bit_fields::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/bit_fields
)

# Package config
include(CMakePackageConfigHelpers)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/bit_fieldsConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/bit_fieldsConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/bit_fields
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/bit_fieldsConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/bit_fieldsConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/bit_fieldsConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/bit_fields
)
