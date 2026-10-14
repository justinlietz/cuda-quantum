# ============================================================================ #
# Copyright (c) 2026 NVIDIA Corporation & Affiliates.                          #
# All rights reserved.                                                         #
#                                                                              #
# This source code and the accompanying materials are made available under     #
# the terms of the Apache License 2.0 which accompanies this distribution.     #
# ============================================================================ #

nanobind_add_module(_gf2_scaffold NB_STATIC bindings/scaffold.cpp)
target_link_libraries(_gf2_scaffold PRIVATE cudaq::gf2)
set(_python_package "${CMAKE_CURRENT_BINARY_DIR}/cudaq/gf2")
set_target_properties(_gf2_scaffold PROPERTIES
  OUTPUT_NAME _scaffold
  LIBRARY_OUTPUT_DIRECTORY "${_python_package}")
configure_file(cudaq/gf2/__init__.py ${_python_package}/__init__.py COPYONLY)
if(SKBUILD)
  set(_native_lib_path "../edems/lib")
else()
  set(_native_lib_path "../../${CMAKE_INSTALL_LIBDIR}")
endif()
if(APPLE)
  set_target_properties(_gf2_scaffold PROPERTIES
    INSTALL_RPATH "@loader_path/${_native_lib_path}")
elseif(UNIX)
  set_target_properties(_gf2_scaffold PROPERTIES
    INSTALL_RPATH "$ORIGIN/${_native_lib_path}")
endif()
install(TARGETS _gf2_scaffold
  LIBRARY DESTINATION ${CUDAQ_EDEMS_PYTHON_INSTALL_DIR}/../gf2 COMPONENT EdemsPython
  RUNTIME DESTINATION ${CUDAQ_EDEMS_PYTHON_INSTALL_DIR}/../gf2 COMPONENT EdemsPython)
install(FILES cudaq/gf2/__init__.py
  DESTINATION ${CUDAQ_EDEMS_PYTHON_INSTALL_DIR}/../gf2 COMPONENT EdemsPython)
