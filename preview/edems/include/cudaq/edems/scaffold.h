/*******************************************************************************
 * Copyright (c) 2026 NVIDIA Corporation & Affiliates.                         *
 * All rights reserved.                                                        *
 *                                                                             *
 * This source code and the accompanying materials are made available under    *
 * the terms of the Apache License 2.0 which accompanies this distribution.    *
 ******************************************************************************/

#pragma once

#include "cudaq/edems/capi.h"
#include "cudaq/gf2/scaffold.h"

// Temporary packaging probes, not the EDEM API.
namespace cudaq::edem::scaffold {
using model_shape = ::cudaq_edems_scaffold_model_shape;
/// Return the temporary C ABI shape through a C++ entry point.
model_shape empty_model();
} // namespace cudaq::edem::scaffold
