/*******************************************************************************
 * Copyright (c) 2026 NVIDIA Corporation & Affiliates.                         *
 * All rights reserved.                                                        *
 *                                                                             *
 * This source code and the accompanying materials are made available under    *
 * the terms of the Apache License 2.0 which accompanies this distribution.    *
 ******************************************************************************/

#include "cudaq/gf2/scaffold.h"
#include <nanobind/nanobind.h>

namespace nb = nanobind;

NB_MODULE(_scaffold, module) {
  module.doc() = "Temporary GF2 packaging probes; no matrix algorithms.";
  nb::class_<cudaq::gf2::scaffold::matrix_shape>(module, "MatrixShape")
      .def_ro("rows", &cudaq::gf2::scaffold::matrix_shape::rows)
      .def_ro("columns", &cudaq::gf2::scaffold::matrix_shape::columns);
  module.def("empty_matrix", &cudaq::gf2::scaffold::empty_matrix);
}
