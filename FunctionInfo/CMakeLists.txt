cmake_minimum_required(VERSION 3.13.4)
project(FunctionInfo)

find_package(LLVM 15 REQUIRED CONFIG)

include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})

set(CMAKE_CXX_STANDARD 17 CACHE STRING "")

if(NOT LLVM_ENABLE_RTTI)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")
endif()

add_library(${PROJECT_NAME} SHARED FunctionInfo.cpp)

target_link_libraries(${PROJECT_NAME}
  "$<$<PLATFORM_ID:Darwin>:-undefined dynamic_lookup>")
