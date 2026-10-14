/*******************************************************************************
 * Copyright (c) 2026 NVIDIA Corporation & Affiliates.                         *
 * All rights reserved.                                                        *
 *                                                                             *
 * This source code and the accompanying materials are made available under    *
 * the terms of the Apache License 2.0 which accompanies this distribution.    *
 ******************************************************************************/

#include "cudaq/edems/capi.h"
#include "cudaq/gf2/capi.h"

int main(void) {
  const cudaq_gf2_scaffold_matrix_shape matrix =
      cudaq_gf2_scaffold_empty_matrix();
  const cudaq_edems_scaffold_model_shape model =
      cudaq_edems_scaffold_empty_model();
  return matrix.rows != 0 || matrix.columns != 0 ||
         model.detector_matrix.rows != 0 || model.detector_matrix.columns != 0;
}
