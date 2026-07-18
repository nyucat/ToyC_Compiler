cmake_minimum_required(VERSION 3.20)

project(ToyCCompiler LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to Release build for maximum performance
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Global performance optimization flags
include(CheckIPOSupported)
check_ipo_supported(RESULT IPO_SUPPORTED)

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
    target_compile_options(compiler PRIVATE /W4 /O2 /GL)
    target_compile_options(test_sema PRIVATE /W4 /O2 /GL)
    target_compile_options(test_sema_integration PRIVATE /W4 /O2 /GL)
    target_compile_options(test_abc_pipeline PRIVATE /W4 /O2 /GL)
    if (IPO_SUPPORTED)
        set_target_properties(compiler PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
        set_target_properties(test_sema PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
        set_target_properties(test_sema_integration PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
        set_target_properties(test_abc_pipeline PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
else()
    set(TOYC_PERF_FLAGS -O3 -ffast-math)
    # Enable LTO if supported
    if (IPO_SUPPORTED)
        list(APPEND TOYC_PERF_FLAGS -flto=auto)
    endif()

    target_compile_options(compiler PRIVATE -Wall -Wextra -pedantic ${TOYC_PERF_FLAGS})
    target_compile_options(test_sema PRIVATE -Wall -Wextra -pedantic ${TOYC_PERF_FLAGS})
    target_compile_options(test_sema_integration PRIVATE -Wall -Wextra -pedantic ${TOYC_PERF_FLAGS})
    target_compile_options(test_abc_pipeline PRIVATE -Wall -Wextra -pedantic ${TOYC_PERF_FLAGS})

    if (IPO_SUPPORTED)
        set_target_properties(compiler PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
        set_target_properties(test_sema PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
        set_target_properties(test_sema_integration PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
        set_target_properties(test_abc_pipeline PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
endif()

include(cmake/ir_integration.cmake)
include(cmake/backend_integration.cmake)
