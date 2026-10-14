/*******************************************************************************
 * Copyright (c) 2026 NVIDIA Corporation & Affiliates.                         *
 * All rights reserved.                                                        *
 *                                                                             *
 * This source code and the accompanying materials are made available under    *
 * the terms of the Apache License 2.0 which accompanies this distribution.    *
 ******************************************************************************/

#pragma once

#include "cudaq/gf2/capi.h"

// Temporary packaging probes, not the GF2 API.
namespace cudaq::gf2::scaffold {
using matrix_shape = ::cudaq_gf2_scaffold_matrix_shape;
/// Return the temporary C ABI shape through a C++ entry point.
matrix_shape empty_matrix();
} // namespace cudaq::gf2::scaffold
