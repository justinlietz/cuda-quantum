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
project(cudaq-gf2 VERSION 0.0.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(cudaq-gf2 SHARED lib/scaffold.cpp)
add_library(cudaq::gf2 ALIAS cudaq-gf2)
set_target_properties(cudaq-gf2 PROPERTIES EXPORT_NAME gf2)
# C ABI consumers do not need a C++ compiler or C++ compilation flags.
target_compile_features(cudaq-gf2 PRIVATE cxx_std_20)
target_include_directories(cudaq-gf2 PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

install(TARGETS cudaq-gf2 EXPORT cudaq-gf2-targets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT EdemsNative
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT EdemsNative
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT EdemsNative)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  COMPONENT EdemsNative)
install(EXPORT cudaq-gf2-targets NAMESPACE cudaq::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cudaq-gf2
  COMPONENT EdemsNative)
configure_package_config_file(cmake/cudaq-gf2-config.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/cudaq-gf2-config.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cudaq-gf2)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/cudaq-gf2-config-version.cmake
  VERSION ${PROJECT_VERSION} COMPATIBILITY ExactVersion)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/cudaq-gf2-config.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/cudaq-gf2-config-version.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cudaq-gf2
  COMPONENT EdemsNative)
