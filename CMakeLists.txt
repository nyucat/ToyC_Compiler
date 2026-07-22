cmake_minimum_required(VERSION 3.20)

project(ToyCCompiler LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(TOYC_COMMON_SOURCES
    src/ast/ast.cpp
    src/frontend/lexer.cpp
    src/frontend/parser.cpp
    src/sema/scope.cpp
    src/sema/semantic_context.cpp
    src/sema/constant_evaluator.cpp
    src/sema/semantic_analyzer.cpp
)

add_executable(compiler
    src/main.cpp
    ${TOYC_COMMON_SOURCES}
)

add_executable(test_sema
    tests/sema/test_sema.cpp
    ${TOYC_COMMON_SOURCES}
)

add_executable(test_sema_integration
    tests/sema/test_sema_integration.cpp
    ${TOYC_COMMON_SOURCES}
)

add_executable(test_abc_pipeline
    tests/integration/test_abc_pipeline.cpp
    ${TOYC_COMMON_SOURCES}
)

target_include_directories(compiler PRIVATE include)
target_include_directories(test_sema PRIVATE include)
target_include_directories(test_sema_integration PRIVATE include)
target_include_directories(test_abc_pipeline PRIVATE include)

if (MSVC)
    target_compile_options(compiler PRIVATE /W4)
    target_compile_options(test_sema PRIVATE /W4)
    target_compile_options(test_sema_integration PRIVATE /W4)
    target_compile_options(test_abc_pipeline PRIVATE /W4)
else()
    target_compile_options(compiler PRIVATE -Wall -Wextra -pedantic)
    target_compile_options(test_sema PRIVATE -Wall -Wextra -pedantic)
    target_compile_options(test_sema_integration PRIVATE -Wall -Wextra -pedantic)
    target_compile_options(test_abc_pipeline PRIVATE -Wall -Wextra -pedantic)
endif()

include(cmake/ir_integration.cmake)
include(cmake/backend_integration.cmake)
