cmake_minimum_required(VERSION 3.2.0)

project(QtFakeTime C CXX)

set(CMAKE_CXX_STANDARD 11)

if (NOT ((CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux") AND (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")))
    # Library is dependant on LD_PRELOAD linker support in order to shim libQt5Core function implementations (a linux specific
    # feature) and currently only implements symbol overrides for GCC specific C++ name-mangled identifiers.
    #
    # CLang would be the obvious alternative compiler to add support for, and it may well "just work" as they both supposedly use
    # the IA64 C++ ABI specification.
    message(FATAL_ERROR "QtFakeTime only supported on linux host using GCC compiler")
endif()

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # GCC specific compiler options
    add_compile_options(-Wfatal-errors)
    add_compile_options(-fmax-errors=5)
    add_compile_options(-Werror=return-type)

    if(CMAKE_BUILD_TYPE STREQUAL Debug)
        add_definitions(-D_GLIBCXX_DEBUG)
        add_compile_options(-g)
        add_compile_options(-O0)
    endif()
endif()

# As Qt moc files are generated in the binary dir, tell CMake to always look for includes there:
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Instruct CMake to run Qt moc, uic & rcc automatically when needed.
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt5 COMPONENTS Core REQUIRED)

add_library(QtFakeTime SHARED ${CMAKE_CURRENT_SOURCE_DIR}/QtFakeTime.cpp)

target_link_libraries(QtFakeTime Qt5::Core)

target_include_directories(QtFakeTime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(test)
