# ============================================================================ #
# Copyright (c) 2026 NVIDIA Corporation & Affiliates.                          #
# All rights reserved.                                                         #
#                                                                              #
# This source code and the accompanying materials are made available under     #
# the terms of the Apache License 2.0 which accompanies this distribution.     #
# ============================================================================ #

cmake_minimum_required(VERSION 3.30)

# Keep generated files out of the source tree, as in the Logical preview.
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_CURRENT_BINARY_DIR)
  message(FATAL_ERROR "Use an out-of-source build: cmake -S . -B build")
endif()
project(cudaq-edems-scaffold VERSION 0.0.0 LANGUAGES CXX)

option(CUDAQ_EDEMS_BUILD_PYTHON "Build the cudaq-edems Python bindings" OFF)

# This umbrella builds only GF2 and EDEM. Logical builds against installed CUDA-Q.
add_subdirectory(gf2)
add_subdirectory(edems)
if(CUDAQ_EDEMS_BUILD_PYTHON)
  # For wheels, the prefix is the EDEM package directory. Native installs keep
  # libraries at the installation prefix and Python packages beneath cudaq/.
  if(SKBUILD)
    set(CUDAQ_EDEMS_PYTHON_INSTALL_DIR ".")
    set(CUDAQ_EDEMS_INSTALL_RELATIVE_PREFIX ".")
  else()
    set(CUDAQ_EDEMS_PYTHON_INSTALL_DIR "cudaq/edems")
    set(CUDAQ_EDEMS_INSTALL_RELATIVE_PREFIX "../..")
  endif()
  find_package(Python 3.11 REQUIRED COMPONENTS Interpreter Development.Module)
  find_package(nanobind 2.4 REQUIRED CONFIG)
  add_subdirectory(gf2/python)
  add_subdirectory(edems/python)
endif()
