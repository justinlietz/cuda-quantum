/*******************************************************************************
 * Copyright (c) 2026 NVIDIA Corporation & Affiliates.                         *
 * All rights reserved.                                                        *
 *                                                                             *
 * This source code and the accompanying materials are made available under    *
 * the terms of the Apache License 2.0 which accompanies this distribution.    *
 ******************************************************************************/

#ifndef CUDAQ_EDEMS_CAPI_H
#define CUDAQ_EDEMS_CAPI_H

#include "cudaq/gf2/capi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Temporary packaging probe; not the final EDEM API. */
typedef struct cudaq_edems_scaffold_model_shape {
  cudaq_gf2_scaffold_matrix_shape detector_matrix;
} cudaq_edems_scaffold_model_shape;

/** Return a zero-initialized shape to exercise C ABI linkage. */
cudaq_edems_scaffold_model_shape cudaq_edems_scaffold_empty_model(void);

#ifdef __cplusplus
}
#endif

#endif /* CUDAQ_EDEMS_CAPI_H */
