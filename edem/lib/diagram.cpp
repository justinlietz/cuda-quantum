/****************************************************************-*- C++ -*-****
 * Copyright (c) 2026 NVIDIA Corporation & Affiliates.                         *
 * All rights reserved.                                                        *
 *                                                                             *
 * This source code and the accompanying materials are made available under    *
 * the terms of the Apache License 2.0 which accompanies this distribution.    *
 ******************************************************************************/
#pragma GCC visibility push(default)
#include "cudaq/edem/diagram.h"
#pragma GCC visibility pop
#include "allocation.h"
#include "storage.h"

using namespace cudaq_gf2_private;

struct cudaq_edem_diagram_data {
  cudaq_edem_diagram_info info;
};

#define EDEM_TRY(expression)                                                   \
  do {                                                                         \
    auto status_ = static_cast<cudaq_edem_status>(expression);                 \
    if (status_ != CUDAQ_EDEM_OK)                                              \
      return status_;                                                          \
  } while (false)

namespace {
struct counts {
  uint64_t ports = 0, bundles = 0, boundaries = 0;
};
struct occurrence_info {
  uint64_t input_begin = 0, output_begin = 0;
  uint32_t indegree = 0;
};

bool add(uint64_t &sum, uint64_t n) {
  if (n > SIZE_MAX - sum)
    return false;
  sum += n;
  return true;
}

uint32_t sidebands(const cudaq_edem_shape &s, uint32_t kind) {
  switch (kind) {
  case CUDAQ_EDEM_FAULT:
    return s.fault_ports_count;
  case CUDAQ_EDEM_DETECTOR:
    return s.detector_ports_count;
  case CUDAQ_EDEM_OBSERVABLE:
    return s.observable_ports_count;
  default:
    return s.success_ports_count;
  }
}
constexpr uint32_t kinds[] = {CUDAQ_EDEM_FAULT, CUDAQ_EDEM_DETECTOR,
                              CUDAQ_EDEM_OBSERVABLE, CUDAQ_EDEM_SUCCESS};

cudaq_edem_status measure(const cudaq_edem_diagram_description &d, counts &c) {
  if (!d.source_identity || !d.registry.source_identity ||
      !array_ok(d.registry.definitions, d.registry.definition_count) ||
      !array_ok(d.occurrences, d.occurrence_count) ||
      !array_ok(d.connections, d.connection_count) ||
      !array_ok(d.inputs, d.input_count) ||
      !array_ok(d.outputs, d.output_count))
    return CUDAQ_EDEM_INVALID_ARGUMENT;
  if (d.source_identity != d.registry.source_identity)
    return CUDAQ_EDEM_INVALID_CONTENT;
  for (uint32_t i = 0; i < d.registry.definition_count; ++i) {
    const auto &def = d.registry.definitions[i];
    if (!array_ok(def.ports, def.port_count))
      return CUDAQ_EDEM_INVALID_ARGUMENT;
    if (!add(c.ports, def.port_count))
      return CUDAQ_EDEM_INSUFFICIENT_CAPACITY;
  }
  for (uint32_t i = 0; i < d.occurrence_count; ++i) {
    if (d.occurrences[i].definition >= d.registry.definition_count)
      return CUDAQ_EDEM_INVALID_CONTENT;
    cudaq_edem_shape s;
    EDEM_TRY(cudaq_edem_get_shape(
        &d.registry.definitions[d.occurrences[i].definition].model, &s));
    if (!add(c.bundles, uint64_t{s.input_code_count} + s.output_code_count))
      return CUDAQ_EDEM_INSUFFICIENT_CAPACITY;
    for (auto kind : kinds)
      if (!add(c.boundaries, sidebands(s, kind)))
        return CUDAQ_EDEM_INSUFFICIENT_CAPACITY;
  }
  return CUDAQ_EDEM_OK;
}

// One linear allocation for all persistent records. Layout is shared by the
// scratch compiler and final owner; no payload, dense adjacency or transfer
// matrix is copied or allocated here.
bool record_bytes(const cudaq_edem_diagram_description &d, const counts &c,
                  uint64_t &bytes) {
  return reserve_scratch<cudaq_edem_diagram_data>(bytes, 1) &&
         reserve_scratch<cudaq_edem_diagram_definition>(
             bytes, d.registry.definition_count) &&
         reserve_scratch<cudaq_edem_diagram_port>(bytes, c.ports) &&
         reserve_scratch<cudaq_edem_diagram_occurrence>(bytes,
                                                        d.occurrence_count) &&
         reserve_scratch<cudaq_edem_diagram_connection>(bytes,
                                                        d.connection_count) &&
         reserve_scratch<cudaq_edem_diagram_code_endpoint>(bytes,
                                                           d.input_count) &&
         reserve_scratch<cudaq_edem_diagram_code_endpoint>(bytes,
                                                           d.output_count) &&
         reserve_scratch<cudaq_edem_diagram_boundary>(bytes, c.boundaries) &&
         reserve_scratch<uint32_t>(bytes, d.occurrence_count) &&
         reserve_scratch<uint64_t>(bytes, uint64_t{d.occurrence_count} + 1) &&
         reserve_scratch<uint32_t>(bytes, d.connection_count);
}

template <class T>
void copy(std::span<T> to, const T *from) {
  if (!to.empty())
    std::copy_n(from, to.size(), to.begin());
}
struct records {
  cudaq_edem_diagram_data &data;
  std::span<cudaq_edem_diagram_definition> definitions;
  std::span<cudaq_edem_diagram_port> ports;
  std::span<cudaq_edem_diagram_boundary> boundaries;
  std::span<uint32_t> order;
  std::span<uint64_t> offsets;
  std::span<uint32_t> connections;

