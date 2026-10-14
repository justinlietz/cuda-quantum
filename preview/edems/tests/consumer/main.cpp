/*******************************************************************************
 * Copyright (c) 2026 NVIDIA Corporation & Affiliates.                         *
 * All rights reserved.                                                        *
 *                                                                             *
 * This source code and the accompanying materials are made available under    *
 * the terms of the Apache License 2.0 which accompanies this distribution.    *
 ******************************************************************************/

#include "cudaq/edems/scaffold.h"
#include "cudaq/gf2/scaffold.h"

int main() {
  const auto matrix = cudaq::gf2::scaffold::empty_matrix();
  const auto model = cudaq::edem::scaffold::empty_model();
  return matrix.rows != 0 || matrix.columns != 0 ||
         model.detector_matrix.rows != 0 || model.detector_matrix.columns != 0;
}
