#!/bin/bash

# set cmake find_package path
CMAKE_PREFIX_PATH=`llvm-config --cmakedir`

# compiler
cmake -B build && cd build && make -j8

# test file
clang -O0 -Xclang -disable-O0-optnone -emit-llvm -c ../test_input.c
opt -mem2reg test_input.bc -o test_input-m2r.bc
opt -load-pass-plugin ./libLocalOpt.so -passes=local-opt -disable-output test_input-m2r.bc