  records(arena &a, const cudaq_edem_diagram_description &d, const counts &c)
      : data(a.take<cudaq_edem_diagram_data>(1)[0]),
        definitions(
            a.take<cudaq_edem_diagram_definition>(d.registry.definition_count)),
        ports(a.take<cudaq_edem_diagram_port>(c.ports)) {
    auto occurrences =
        a.take<cudaq_edem_diagram_occurrence>(d.occurrence_count);
    auto edges = a.take<cudaq_edem_diagram_connection>(d.connection_count);
    auto inputs = a.take<cudaq_edem_diagram_code_endpoint>(d.input_count);
    auto outputs = a.take<cudaq_edem_diagram_code_endpoint>(d.output_count);
    boundaries = a.take<cudaq_edem_diagram_boundary>(c.boundaries);
    order = a.take<uint32_t>(d.occurrence_count);
    offsets = a.take<uint64_t>(uint64_t{d.occurrence_count} + 1);
    connections = a.take<uint32_t>(d.connection_count);
    copy(definitions, d.registry.definitions);
    uint64_t port = 0;
    for (auto &def : definitions) {
      auto local = ports.subspan(port, def.port_count);
      copy(local, def.ports);
      def.ports = local.empty() ? nullptr : local.data();
      port += def.port_count;
    }
    copy(occurrences, d.occurrences);
    copy(edges, d.connections);
    copy(inputs, d.inputs);
    copy(outputs, d.outputs);
    auto owned = d;
    owned.registry.definitions = definitions.data();
    owned.occurrences = occurrences.data();
    owned.connections = edges.data();
    owned.inputs = inputs.data();
    owned.outputs = outputs.data();
    data.info = {owned,        c.boundaries,   boundaries.data(),
                 order.data(), offsets.data(), connections.data()};
  }
};

cudaq_edem_status validate_definitions(const cudaq_edem_diagram_description &d,
                                       std::span<uint8_t> used) {
  for (uint32_t i = 0; i < d.registry.definition_count; ++i) {
    const auto &def = d.registry.definitions[i];
    cudaq_edem_shape s;
    EDEM_TRY(cudaq_edem_get_shape(&def.model, &s));
    // At most two represented bundles per local port. This also bounds the
    // reusable marker storage by the copied mapping count.
    uint64_t n = uint64_t{s.input_code_count} + s.output_code_count;
    if (n > 2ull * def.port_count)
      return CUDAQ_EDEM_INVALID_CONTENT;
    std::fill_n(used.begin(), n, uint8_t{0});
    uint64_t bound = 0;
    for (uint32_t p = 0; p < def.port_count; ++p) {
      const auto &port = def.ports[p];
      if (port.input_code_bundle == CUDAQ_EDEM_NO_CODE_BUNDLE &&
          port.output_code_bundle == CUDAQ_EDEM_NO_CODE_BUNDLE)
        return CUDAQ_EDEM_INVALID_CONTENT;
      for (uint32_t side = 0; side < 2; ++side) {
        uint32_t bundle =
            side ? port.output_code_bundle : port.input_code_bundle;
        if (bundle == CUDAQ_EDEM_NO_CODE_BUNDLE)
          continue;
        uint32_t limit = side ? s.output_code_count : s.input_code_count;
        if (bundle >= limit)
          return CUDAQ_EDEM_INVALID_CONTENT;
        uint64_t slot = (side ? s.input_code_count : 0ull) + bundle;
        if (used[slot]++)
          return CUDAQ_EDEM_INVALID_CONTENT;
        ++bound;
      }
    }
    if (bound != n)
      return CUDAQ_EDEM_INVALID_CONTENT;
  }
  return CUDAQ_EDEM_OK;
}

cudaq_edem_status endpoint(const cudaq_edem_diagram_description &d,
                           std::span<const occurrence_info> info,
                           cudaq_edem_diagram_code_endpoint e, uint32_t side,
                           uint64_t &slot, uint32_t &bundle) {
  if (e.occurrence >= d.occurrence_count)
    return CUDAQ_EDEM_INVALID_CONTENT;
  const auto &def =
      d.registry.definitions[d.occurrences[e.occurrence].definition];
  if (e.port >= def.port_count)
    return CUDAQ_EDEM_INVALID_CONTENT;
  bundle = side ? def.ports[e.port].output_code_bundle
                : def.ports[e.port].input_code_bundle;
  if (bundle == CUDAQ_EDEM_NO_CODE_BUNDLE)
    return CUDAQ_EDEM_INVALID_CONTENT;
  slot = (side ? info[e.occurrence].output_begin
               : info[e.occurrence].input_begin) +
         bundle;
  return CUDAQ_EDEM_OK;
}

cudaq_edem_status code(const cudaq_edem_view &v, uint32_t side, uint32_t bundle,
                       cudaq_edem_code_presentation &c) {
  cudaq_gf2_wire w;
  cudaq_gf2_bundle b;
  EDEM_TRY(
      cudaq_edem_resolve_endpoint(&v, side, {CUDAQ_EDEM_VIRTUAL, bundle}, &w));
  EDEM_TRY(cudaq_gf2_map_get_bundle(&v.map, side, w.bundle, &b));
  return cudaq_edem_get_code(&v, b.type, &c);
}

cudaq_edem_status compile(records &r, std::span<occurrence_info> info,
                          std::span<uint8_t> used,
                          std::span<uint64_t> cursors) {
  const auto &d = r.data.info.description;
  uint64_t begin = 0;
  for (uint32_t i = 0; i < d.occurrence_count; ++i) {
    cudaq_edem_shape s;
    EDEM_TRY(cudaq_edem_get_shape(
        &d.registry.definitions[d.occurrences[i].definition].model, &s));
    info[i].input_begin = begin;
    begin += s.input_code_count;
    info[i].output_begin = begin;
    begin += s.output_code_count;
  }
  // Mark every incidence, including exposed code bundles. No implicit zero
  // inputs, discarded outputs, fan-out or duplicate boundary entries.
  for (uint32_t i = 0; i < d.connection_count; ++i) {
    const auto &edge = d.connections[i];
    uint64_t out, in;
    uint32_t ob, ib;
    EDEM_TRY(endpoint(d, info, edge.output, CUDAQ_EDEM_OUTPUT, out, ob));
    EDEM_TRY(endpoint(d, info, edge.input, CUDAQ_EDEM_INPUT, in, ib));
    if (used[out]++ || used[in]++)
      return CUDAQ_EDEM_INVALID_CONTENT;
    const auto &left =
        d.registry.definitions[d.occurrences[edge.output.occurrence].definition]
            .model;
    const auto &right =
        d.registry.definitions[d.occurrences[edge.input.occurrence].definition]
            .model;
    cudaq_edem_code_presentation a, b;
    EDEM_TRY(code(left, CUDAQ_EDEM_OUTPUT, ob, a));
    EDEM_TRY(code(right, CUDAQ_EDEM_INPUT, ib, b));
    if (name_compare(a.name, a.name_size, b.name, b.name_size) ||
        a.syndrome_count != b.syndrome_count ||
        a.virtual_outcome_count != b.virtual_outcome_count ||
        a.logical_qubit_count != b.logical_qubit_count)
      return CUDAQ_EDEM_INVALID_CONTENT;
    // Checked EDEM bundles have widths fixed by these presentations, including
    // L=2*k. Whole-bundle matching checks every component, even at width zero.
    ++r.offsets[uint64_t{edge.output.occurrence} + 1];
    ++info[edge.input.occurrence].indegree;
  }
  for (uint32_t side = 0; side < 2; ++side) {
    uint32_t count = side ? d.output_count : d.input_count;
    const auto *ends = side ? d.outputs : d.inputs;
    for (uint32_t i = 0; i < count; ++i) {
      uint64_t slot;
      uint32_t bundle;
      EDEM_TRY(endpoint(d, info, ends[i], side, slot, bundle));
      if (used[slot]++)
        return CUDAQ_EDEM_INVALID_CONTENT;
    }
  }
  if (std::find(used.begin(), used.end(), uint8_t{0}) != used.end())
    return CUDAQ_EDEM_INVALID_CONTENT;

  for (uint32_t i = 0; i < d.occurrence_count; ++i) {
    r.offsets[uint64_t{i} + 1] += r.offsets[i];
    cursors[i] = r.offsets[i];
  }
  for (uint32_t i = 0; i < d.connection_count; ++i)
    r.connections[cursors[d.connections[i].output.occurrence]++] = i;
  // Kahn's algorithm includes self-edges and zero maps. Stable FIFO seeding
  // and input connection order make the order deterministic in O(V+E).
  uint64_t head = 0, tail = 0;
  for (uint32_t i = 0; i < d.occurrence_count; ++i)
    if (!info[i].indegree)
      r.order[tail++] = i;
  while (head < tail) {
    uint32_t i = r.order[head++];
    for (uint64_t p = r.offsets[i]; p < r.offsets[uint64_t{i} + 1]; ++p) {
      uint32_t next = d.connections[r.connections[p]].input.occurrence;
      if (!--info[next].indegree)
        r.order[tail++] = next;
    }
  }
  if (tail != d.occurrence_count)
    return CUDAQ_EDEM_INVALID_CONTENT;

  uint64_t boundary = 0;
  for (auto kind : kinds) {
    uint64_t offset = 0, port = 0;
    uint32_t side =
        kind == CUDAQ_EDEM_FAULT ? CUDAQ_EDEM_INPUT : CUDAQ_EDEM_OUTPUT;
    for (uint32_t i = 0; i < d.occurrence_count; ++i) {
      const auto &model =
          d.registry.definitions[d.occurrences[i].definition].model;
      cudaq_edem_shape s;
      EDEM_TRY(cudaq_edem_get_shape(&model, &s));
      uint32_t n = sidebands(s, kind);
      if (port + n > UINT32_MAX)
        return CUDAQ_EDEM_INSUFFICIENT_CAPACITY;
      for (uint32_t p = 0; p < n; ++p) {
        cudaq_gf2_wire w;
        uint32_t width;
        uint64_t local_offset;
        EDEM_TRY(cudaq_edem_resolve_endpoint(&model, side, {kind, p}, &w));
        EDEM_TRY(
            cudaq_gf2_map_get_wire(&model.map, side, w, &width, &local_offset));
        r.boundaries[boundary++] = {
            i, {kind, p}, static_cast<uint32_t>(port++), width, offset};
        if (width > UINT64_MAX - offset)
          return CUDAQ_EDEM_INSUFFICIENT_CAPACITY;
        offset += width;
      }
    }
  }
  return CUDAQ_EDEM_OK;
}

cudaq_edem_status build(const cudaq_edem_diagram_description *description,
                        const cudaq_gf2_memory *memory,
                        cudaq_gf2_owned_buffer *owner,
                        cudaq_edem_diagram_view *result) {
  if (!description || !valid_allocator(scratch_allocator(memory)))
    return CUDAQ_EDEM_INVALID_ARGUMENT;
  const auto &d = *description;
  counts c;
  EDEM_TRY(measure(d, c));
  uint64_t bytes = 0;
  if (!record_bytes(d, c, bytes))
    return CUDAQ_EDEM_INSUFFICIENT_CAPACITY;
  uint64_t persistent_bytes = bytes;
  if (c.ports > SIZE_MAX / 2 ||
      !reserve_scratch<occurrence_info>(bytes, d.occurrence_count) ||
      !reserve_scratch<uint8_t>(bytes, std::max(2 * c.ports, c.bundles)) ||
      !reserve_scratch<uint64_t>(bytes, d.occurrence_count))
    return CUDAQ_EDEM_INSUFFICIENT_CAPACITY;
  allocation scratch(scratch_allocator(memory));
  EDEM_TRY(scratch.reserve(bytes, scratch_alignment));
  arena a{static_cast<std::byte *>(scratch.data()), bytes};
  records compiled(a, d, c);
  auto info = a.take<occurrence_info>(d.occurrence_count);
  auto used = a.take<uint8_t>(std::max(2 * c.ports, c.bundles));
  auto cursors = a.take<uint64_t>(d.occurrence_count);
  EDEM_TRY(validate_definitions(compiled.data.info.description, used));
  std::fill(used.begin(), used.end(), uint8_t{0});
  EDEM_TRY(compile(compiled, info, used.first(c.bundles), cursors));
  if (!result)
    return CUDAQ_EDEM_OK;
  allocation storage(result_allocator(memory));
  EDEM_TRY(storage.reserve(persistent_bytes, scratch_alignment));
  arena out{static_cast<std::byte *>(storage.data()), persistent_bytes};
  records owned(out, compiled.data.info.description, c);
  copy(owned.boundaries, compiled.boundaries.data());
  copy(owned.order, compiled.order.data());
  copy(owned.offsets, compiled.offsets.data());
  copy(owned.connections, compiled.connections.data());
  *owner = storage.relinquish();
  *result = {&owned.data};
  return CUDAQ_EDEM_OK;
}
} // namespace

cudaq_edem_status
cudaq_edem_diagram_validate(const cudaq_edem_diagram_description *description,
                            const cudaq_gf2_memory *memory) {
  return build(description, memory, nullptr, nullptr);
}

cudaq_edem_status
cudaq_edem_diagram_construct(const cudaq_edem_diagram_description *description,
                             const cudaq_gf2_memory *memory,
                             cudaq_gf2_owned_buffer *owner,
                             cudaq_edem_diagram_view *result) {
  if (!owner || !result || !empty_owner(*owner) ||
      !valid_allocator(result_allocator(memory)))
    return CUDAQ_EDEM_INVALID_ARGUMENT;
  return build(description, memory, owner, result);
}

cudaq_edem_status
cudaq_edem_diagram_inspect(const cudaq_edem_diagram_view *value,
                           cudaq_edem_diagram_info *result) {
  if (!value || !value->data || !result)
    return CUDAQ_EDEM_INVALID_ARGUMENT;
  *result = value->data->info;
  return CUDAQ_EDEM_OK;
}
