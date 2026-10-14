/*******************************************************************************
 * Copyright (c) 2026 NVIDIA Corporation & Affiliates.                         *
 * All rights reserved.                                                        *
 *                                                                             *
 * This source code and the accompanying materials are made available under    *
 * the terms of the Apache License 2.0 which accompanies this distribution.    *
 ******************************************************************************/

#include "cudaq/edems/scaffold.h"

extern "C" cudaq_edems_scaffold_model_shape
cudaq_edems_scaffold_empty_model(void) {
  return {cudaq_gf2_scaffold_empty_matrix()};
}

cudaq::edem::scaffold::model_shape cudaq::edem::scaffold::empty_model() {
  return cudaq_edems_scaffold_empty_model();
}
