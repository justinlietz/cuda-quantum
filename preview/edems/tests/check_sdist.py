# ============================================================================ #
# Copyright (c) 2026 NVIDIA Corporation & Affiliates.                          #
# All rights reserved.                                                         #
#                                                                              #
# This source code and the accompanying materials are made available under     #
# the terms of the Apache License 2.0 which accompanies this distribution.     #
# ============================================================================ #
"""Check that the source distribution contains sources and no build artifacts."""

from pathlib import PurePosixPath
import sys
import tarfile

with tarfile.open(sys.argv[1]) as archive:
    paths = [PurePosixPath(member.name) for member in archive.getmembers()]
    relative = [path.parts[1:] for path in paths]
    assert all(
        not {"build", "dist", "logical"}.intersection(parts)
        for parts in relative), "Generated or unrelated sources in archive"
    required = {
        "CMakeLists.txt",
        "pyproject.toml",
        "gf2/CMakeLists.txt",
        "gf2/include/cudaq/gf2/capi.h",
        "edems/CMakeLists.txt",
        "edems/include/cudaq/edems/capi.h",
        "edems/python/cudaq/edems/__init__.py.in",
    }
    assert required.issubset({"/".join(parts) for parts in relative}), \
        "Missing library or packaging sources"
