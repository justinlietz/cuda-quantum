/*******************************************************************************
 * Copyright (c) 2026 NVIDIA Corporation & Affiliates.                         *
 * All rights reserved.                                                        *
 *                                                                             *
 * This source code and the accompanying materials are made available under    *
 * the terms of the Apache License 2.0 which accompanies this distribution.    *
 ******************************************************************************/

#include "cudaq/gf2/scaffold.h"

extern "C" cudaq_gf2_scaffold_matrix_shape
cudaq_gf2_scaffold_empty_matrix(void) {
  return {0, 0};
}

cudaq::gf2::scaffold::matrix_shape cudaq::gf2::scaffold::empty_matrix() {
  return cudaq_gf2_scaffold_empty_matrix();
}
