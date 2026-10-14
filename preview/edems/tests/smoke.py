# ============================================================================ #
# Copyright (c) 2026 NVIDIA Corporation & Affiliates.                          #
# All rights reserved.                                                         #
#                                                                              #
# This source code and the accompanying materials are made available under     #
# the terms of the Apache License 2.0 which accompanies this distribution.     #
# ============================================================================ #
"""Exercise both native libraries through an installed wheel."""

from pathlib import Path

from cudaq.edems import _scaffold as edems
from cudaq.edems import get_install_prefix
from cudaq.gf2 import _scaffold as gf2

matrix = gf2.empty_matrix()
model = edems.empty_model()
assert isinstance(matrix, gf2.MatrixShape)
assert isinstance(model, edems.ModelShape)
assert isinstance(model.detector_matrix, gf2.MatrixShape)
assert (matrix.rows, matrix.columns) == (0, 0)
assert (model.detector_matrix.rows, model.detector_matrix.columns) == (0, 0)

install_prefix = Path(get_install_prefix())
assert (install_prefix / "include/cudaq/gf2/capi.h").is_file()
assert (install_prefix / "include/cudaq/edems/capi.h").is_file()
assert (install_prefix / "lib/cmake/cudaq-gf2/cudaq-gf2-config.cmake").is_file()
assert (install_prefix /
        "lib/cmake/cudaq-edems/cudaq-edems-config.cmake").is_file()
