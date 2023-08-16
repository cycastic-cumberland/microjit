cmake_minimum_required(VERSION 3.26)
project(microjit)

set(CMAKE_CXX_STANDARD 17)

add_definitions(-DDEBUG_ENABLED)
add_definitions(-DASMJIT_STATIC)

#set(GTEST_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/googletest)
set(ASMJIT_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/asmjit)

#add_subdirectory(${GTEST_SOURCE_DIR})
add_subdirectory(${ASMJIT_SOURCE_DIR})

#include_directories(${GTEST_SOURCE_DIR}/googletest/include)
include_directories(${ASMJIT_SOURCE_DIR}/src)

add_library(microjit
            src/microjit/microjit.h
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
)


target_link_libraries(microjit asmjit)
