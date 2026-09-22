cmake_minimum_required(VERSION 3.24)
project(dis_realtime_bench CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

# Recorded verbatim in data/env-*.txt by run_all.sh. Match across languages (handoff trap 6).
set(DIS_ARCH_FLAGS "-mcpu=native" CACHE STRING "CPU targeting flags")
# -ffp-contract=off: clang (on) and gcc (fast) fuse a*b+c into FMA by default; rustc and
# RyuJIT never do. Contraction changes the last bit of the DR outputs, so without this the
# three languages compute different answers and cross-language timing compares different work.
set(DIS_FP_FLAGS "-ffp-contract=off" CACHE STRING "Floating-point semantics flags")
separate_arguments(_arch UNIX_COMMAND "${DIS_ARCH_FLAGS}")
separate_arguments(_fp UNIX_COMMAND "${DIS_FP_FLAGS}")
add_compile_options(${_arch} ${_fp} -Wall -Wextra -Werror)

add_executable(bench bench.cpp)
target_include_directories(bench PRIVATE include)

add_executable(test_kernels test_kernels.cpp)
target_include_directories(test_kernels PRIVATE include)
