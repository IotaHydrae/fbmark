cmake_minimum_required(VERSION 3.10)
project(fbmark VERSION 1.0.1 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Match Makefile: BIN_SUFFIX = .out
set(CMAKE_EXECUTABLE_SUFFIX ".out")

add_executable(fbmark fbmark.c)

target_compile_options(fbmark PRIVATE -Wall -g -O2)

target_link_libraries(fbmark PRIVATE m)

install(
    TARGETS fbmark
    RUNTIME DESTINATION bin
)
