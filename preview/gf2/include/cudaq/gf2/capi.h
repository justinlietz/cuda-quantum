/*******************************************************************************
 * Copyright (c) 2026 NVIDIA Corporation & Affiliates.                         *
 * All rights reserved.                                                        *
 *                                                                             *
 * This source code and the accompanying materials are made available under    *
 * the terms of the Apache License 2.0 which accompanies this distribution.    *
 ******************************************************************************/

#ifndef CUDAQ_GF2_CAPI_H
#define CUDAQ_GF2_CAPI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Temporary packaging probe; not the final GF2 API. */
typedef struct cudaq_gf2_scaffold_matrix_shape {
  uint64_t rows;
  uint64_t columns;
} cudaq_gf2_scaffold_matrix_shape;

/** Return a zero-initialized shape to exercise C ABI linkage. */
cudaq_gf2_scaffold_matrix_shape cudaq_gf2_scaffold_empty_matrix(void);

#ifdef __cplusplus
}
#endif

#endif /* CUDAQ_GF2_CAPI_H */
