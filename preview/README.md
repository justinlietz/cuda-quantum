# GF2 and EDEM packaging intent

The intent is to provide one `cudaq-edems` wheel that packages math
infrastructure for `cudaq-qec` and other consumers. It provides Python bindings
and native C and C++ interfaces to the same libraries. This scaffold contains
only temporary shape records and empty-result probes. It contains no GF2 or
EDEM algorithms, and the probes are not a final API.

## Libraries and Python packages

`preview/gf2` owns `cudaq-gf2`; `preview/edems` owns `cudaq-edems`, which links
GF2. Each native project is independently buildable. The `preview` umbrella
assembles both libraries and optionally their Python bindings. `-E` opts into
that assembly in the CUDA-Q build script, following the existing `-L` pattern.
Direct native builds do not require CUDA, LLVM, MLIR, CUDA-Q, or Git submodules.

The single wheel provides `cudaq.gf2` and `cudaq.edems`. It adds no
`cudaq/__init__.py` and has no CUDA-Q runtime dependency. The wheel supplies
both shared libraries, native headers, and relocatable CMake package exports.

## Native libraries inside the wheel

```text
cudaq/
  gf2/                  Python package and binding extension
  edems/                Python package and binding extension
    include/cudaq/      GF2 and EDEM C and C++ headers
    lib/                libcudaq-gf2 and libcudaq-edems
      cmake/            cudaq-gf2 and cudaq-edems package exports
```

`cudaq.edems.get_install_prefix()` returns the installed `cudaq/edems` directory
as a string. A native consumer such as `cudaq-qec` can use that prefix in
`CMAKE_PREFIX_PATH`, then call `find_package(cudaq-edems CONFIG REQUIRED)` and
link `cudaq::edems`. That target resolves GF2 transitively. GF2-only consumers
can find `cudaq-gf2` and link `cudaq::gf2`.

For a native installation, the helper returns the native installation prefix
instead. Both layouts expose the same headers, libraries, and CMake targets.

The wheel installation belongs to its Python environment. Native consumers
must keep that installation available at runtime. Shared libraries use relative
loader paths to find their bundled dependencies.

## C and C++ interface intent

Both interfaces are intended for native consumers, including `cudaq-qec`:

- The C boundary lives in `cudaq/gf2/capi.h` and `cudaq/edems/capi.h`. We aim
  to develop a stable C ABI. The current probes make no stability guarantee.
  Their records use fixed-width integer fields and their entry points have C
  linkage. C consumers need no C++ types or C++ compilation flags.
- The C++ headers expose interfaces in C++ namespaces directly. Consumers can
  use them when that suits integration or measured performance needs. These
  interfaces may evolve and require matching headers and a compatible C++
  compiler/runtime ABI.

Both interfaces share one library implementation. Future C++ APIs may support
inlining or templates, but the C ABI does not inherently require copies or
slower algorithms. Performance choices will depend on the eventual APIs and
measurements; this scaffold makes no performance claim.

## Packaging checks

CI is intended to exercise independent native builds, relocated CMake
packages, Python bindings installed from the single wheel, and C and C++
consumers compiled and linked against the pip-installed libraries. Building the
wheel from a source distribution also checks that both source trees are
packaged together.

Version `0.0.0.dev0` marks this scaffold. CI is intended to cover packaging on
Linux and macOS; release wheel repair and publication remain future work.
