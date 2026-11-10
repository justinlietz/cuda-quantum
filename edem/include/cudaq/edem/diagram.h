/******************************************************************************
 * Copyright (c) 2026 NVIDIA Corporation & Affiliates.                         *
 * All rights reserved.                                                       *
 *                                                                            *
 * This source code and the accompanying materials are made available under   *
 * the terms of the Apache License 2.0 which accompanies this distribution.    *
 *****************************************************************************/
#pragma once

#include "cudaq/edem/core.h"
#ifdef __cplusplus
extern "C" {
#endif

#define CUDAQ_EDEM_NO_CODE_BUNDLE UINT32_MAX

/* One code-bearing local gadget port. Ordinals index the model's input and
 * output code bundles independently. Preparation/readout use NO_CODE_BUNDLE
 * on the absent side. Every bundle must appear exactly once, even at width
 * zero; a port cannot have both sides absent. Local port order is preserved. */
typedef struct cudaq_edem_diagram_port {
  uint32_t input_code_bundle;
  uint32_t output_code_bundle;
} cudaq_edem_diagram_port;

typedef struct cudaq_edem_diagram_definition {
  cudaq_edem_view model;
  uint32_t port_count;
  const cudaq_edem_diagram_port *ports;
} cudaq_edem_diagram_definition;

/* Minimum registry binding, not a gadget registry implementation. The index
 * of each definition is its source GadgetId. source_identity is a nonnull
 * address token for that particular source registry, never a name or shape.
 * The higher layer supplies the complete aligned table and must not reuse the
 * token while a diagram lives. Alternate noise tables use the same token.
 * Identity is compared only, never dereferenced or owned. */
typedef struct cudaq_edem_diagram_registry {
  const void *source_identity;
  uint32_t definition_count;
  const cudaq_edem_diagram_definition *definitions;
} cudaq_edem_diagram_registry;

/* Array index is the occurrence identity, independent of execution order.
 * Sharing a definition never shares fault coordinates between occurrences. */
typedef struct cudaq_edem_diagram_occurrence {
  uint32_t definition;
} cudaq_edem_diagram_occurrence;

typedef struct cudaq_edem_diagram_code_endpoint {
  uint32_t occurrence;
  uint32_t port;
} cudaq_edem_diagram_code_endpoint;

typedef struct cudaq_edem_diagram_connection {
  cudaq_edem_diagram_code_endpoint output;
  cudaq_edem_diagram_code_endpoint input;
} cudaq_edem_diagram_connection;

/* Every code input/output is either connected once or exposed once in the
 * corresponding ordered list. Fan-out and intermediate observation are not
 * supported. Connections carry complete nominally matching (V,S,L) bundles.
 * Faults and D/O/Success responses are derived, never manually connected.
 * Disconnected components and the empty diagram are valid. */
typedef struct cudaq_edem_diagram_description {
  const void *source_identity;
  cudaq_edem_diagram_registry registry;
  uint32_t occurrence_count;
  const cudaq_edem_diagram_occurrence *occurrences;
  uint32_t connection_count;
  const cudaq_edem_diagram_connection *connections;
  uint32_t input_count;
  const cudaq_edem_diagram_code_endpoint *inputs;
  uint32_t output_count;
  const cudaq_edem_diagram_code_endpoint *outputs;
} cudaq_edem_diagram_description;

/* A derived external fault/response port. Ordered F, D, O, Success, then by
 * occurrence and local port within each kind. port and offset enumerate ports
 * and coordinates within that kind, starting at zero. local.index addresses
 * the original model endpoint; local indicators/rows retain their ordering.
 * Resolve local with cudaq_edem_resolve_endpoint to inspect exact laws or
 * matrices in the borrowed model. Zero-width ports remain present. */
typedef struct cudaq_edem_diagram_boundary {
  uint32_t occurrence;
  cudaq_edem_endpoint local;
  uint32_t port;
  uint32_t width;
  uint64_t offset;
} cudaq_edem_diagram_boundary;

/* All pointers borrow immutable topology storage, except model payload bytes
 * and the source identity token, which borrow the caller's registry lifetime.
 * Outgoing CSR contains connection indices, retaining connection input order
 * within each occurrence. topological_order contains occurrence indices;
 * it never renumbers description.occurrences or external coordinates. */
typedef struct cudaq_edem_diagram_info {
  cudaq_edem_diagram_description description;
  uint64_t boundary_count;
  const cudaq_edem_diagram_boundary *boundaries;
  const uint32_t *topological_order;
  const uint64_t *outgoing_offsets;     /* occurrence_count + 1 entries */
  const uint32_t *outgoing_connections; /* connection_count entries */
} cudaq_edem_diagram_info;

typedef struct cudaq_edem_diagram_data cudaq_edem_diagram_data;
/* Trusted token initialized only by successful construction in this ABI.
 * This process-local topology is not a packed EDEM or a serialized format. */
typedef struct cudaq_edem_diagram_view {
  const cudaq_edem_diagram_data *data;
} cudaq_edem_diagram_view;

/* Own copied topology and compiled sparse adjacency; borrow checked immutable
 * model payloads, including exact laws. Model bytes must outlive the diagram.
 * Temporary description arrays can be released after return. No scratch pointer
 * survives. NULL memory selects heap resources; explicit resources never fall
 * back. Validation uses scratch only. Construction requires an empty owner and
 * uses memory->result for one allocation; release via owned_buffer_release.
 * Failure preserves all outputs, but need not roll back arena consumption.
 * Input descriptions/model storage and output storage must be disjoint.
 * Missing references, incomplete/ambiguous bindings, mismatched presentations,
 * fan-out, duplicate producers, self-loops and structural cycles are rejected,
 * including zero-width/zero-coefficient connections. */
cudaq_edem_status
cudaq_edem_diagram_validate(const cudaq_edem_diagram_description *description,
                            const cudaq_gf2_memory *memory);
cudaq_edem_status
cudaq_edem_diagram_construct(const cudaq_edem_diagram_description *description,
                             const cudaq_gf2_memory *memory,
                             cudaq_gf2_owned_buffer *owner,
                             cudaq_edem_diagram_view *result);
cudaq_edem_status
cudaq_edem_diagram_inspect(const cudaq_edem_diagram_view *value,
                           cudaq_edem_diagram_info *result);

#ifdef __cplusplus
}
#endif
