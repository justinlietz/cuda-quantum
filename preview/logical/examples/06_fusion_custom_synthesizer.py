# Copyright (c) 2026 NVIDIA Corporation & Affiliates.
# Licensed under the Apache License, Version 2.0.
"""Fuse a CUDA-Q rotation chain before invoking a Python Rz synthesizer.

CUDA-Q compiles every Logical import through a ``quake-unmapped`` stage with
the ``t-count`` cost model. From optimization level 2 that stage fuses
one-qubit chains, so the example selects fusion with the optimization level
instead of running passes itself. The toy plugin delegates to native GridSynth
so that the comparison isolates fusion. Both levels are checked against the
original unitary at the same error budget.
"""

import math
import re

import numpy as np

import cudaq
import cudaq.logical as cql
from cudaq.logical.compiler import CliffordTWord


@cudaq.kernel
def rotation_chain():
    q = cudaq.qubit()
    rx(0.125, q)
    rz(0.25, q)
    ry(-0.375, q)
    rx(0.5, q)
    rz(-0.625, q)
    ry(0.75, q)
    mz(q)


class RecordingSynthesizer:
    """A user-owned Rz adapter; replace ``rz`` with an external algorithm."""

    name = "fusion-example"
    version = "0.1.0"

    def __init__(self):
        self.calls = []

    def rz(self, angle, precision):
        self.calls.append((angle, precision))
        expansions = {
            "H": ("h",),
            "S": ("s",),
            "T": ("t",),
            "X": ("h", "s", "s", "h"),
            "W": (),
            "I": (),
        }
        # Reserve room for the logical compiler's independent word verifier.
        sequence = str(cudaq.synth.gridsynth(angle, precision / 2, seed=0))
        gates = tuple(gate for native in reversed(sequence)
                      for gate in expansions[native])
        error = cudaq.synth.rz_error(angle, sequence)
        return CliffordTWord(gates, precision, error, "example-gridsynth",
                             self.name, self.version)


def rotation_count(build):
    """Count the P0 rotations that synthesis will hand to the plugin."""
    return len(re.findall(r"#qlx\.action<pauli_rotation", build.to_mlir()))


def synthesize_chain(*, optimization_level, synthesizer, precision=1e-6):
    """Import the kernel at ``optimization_level``, then call the plugin.

    Level 1 only cleans up, and level 2 also fuses one-qubit chains. The stage
    keeps rotations in its basis, so CUDA-Q leaves their synthesis to the
    plugin. ``precision`` budgets this example's entire six-rotation block.
    Half is shared among the remaining rotations; the rest leaves room for
    fusion roundoff. The final numerical check measures against the original
    block. This is separate from the compiler's per-rotation ``precision``.
    """
    p0 = cql.compiler.import_cudaq(rotation_chain,
                                   optimization_level=optimization_level)
    rotations = rotation_count(p0)
    build = cql.compiler.synthesize(p0,
                                    gate_set=cql.compiler.clifford_t,
                                    precision=precision / (2 * rotations),
                                    synthesizer=synthesizer)
    return build, rotations


def original_unitary():
    """Independent matrix reference for the authored kernel, before measurement."""
    paulis = {
        "x": np.array([[0, 1], [1, 0]], dtype=complex),
        "y": np.array([[0, -1j], [1j, 0]], dtype=complex),
        "z": np.diag([1, -1]),
    }
    result = np.eye(2, dtype=complex)
    for axis, angle in (("x", 0.125), ("z", 0.25), ("y", -0.375), ("x", 0.5),
                        ("z", -0.625), ("y", 0.75)):
        rotation = (math.cos(angle / 2) * np.eye(2) -
                    1j * math.sin(angle / 2) * paulis[axis])
        result = rotation @ result
    return result


def block_error(build):
    """Check the complete H/S/T word against the original, up to global phase."""
    matrices = {
        "h": np.array([[1, 1], [1, -1]]) / math.sqrt(2),
        "s": np.diag([1, 1j]),
        "t": np.diag([1, np.exp(1j * math.pi / 4)]),
    }
    actual = np.eye(2, dtype=complex)
    for gate in re.findall(r"#qlx\.action<(h|s|t)>", build.to_mlir()):
        actual = matrices[gate] @ actual
    target = original_unitary()
    phase = np.angle(np.trace(target.conj().T @ actual))
    return float(np.linalg.norm(actual - np.exp(1j * phase) * target, ord=2))


def main():
    precision = 1e-6
    results = {}
    print(f"Whole-block error budget: {precision:g}")
    print("Level  rotations  plugin calls  T gates  measured block error")
    for level in (1, 2):
        plugin = RecordingSynthesizer()
        build, rotations = synthesize_chain(optimization_level=level,
                                            synthesizer=plugin,
                                            precision=precision)
        error = block_error(build)
        assert error <= precision
        assert plugin.calls
        assert build.synthesis.synthesizer == "fusion-example/0.1.0"
        results[level] = (rotations, len(plugin.calls), build.synthesis.t_count)
        rotations, calls, t_count = results[level]
        print(f"{level:5}  {rotations:9}  {calls:12}  {t_count:7}  {error:.3e}")
    assert results[2][0] < results[1][0]
    assert results[2][1] < results[1][1]
    print("Both results verified against the original kernel.")


if __name__ == "__main__":
    main()
