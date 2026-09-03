# This is a very simplified CMakeLists.txt for the userver-mailgun project
# It is intended to be included to a bigger project with userver already 
# configured.

cmake_minimum_required(VERSION 3.20...3.31)

set(CMAKE_CXX_COMPILER "/usr/bin/clang++")
set(CMAKE_C_COMPILER "/usr/bin/clang")

project(userver-mailgun VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
list(APPEND CMAKE_PREFIX_PATH "/userver/cmake")

include(/userver/cmake/ModuleHelpers.cmake)
include(/userver/cmake/UserverSetupEnvironment.cmake)

find_package(fmt REQUIRED)
userver_setup_environment()

set(CMAKE_CXX_STANDARD 20)
add_compile_options(-Wno-unknown-warning-option)

set(USERVER_MAILGUN_COMPONENTS universal core)

find_package(userver REQUIRED COMPONENTS ${USERVER_MAILGUN_COMPONENTS})

add_subdirectory(mailgun)
