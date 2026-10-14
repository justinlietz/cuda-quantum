/*******************************************************************************
 * Copyright (c) 2026 NVIDIA Corporation & Affiliates.                         *
 * All rights reserved.                                                        *
 *                                                                             *
 * This source code and the accompanying materials are made available under    *
 * the terms of the Apache License 2.0 which accompanies this distribution.    *
 ******************************************************************************/

#include "cudaq/edems/scaffold.h"
#include <nanobind/nanobind.h>

namespace nb = nanobind;

NB_MODULE(_scaffold, module) {
  module.doc() = "Temporary EDEM packaging probes; no decoding algorithms.";
  // Register GF2's types through its own module before exposing EDEM records.
  nb::module_::import_("cudaq.gf2._scaffold");
  nb::class_<cudaq::edem::scaffold::model_shape>(module, "ModelShape")
      .def_ro("detector_matrix",
              &cudaq::edem::scaffold::model_shape::detector_matrix);
  module.def("empty_model", &cudaq::edem::scaffold::empty_model);
}
