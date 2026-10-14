# ============================================================================ #
# Copyright (c) 2026 NVIDIA Corporation & Affiliates.                          #
# All rights reserved.                                                         #
#                                                                              #
# This source code and the accompanying materials are made available under     #
# the terms of the Apache License 2.0 which accompanies this distribution.     #
# ============================================================================ #

nanobind_add_module(_edems_scaffold NB_STATIC bindings/scaffold.cpp)
target_link_libraries(_edems_scaffold PRIVATE cudaq::edems)
set(_python_package "${CMAKE_CURRENT_BINARY_DIR}/cudaq/edems")
set_target_properties(_edems_scaffold PROPERTIES
  OUTPUT_NAME _scaffold
  LIBRARY_OUTPUT_DIRECTORY "${_python_package}")
configure_file(cudaq/edems/__init__.py.in ${_python_package}/__init__.py @ONLY)

# Both installation layouts share one copy of the native libraries.
if(SKBUILD)
  set(_native_lib_path "lib")
else()
  set(_native_lib_path "../../${CMAKE_INSTALL_LIBDIR}")
endif()
if(APPLE)
  set_target_properties(_edems_scaffold PROPERTIES
    INSTALL_RPATH "@loader_path/${_native_lib_path}")
elseif(UNIX)
  set_target_properties(_edems_scaffold PROPERTIES
    INSTALL_RPATH "$ORIGIN/${_native_lib_path}")
endif()
install(TARGETS _edems_scaffold
  LIBRARY DESTINATION ${CUDAQ_EDEMS_PYTHON_INSTALL_DIR} COMPONENT EdemsPython
  RUNTIME DESTINATION ${CUDAQ_EDEMS_PYTHON_INSTALL_DIR} COMPONENT EdemsPython)
install(FILES ${_python_package}/__init__.py
  DESTINATION ${CUDAQ_EDEMS_PYTHON_INSTALL_DIR} COMPONENT EdemsPython)
