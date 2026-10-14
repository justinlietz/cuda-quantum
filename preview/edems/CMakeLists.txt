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
project(cudaq-edems VERSION 0.0.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# An independent EDEM build consumes the installed GF2 package. The umbrella
# supplies the same target directly, with no dependency downloads.
if(NOT TARGET cudaq::gf2)
  find_package(cudaq-gf2 REQUIRED CONFIG)
endif()

add_library(cudaq-edems SHARED lib/scaffold.cpp)
add_library(cudaq::edems ALIAS cudaq-edems)
set_target_properties(cudaq-edems PROPERTIES EXPORT_NAME edems)
target_compile_features(cudaq-edems PRIVATE cxx_std_20)
target_include_directories(cudaq-edems PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(cudaq-edems PUBLIC cudaq::gf2)
if(APPLE)
  set_target_properties(cudaq-edems PROPERTIES INSTALL_RPATH "@loader_path")
elseif(UNIX)
  set_target_properties(cudaq-edems PROPERTIES INSTALL_RPATH "$ORIGIN")
endif()

install(TARGETS cudaq-edems EXPORT cudaq-edems-targets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT EdemsNative
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT EdemsNative
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT EdemsNative)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  COMPONENT EdemsNative)
install(EXPORT cudaq-edems-targets NAMESPACE cudaq::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cudaq-edems
  COMPONENT EdemsNative)

configure_package_config_file(cmake/cudaq-edems-config.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/cudaq-edems-config.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cudaq-edems)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/cudaq-edems-config-version.cmake
  VERSION ${PROJECT_VERSION} COMPATIBILITY ExactVersion)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/cudaq-edems-config.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/cudaq-edems-config-version.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cudaq-edems
  COMPONENT EdemsNative)
