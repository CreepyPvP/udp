# Get CMake PreDefined Variables https://cmake.org/cmake/help/latest/manual/cmake-variables.7.html
# This CMake File for OpenGL Projects is compiled by Kushagra
# I do not claim that this is the best CMake Configuration you will get but it does work.

cmake_minimum_required(VERSION 3.20.0 FATAL_ERROR)

project(networking)

# Clang deez nuts
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Include debug information
set(CMAKE_BUILD_TYPE Debug)

# Add all Source Files
file(GLOB_RECURSE SOURCE_FILES 
	${CMAKE_SOURCE_DIR}/src/*.cpp
)

# Add all Header Files
file(GLOB_RECURSE HEADER_FILES 
	${CMAKE_SOURCE_DIR}/src/*.h
	${CMAKE_SOURCE_DIR}/src/*.hpp
)

add_executable(${PROJECT_NAME} ${HEADER_FILES} ${SOURCE_FILES})
