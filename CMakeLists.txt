cmake_minimum_required(VERSION 3.20)

project(ToyCCompiler LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(compiler
    src/main.cpp
    src/ast/ast.cpp
    src/frontend/lexer.cpp
    src/frontend/parser.cpp
)

target_include_directories(compiler PRIVATE include)

if (MSVC)
    target_compile_options(compiler PRIVATE /W4)
else()
    target_compile_options(compiler PRIVATE -Wall -Wextra -pedantic)
endif()
