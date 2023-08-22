cmake_minimum_required(VERSION 3.26)
project(microjit)

set(CMAKE_CXX_STANDARD 17)

add_definitions(-DDEBUG_ENABLED)
add_definitions(-DASMJIT_STATIC)

#set(GTEST_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/googletest)
set(ASMJIT_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/asmjit)

#find_package(PythonInterp 3.11 REQUIRED)

#add_subdirectory(${GTEST_SOURCE_DIR})
#add_subdirectory(${ASMJIT_SOURCE_DIR})

#include_directories(${GTEST_SOURCE_DIR}/googletest/include)
include_directories(${ASMJIT_SOURCE_DIR}/src)

add_library(microjit
            src/microjit/helper.h
            src/microjit/safe_refcount.h
            src/microjit/def.h
            src/microjit/lock.h
            src/microjit/virtual_stack.h
            src/microjit/instructions.h
            src/microjit/orchestrator.h
            src/microjit/jit.h
            src/microjit/jit_x86_64.h
            src/microjit/jit_x86_64.cpp
            src/microjit/utils.h
        src/microjit/instructions.cpp
        src/microjit/jit.cpp
        src/microjit/primitive_conversion_map.gen.h
        src/microjit/x86_64_primitive_converter.gen.h
        src/microjit/thread_pool.h
        src/microjit/priority_queue.h
        src/microjit/managed_thread.h
        src/microjit/managed_thread.cpp
        src/microjit/compilation_agent.h
        src/microjit/command_queue.h
        src/microjit/compilation_agent.cpp
)

set(PYTHON_EXECUTABLE python3)

add_custom_target(
        generate_primitive_conversion_map ALL
        COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/src/microjit/generate_primitive_conversion_map.py
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src/microjit/
        COMMENT "MicroJIT: Generating primitive conversion map"
)

add_custom_target(
        generate_primitive_converter ALL
        COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/src/microjit/generate_x86_64_primitive_converter.py
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src/microjit/
        COMMENT "MicroJIT: Generating primitive converter"
)

add_dependencies(microjit generate_primitive_conversion_map generate_primitive_converter)

#target_link_libraries(microjit asmjit)
