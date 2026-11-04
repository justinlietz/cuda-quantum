# Copyright (c) 2026 NVIDIA Corporation & Affiliates.
# Licensed under the Apache License, Version 2.0.

import runpy
from pathlib import Path

import pytest

import cudaq.logical as cql
from cudaq.logical._native import native

pytestmark = pytest.mark.skipif(
    not native.has_quake_import,
    reason="fusion example requires typed Quake import",
)


@pytest.fixture(scope="module")
def example():
    path = (Path(__file__).resolve().parents[4] / "examples" /
            "06_fusion_custom_synthesizer.py")
    return runpy.run_path(str(path))


@pytest.mark.parametrize("precision", [1e-4, 1e-6])
def test_fusion_precedes_external_synthesis(example, precision):
    results = []
    for fuse, expected_rotations in ((False, 6), (True, 3)):
        plugin = example["RecordingSynthesizer"]()
        build, quake = example["synthesize_chain"](fuse=fuse,
                                                   synthesizer=plugin,
                                                   precision=precision)
        assert example["rotation_count"](quake) == expected_rotations
        assert len(plugin.calls) == expected_rotations
        assert all(eps == precision / (2 * expected_rotations)
                   for _, eps in plugin.calls)
        assert build.synthesis.synthesizer == "fusion-example/0.1.0"
        # Check the entire word against the original kernel, including fusion
        # drift and the accumulated errors of all independently checked words.
        assert example["block_error"](build) <= precision
        results.append(build.synthesis.t_count)
    assert results[1] < results[0]


def test_fused_word_still_requires_independent_verification(example):
    plugin = example["RecordingSynthesizer"]()
    plugin.rz = lambda angle, eps: cql.compiler.CliffordTWord(
        (), eps, 0.0, "incorrect-identity", plugin.name, plugin.version)
    with pytest.raises(RuntimeError, match="exceeds requested precision"):
        example["synthesize_chain"](fuse=True, synthesizer=plugin)


@pytest.mark.parametrize("fuse", [False, True])
def test_u3_synthesizer_receives_one_merged_chain(example, synthesizer_factory,
                                                  fuse):
    precision = 1e-6
    plugin = synthesizer_factory(direct=True)
    build, quake = example["synthesize_chain"](fuse=fuse,
                                               synthesizer=plugin,
                                               precision=precision)
    assert example["rotation_count"](quake) == (3 if fuse else 6)
    assert [call[0] for call in plugin.calls] == ["u3"]
    assert plugin.calls[0][-1] <= precision / (2 * (3 if fuse else 6))
    assert example["block_error"](build) <= precision
