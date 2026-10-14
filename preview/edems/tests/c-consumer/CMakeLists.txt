# ============================================================================ #
# Copyright (c) 2026 NVIDIA Corporation & Affiliates.                          #
# All rights reserved.                                                         #
#                                                                              #
# This source code and the accompanying materials are made available under     #
# the terms of the Apache License 2.0 which accompanies this distribution.     #
# ============================================================================ #

cmake_minimum_required(VERSION 3.30)
project(edems-cabi-consumer LANGUAGES C)

find_package(cudaq-edems REQUIRED CONFIG)
add_executable(cabi-consumer main.c)
target_compile_features(cabi-consumer PRIVATE c_std_99)
target_link_libraries(cabi-consumer PRIVATE cudaq::edems)
