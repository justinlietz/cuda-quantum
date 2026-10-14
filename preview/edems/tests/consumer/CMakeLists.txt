# ============================================================================ #
# Copyright (c) 2026 NVIDIA Corporation & Affiliates.                          #
# All rights reserved.                                                         #
#                                                                              #
# This source code and the accompanying materials are made available under     #
# the terms of the Apache License 2.0 which accompanies this distribution.     #
# ============================================================================ #

cmake_minimum_required(VERSION 3.30)
project(edems-scaffold-consumer LANGUAGES CXX)

find_package(cudaq-gf2 REQUIRED CONFIG)
find_package(cudaq-edems REQUIRED CONFIG)
add_executable(scaffold-consumer main.cpp)
target_compile_features(scaffold-consumer PRIVATE cxx_std_20)
target_link_libraries(scaffold-consumer PRIVATE cudaq::gf2 cudaq::edems)
