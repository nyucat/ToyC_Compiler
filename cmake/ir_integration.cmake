# 成员 C：IR 生成、中端优化与独立测试目标
# 成员 A 联调时在根 CMakeLists.txt 末尾 include 本文件。

set(TOYC_IR_SOURCES
    src/ir/ir.cpp
    src/ir/basic_block.cpp
    src/ir/ir_builder.cpp
    src/optimizer/constant_fold.cpp
    src/optimizer/dead_code.cpp
    src/optimizer/copy_prop.cpp
    src/optimizer/cse.cpp
    src/optimizer/cfg_simplify.cpp
    src/optimizer/optimizer.cpp
)

if (TARGET compiler)
    target_sources(compiler PRIVATE ${TOYC_IR_SOURCES})
endif()

if (TARGET test_abc_pipeline)
    target_sources(test_abc_pipeline PRIVATE ${TOYC_IR_SOURCES})
endif()

if (MSVC)
    add_compile_options(/FS)
endif()

add_executable(test_ir
    tests/ir/test_ir.cpp
    tests/ir/support/symbol_table_builder.cpp
    src/ast/ast.cpp
    src/frontend/lexer.cpp
    src/frontend/parser.cpp
    ${TOYC_IR_SOURCES}
)
target_include_directories(test_ir PRIVATE include ${CMAKE_SOURCE_DIR})

add_executable(test_optimizer
    tests/optimization/test_optimizer.cpp
    ${TOYC_IR_SOURCES}
)
target_include_directories(test_optimizer PRIVATE include)

if (MSVC)
    target_compile_options(test_ir PRIVATE /W4)
    target_compile_options(test_optimizer PRIVATE /W4)
    if (TARGET test_abc_pipeline)
        target_compile_options(test_abc_pipeline PRIVATE /W4)
    endif()
endif()

enable_testing()
add_test(NAME ir COMMAND test_ir)
add_test(NAME optimizer COMMAND test_optimizer)
if (TARGET test_abc_pipeline)
    add_test(NAME abc_pipeline COMMAND test_abc_pipeline)
endif()
