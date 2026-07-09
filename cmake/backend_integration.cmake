set(TOYC_BACKEND_SOURCES
    src/backend/riscv_instruction.cpp
    src/backend/frame_layout.cpp
    src/backend/register_allocator.cpp
    src/backend/code_generator.cpp
)

if (TARGET compiler)
    target_sources(compiler PRIVATE ${TOYC_BACKEND_SOURCES})
endif()

if (TARGET test_abc_pipeline)
    target_sources(test_abc_pipeline PRIVATE ${TOYC_BACKEND_SOURCES})
endif()

if (TARGET test_ir)
    target_sources(test_ir PRIVATE ${TOYC_BACKEND_SOURCES})
endif()

if (TARGET test_optimizer)
    target_sources(test_optimizer PRIVATE ${TOYC_BACKEND_SOURCES})
endif()
