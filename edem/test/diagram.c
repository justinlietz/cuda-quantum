/******************************************************************************
 * Copyright (c) 2026 NVIDIA Corporation & Affiliates.                         *
 * All rights reserved.                                                       *
 *                                                                            *
 * This source code and the accompanying materials are made available under   *
 * the terms of the Apache License 2.0 which accompanies this distribution.    *
 *****************************************************************************/
#include "cudaq/edem/diagram.h"
#include "../../math/gf2/test/test_allocator.h"

enum { PREP, SE, CNOT, READ, MODEL_COUNT };
#define NONE CUDAQ_EDEM_NO_CODE_BUNDLE

/* Nonzero noisy code models: (V,S,L) widths (1,1,2), independent and
 * exclusive exact laws, two fault ports, and all three response kinds.
 * CNOT is one two-code model with the usual X/Z propagation across blocks. */
static void model(unsigned kind, const cudaq_edem_code_presentation *code,
                  uint32_t fault_count, double probability,
                  cudaq_gf2_owned_buffer *owner, cudaq_edem_view *value) {
  uint32_t ids[] = {0, 1, 2, 3}, code_ids[] = {0, 0};
  double probabilities[] = {probability, 0.1, 0.2, 0.05};
  cudaq_edem_fault_group groups[] = {
      {CUDAQ_EDEM_INDEPENDENT, 1, ids, probabilities},
      {CUDAQ_EDEM_EXCLUSIVE, fault_count - 1, ids + 1, probabilities + 1}};
  cudaq_edem_fault_group second = {CUDAQ_EDEM_INDEPENDENT, 1, ids,
                                   probabilities};
  cudaq_edem_fault_distribution faults[] = {{fault_count, 2, groups},
                                            {1, 1, &second}};
  const uint32_t sizes[] = {1, 0};
  const uint64_t offsets[] = {0, 1, 2}, f_offsets[] = {0, 1, 1, 2, 2};
  const uint32_t rows[] = {0, 1}, f_rows[] = {0, 0};
  const cudaq_gf2_csc_view bit = {1, 1, 1, offsets, rows};
  const cudaq_gf2_csc_view logical = {2, 2, 2, offsets, rows};
  const uint64_t cross_offsets[] = {0, 1, 1}, z_offsets[] = {0, 0, 1};
  const uint32_t z_row = 1;
  const cudaq_gf2_csc_view x = {2, 2, 1, cross_offsets, rows};
  const cudaq_gf2_csc_view z = {2, 2, 1, z_offsets, &z_row};
  cudaq_edem_matrix_block blocks[16];
  unsigned n = 0, input_count = kind == PREP ? 0 : kind == CNOT ? 2 : 1;
  unsigned output_count = kind == READ ? 0 : kind == CNOT ? 2 : 1;
  if (input_count && output_count)
    for (uint32_t p = 0; p < input_count; ++p)
      for (uint32_t k = CUDAQ_EDEM_VIRTUAL; k <= CUDAQ_EDEM_LOGICAL; ++k)
        blocks[n++] = (cudaq_edem_matrix_block){
            {k, p}, {k, p}, k == CUDAQ_EDEM_LOGICAL ? logical : bit};
  if (kind == CNOT) {
    blocks[n++] = (cudaq_edem_matrix_block){
        {CUDAQ_EDEM_LOGICAL, 1}, {CUDAQ_EDEM_LOGICAL, 0}, x};
    blocks[n++] = (cudaq_edem_matrix_block){
        {CUDAQ_EDEM_LOGICAL, 0}, {CUDAQ_EDEM_LOGICAL, 1}, z};
  }
  for (uint32_t k = CUDAQ_EDEM_DETECTOR; k <= CUDAQ_EDEM_SUCCESS; ++k)
    blocks[n++] = (cudaq_edem_matrix_block){
        {k, 0}, {CUDAQ_EDEM_FAULT, 0}, {1, fault_count, 2, f_offsets, f_rows}};
  if (output_count)
    blocks[n++] =
        (cudaq_edem_matrix_block){{CUDAQ_EDEM_SYNDROME, 0},
                                  {CUDAQ_EDEM_FAULT, 1},
                                  {code->syndrome_count, 1, 1, offsets, rows}};
  cudaq_edem_description d = {0};
  d.shape = (cudaq_edem_shape){input_count, output_count, 2, 2, 1, 1};
  d.code_table = (cudaq_edem_code_table){1, code};
  d.input_code_indices = code_ids;
  d.output_code_indices = code_ids;
  d.fault_ports_distributions = faults;
  d.detector_ports_sizes = sizes;
  d.observable_ports_sizes = sizes;
  d.success_ports_sizes = sizes;
  d.block_count = n;
  d.blocks = blocks;
  cudaq_edem_status status = cudaq_edem_construct(&d, 0, owner, value);
  if (status != CUDAQ_EDEM_OK)
    fprintf(stderr, "model kind=%u V=%u S=%u L=%u: %s\n", kind,
            code->virtual_outcome_count, code->syndrome_count,
            code->logical_qubit_count, cudaq_edem_status_string(status));
  TA_CHECK(status == CUDAQ_EDEM_OK);
}

typedef struct fixture {
  int identity;
  cudaq_gf2_owned_buffer owners[MODEL_COUNT];
  cudaq_edem_diagram_definition definitions[MODEL_COUNT];
  cudaq_edem_diagram_port ports[5];
  cudaq_edem_diagram_occurrence occurrences[64];
  cudaq_edem_diagram_connection connections[64];
  cudaq_edem_diagram_description d;
} fixture;

static void init(fixture *f) {
  memset(f, 0, sizeof(*f));
  const cudaq_edem_code_presentation code = {"block", 5, 1, 1, 1};
  const cudaq_edem_diagram_port ports[] = {
      {NONE, 0}, {0, 0}, {0, 0}, {1, 1}, {0, NONE}};
  memcpy(f->ports, ports, sizeof(ports));
  const uint32_t starts[] = {0, 1, 2, 4};
  for (unsigned k = 0; k < MODEL_COUNT; ++k) {
    model(k, &code, 3, 0.03, f->owners + k, &f->definitions[k].model);
    f->definitions[k].port_count = k == CNOT ? 2 : 1;
    f->definitions[k].ports = f->ports + starts[k];
  }
  f->d.source_identity = &f->identity;
  f->d.registry =
      (cudaq_edem_diagram_registry){&f->identity, MODEL_COUNT, f->definitions};
  f->d.occurrences = f->occurrences;
  f->d.connections = f->connections;
}
static void release(fixture *f) {
  for (unsigned k = 0; k < MODEL_COUNT; ++k)
    cudaq_gf2_owned_buffer_release(f->owners + k);
}
static void connect(fixture *f, unsigned a, unsigned ap, unsigned b,
                    unsigned bp) {
  f->connections[f->d.connection_count++] =
      (cudaq_edem_diagram_connection){{a, ap}, {b, bp}};
}
static void experiment(fixture *f, unsigned rounds) {
  f->d.occurrence_count = 0;
  f->d.connection_count = 0;
  uint32_t last[] = {0, 1}, last_port[] = {0, 0};
  f->occurrences[f->d.occurrence_count++].definition = PREP;
  f->occurrences[f->d.occurrence_count++].definition = PREP;
  for (unsigned stage = 0; stage < 2; ++stage) {
    for (unsigned r = 0; r < rounds; ++r)
      for (unsigned b = 0; b < 2; ++b) {
        uint32_t i = f->d.occurrence_count++;
        f->occurrences[i].definition = SE;
        connect(f, last[b], last_port[b], i, 0);
        last[b] = i;
        last_port[b] = 0;
      }
    if (!stage) {
      uint32_t i = f->d.occurrence_count++;
      f->occurrences[i].definition = CNOT;
      for (unsigned b = 0; b < 2; ++b) {
        connect(f, last[b], last_port[b], i, b);
        last[b] = i;
        last_port[b] = b;
      }
    }
  }
  for (unsigned b = 0; b < 2; ++b) {
    uint32_t i = f->d.occurrence_count++;
    f->occurrences[i].definition = READ;
    connect(f, last[b], last_port[b], i, 0);
  }
}

static cudaq_edem_diagram_info inspect(cudaq_edem_diagram_view v) {
  cudaq_edem_diagram_info info;
  TA_CHECK(cudaq_edem_diagram_inspect(&v, &info) == CUDAQ_EDEM_OK);
  return info;
}

/* Validate every derived coordinate against an independent occurrence/local
 * enumeration and inspect laws through the original model endpoint. */
static void check_boundaries(cudaq_edem_diagram_info info) {
  uint64_t index = 0;
  const uint32_t kinds[] = {CUDAQ_EDEM_FAULT, CUDAQ_EDEM_DETECTOR,
                            CUDAQ_EDEM_OBSERVABLE, CUDAQ_EDEM_SUCCESS};
  for (unsigned k = 0; k < 4; ++k) {
    uint32_t global_port = 0;
    uint64_t offset = 0;
    for (uint32_t i = 0; i < info.description.occurrence_count; ++i) {
      const cudaq_edem_view *m =
          &info.description.registry
               .definitions[info.description.occurrences[i].definition]
               .model;
      const uint32_t count = k < 2 ? 2 : 1;
      for (uint32_t p = 0; p < count; ++p) {
        const cudaq_edem_diagram_boundary *b = info.boundaries + index++;
        cudaq_gf2_wire w;
        uint32_t width;
        uint64_t local_offset;
        uint32_t side = k ? CUDAQ_EDEM_OUTPUT : CUDAQ_EDEM_INPUT;
        TA_CHECK(cudaq_edem_resolve_endpoint(m, side,
                                             (cudaq_edem_endpoint){kinds[k], p},
                                             &w) == CUDAQ_EDEM_OK);
        TA_CHECK(cudaq_gf2_map_get_wire(&m->map, side, w, &width,
                                        &local_offset) == CUDAQ_GF2_OK);
        TA_CHECK(b->occurrence == i && b->local.kind == kinds[k] &&
                 b->local.index == p && b->port == global_port++ &&
                 b->width == width && b->offset == offset);
        offset += width;
        if (!k) {
          cudaq_edem_fault_group group;
          TA_CHECK(cudaq_edem_get_group(m, w.bundle, 0, &group) ==
                   CUDAQ_EDEM_OK);
          TA_CHECK(group.kind == CUDAQ_EDEM_INDEPENDENT && group.size == 1);
          if (!p) {
            TA_CHECK(cudaq_edem_get_group(m, w.bundle, 1, &group) ==
                     CUDAQ_EDEM_OK);
            TA_CHECK(group.kind == CUDAQ_EDEM_EXCLUSIVE &&
                     group.fault_ids[0] == 1 && group.fault_ids[1] == 2 &&
                     group.probabilities[0] == 0.1 &&
                     group.probabilities[1] == 0.2);
          }
        }
      }
    }
  }
  TA_CHECK(index == info.boundary_count);
}

static void check_graph(cudaq_edem_diagram_info info) {
  const cudaq_edem_diagram_description *d = &info.description;
  uint32_t position[64];
  for (uint32_t i = 0; i < d->occurrence_count; ++i) {
    TA_CHECK(info.topological_order[i] < d->occurrence_count);
    position[info.topological_order[i]] = i;
  }
  TA_CHECK(info.outgoing_offsets[0] == 0 &&
           info.outgoing_offsets[d->occurrence_count] == d->connection_count);
  uint32_t seen[64] = {0};
  for (uint32_t i = 0; i < d->occurrence_count; ++i)
    for (uint64_t j = info.outgoing_offsets[i];
         j < info.outgoing_offsets[i + 1]; ++j) {
      uint32_t e = info.outgoing_connections[j];
      TA_CHECK(e < d->connection_count && !seen[e]++);
      TA_CHECK(d->connections[e].output.occurrence == i);
      TA_CHECK(position[i] < position[d->connections[e].input.occurrence]);
    }
}

static void cnot_experiment(void) {
  fixture f;
  init(&f);
  for (unsigned rounds = 1; rounds <= 5; ++rounds) {
    experiment(&f, rounds);
    cudaq_gf2_owned_buffer owner = {0};
    cudaq_edem_diagram_view v;
    TA_CHECK(cudaq_edem_diagram_validate(&f.d, 0) == CUDAQ_EDEM_OK);
    TA_CHECK(cudaq_edem_diagram_construct(&f.d, 0, &owner, &v) ==
             CUDAQ_EDEM_OK);
    cudaq_edem_diagram_info info = inspect(v);
    TA_CHECK(info.description.occurrence_count == 5 + 4 * rounds);
    TA_CHECK(info.description.connection_count == 4 + 4 * rounds);
    TA_CHECK(info.description.registry.definition_count == MODEL_COUNT &&
             info.description.registry.source_identity == &f.identity);
    TA_CHECK(info.description.occurrences[2 + 2 * rounds].definition == CNOT);
    TA_CHECK(info.description.registry.definitions[CNOT].port_count == 2);
    for (uint32_t k = 0; k < MODEL_COUNT; ++k)
      TA_CHECK(info.description.registry.definitions[k].model.map.data ==
               f.definitions[k].model.map.data);
    check_boundaries(info);
    check_graph(info);
    // Validate the model's two-block X propagation through the shared payload.
    const cudaq_edem_view *m =
        &info.description.registry.definitions[CNOT].model;
    cudaq_gf2_wire w;
    uint32_t width;
    uint64_t offset;
    uint8_t input[12] = {0}, output[12] = {0};
    TA_CHECK(cudaq_edem_resolve_endpoint(
                 m, CUDAQ_EDEM_INPUT,
                 (cudaq_edem_endpoint){CUDAQ_EDEM_LOGICAL, 0},
                 &w) == CUDAQ_EDEM_OK);
    TA_CHECK(cudaq_gf2_map_get_wire(&m->map, CUDAQ_EDEM_INPUT, w, &width,
                                    &offset) == CUDAQ_GF2_OK);
    input[offset] = 1;
    uint64_t ni, no;
    TA_CHECK(cudaq_gf2_map_vector_sizes(&m->map, &ni, &no) == CUDAQ_GF2_OK);
    TA_CHECK(ni == 12 && no == 11);
    TA_CHECK(cudaq_gf2_map_apply(&m->map, input, ni, output, no) ==
             CUDAQ_GF2_OK);
    for (unsigned p = 0; p < 2; ++p) {
      TA_CHECK(cudaq_edem_resolve_endpoint(
                   m, CUDAQ_EDEM_OUTPUT,
                   (cudaq_edem_endpoint){CUDAQ_EDEM_LOGICAL, p},
                   &w) == CUDAQ_EDEM_OK);
      TA_CHECK(cudaq_gf2_map_get_wire(&m->map, CUDAQ_EDEM_OUTPUT, w, &width,
                                      &offset) == CUDAQ_GF2_OK);
      TA_CHECK(output[offset] == 1 && output[offset + 1] == 0);
    }
    cudaq_gf2_owned_buffer_release(&owner);
  }
  release(&f);
}

static void failure_sites(const cudaq_edem_diagram_description *d) {
  test_resource resource;
  test_resource_init(&resource);
  cudaq_gf2_memory memory = test_memory(&resource, &resource);
  cudaq_gf2_owned_buffer owner = {0};
  cudaq_edem_diagram_view v = {0};
  TA_CHECK(cudaq_edem_diagram_construct(d, &memory, &owner, &v) ==
           CUDAQ_EDEM_OK);
  uint64_t attempts = resource.attempts;
  TA_CHECK(attempts == 2);
  cudaq_edem_diagram_view sentinel = v;
  cudaq_gf2_owned_buffer_release(&owner);
  test_resource_balanced(&resource);
  for (uint64_t fail = 1; fail <= attempts; ++fail) {
    test_resource_init(&resource);
    resource.fail_on_attempt = fail;
    v = sentinel;
    TA_CHECK(cudaq_edem_diagram_construct(d, &memory, &owner, &v) ==
             CUDAQ_EDEM_ALLOCATION_FAILED);
    TA_CHECK(test_same_owner(owner, (cudaq_gf2_owned_buffer){0}) &&
             v.data == sentinel.data);
    test_resource_balanced(&resource);
  }
  test_resource_init(&resource);
  memory.result = (cudaq_gf2_allocator){0};
  TA_CHECK(cudaq_edem_diagram_validate(d, &memory) == CUDAQ_EDEM_OK);
  TA_CHECK(resource.attempts == 1);
  test_resource_balanced(&resource);
  resource.fail_on_attempt = resource.attempts + 1;
  TA_CHECK(cudaq_edem_diagram_validate(d, &memory) ==
           CUDAQ_EDEM_ALLOCATION_FAILED);
  test_resource_balanced(&resource);
}

static void bounded_ownership(void) {
  fixture f;
  init(&f);
  experiment(&f, 2);
  failure_sites(&f.d);
  _Alignas(8) unsigned char scratch_bytes[32768], result_bytes[32768];
  test_resource scratch, result;
  test_resource_bump(&scratch, scratch_bytes, sizeof(scratch_bytes));
  test_resource_bump(&result, result_bytes, sizeof(result_bytes));
  cudaq_gf2_memory memory = test_memory(&scratch, &result);
  cudaq_gf2_owned_buffer owner = {0};
  cudaq_edem_diagram_view v;
  TA_CHECK(cudaq_edem_diagram_construct(&f.d, &memory, &owner, &v) ==
           CUDAQ_EDEM_OK);
  TA_CHECK(owner.data >= (void *)result_bytes &&
           (unsigned char *)owner.data < result_bytes + sizeof(result_bytes));
  test_resource_reset(&scratch);
  // Destroy every temporary descriptor/mapping. Only payload owners and the
  // identity token remain. Poison scratch to expose retained scratch pointers.
  memset(f.definitions, 0xa5, sizeof(f.definitions));
  memset(f.ports, 0xa5, sizeof(f.ports));
  memset(f.occurrences, 0xa5, sizeof(f.occurrences));
  memset(f.connections, 0xa5, sizeof(f.connections));
  memset(&f.d, 0xa5, sizeof(f.d));
  cudaq_edem_diagram_info info = inspect(v);
  check_graph(info);
  check_boundaries(info);
  TA_CHECK(
      info.description.registry.definitions[SE].ports[0].input_code_bundle ==
      0);
  TA_CHECK(info.description.registry.definitions[SE].model.map.data ==
           f.owners[SE].data);
  // Both explicit resource failures preserve publication and never fall back.
  cudaq_edem_diagram_description saved = info.description;
  cudaq_edem_diagram_view sentinel = v;
  cudaq_gf2_owned_buffer failed_owner = {0};
  scratch.arena.capacity = 0;
  TA_CHECK(cudaq_edem_diagram_construct(&saved, &memory, &failed_owner, &v) ==
           CUDAQ_EDEM_ALLOCATION_FAILED);
  TA_CHECK(!failed_owner.data && v.data == sentinel.data);
  scratch.arena.capacity = sizeof(scratch_bytes);
  result.arena.capacity = result.arena.used;
  TA_CHECK(cudaq_edem_diagram_construct(&saved, &memory, &failed_owner, &v) ==
           CUDAQ_EDEM_ALLOCATION_FAILED);
  TA_CHECK(!failed_owner.data && v.data == sentinel.data);
  test_resource_balanced(&scratch);
  cudaq_gf2_owned_buffer_release(&owner);
  test_resource_balanced(&result);
  // Releasing topology does not release or change borrowed model allocations.
  for (unsigned i = 0; i < MODEL_COUNT; ++i) {
    cudaq_edem_view m;
    TA_CHECK(cudaq_edem_read(f.owners[i].data, f.owners[i].size, 0, &m) ==
             CUDAQ_EDEM_OK);
  }
  release(&f);
}

static void reject(const cudaq_edem_diagram_description *d) {
  cudaq_gf2_owned_buffer owner = {0};
  cudaq_edem_diagram_view v = {0};
  test_resource resource;
  test_resource_init(&resource);
  cudaq_gf2_memory memory = test_memory(&resource, &resource);
  TA_CHECK(cudaq_edem_diagram_validate(d, &memory) ==
           CUDAQ_EDEM_INVALID_CONTENT);
  TA_CHECK(cudaq_edem_diagram_construct(d, &memory, &owner, &v) ==
           CUDAQ_EDEM_INVALID_CONTENT);
  TA_CHECK(!owner.data && !v.data);
  test_resource_balanced(&resource);
}

static void invalid(void) {
  fixture f;
  init(&f);
  experiment(&f, 1);
  int unrelated_registry;
  f.d.source_identity = &unrelated_registry;
  reject(&f.d);
  f.d.source_identity = &f.identity;
  f.occurrences[0].definition = MODEL_COUNT;
  reject(&f.d);
  f.occurrences[0].definition = PREP;
  cudaq_edem_diagram_connection saved = f.connections[0];
  f.connections[0].input.occurrence = 99;
  reject(&f.d);
  f.connections[0] = saved;
  f.connections[0].input.port = 1;
  reject(&f.d);
  f.connections[0] = saved;
  f.connections[0].input.occurrence = 0; // absent preparation input
  reject(&f.d);
  f.connections[0] = saved;
  --f.d.connection_count; // missing binding
  reject(&f.d);
  ++f.d.connection_count;
  f.connections[f.d.connection_count++] = saved; // duplicate connection
  reject(&f.d);
  --f.d.connection_count;
  cudaq_edem_diagram_connection second = f.connections[1];
  f.connections[1].input = saved.input; // conflicting producer
  reject(&f.d);
  f.connections[1] = second;
  f.connections[1].output = saved.output; // fan-out
  reject(&f.d);
  f.connections[1] = second;
  f.ports[3].input_code_bundle = 0; // duplicate local bundle mapping
  reject(&f.d);
  f.ports[3].input_code_bundle = 2; // nonexistent local bundle
  reject(&f.d);
  f.ports[3].input_code_bundle = 1;
  f.definitions[CNOT].port_count = 1; // incomplete local mapping
  reject(&f.d);
  f.definitions[CNOT].port_count = 2;
  f.ports[0].output_code_bundle = NONE; // both absent
  reject(&f.d);
  f.ports[0].output_code_bundle = 0;
  f.ports[0].input_code_bundle = 0; // nonexistent preparation bundle
  reject(&f.d);
  f.ports[0].input_code_bundle = NONE;
  // Equal width with a different name, then a different width of each V,S,L.
  for (unsigned k = 0; k < 4; ++k) {
    cudaq_edem_code_presentation code = {"block", 5, 1, 1, 1};
    if (!k) {
      code.name = "other";
    }
    if (k == 1)
      ++code.syndrome_count;
    if (k == 2)
      ++code.virtual_outcome_count;
    if (k == 3)
      ++code.logical_qubit_count;
    // PREP has no code->code identity blocks, so arbitrary widths are valid.
    cudaq_gf2_owned_buffer alternate_owner = {0};
    cudaq_edem_view alternate;
    model(PREP, &code, 3, 0.03, &alternate_owner, &alternate);
    cudaq_edem_view original = f.definitions[PREP].model;
    f.definitions[PREP].model = alternate;
    reject(&f.d);
    f.definitions[PREP].model = original;
    cudaq_gf2_owned_buffer_release(&alternate_owner);
  }
  release(&f);
}

static void open_reuse_and_replacement(void) {
  fixture f;
  init(&f);
  f.d.occurrence_count = 2;
  f.occurrences[0].definition = SE;
  f.occurrences[1].definition = SE;
  // Reverse causal order to prove occurrence identity is independent of
  // topological order. Explicit boundaries preserve the caller's ordering.
  connect(&f, 1, 0, 0, 0);
  cudaq_edem_diagram_code_endpoint in = {1, 0}, out = {0, 0};
  f.d.input_count = f.d.output_count = 1;
  f.d.inputs = &in;
  f.d.outputs = &out;
  cudaq_gf2_owned_buffer owners[2] = {{0}};
  cudaq_edem_diagram_view views[2];
  TA_CHECK(cudaq_edem_diagram_construct(&f.d, 0, owners, views) ==
           CUDAQ_EDEM_OK);
  cudaq_edem_diagram_info original = inspect(views[0]);
  TA_CHECK(original.topological_order[0] == 1 &&
           original.topological_order[1] == 0);
  TA_CHECK(original.description.inputs[0].occurrence == 1 &&
           original.description.outputs[0].occurrence == 0);
  check_boundaries(original);
  check_graph(original);
  f.d.inputs = &out; // duplicate connected input, and missing true input
  reject(&f.d);
  f.d.inputs = &in;
  f.d.outputs = &in; // observing an internally consumed output
  reject(&f.d);
  f.d.outputs = &out;
  cudaq_gf2_owned_buffer alternate_owner = {0};
  cudaq_edem_view alternate;
  const cudaq_edem_code_presentation code = {"block", 5, 1, 1, 1};
  // A probability-only replacement preserves coordinate schemas and exact
  // response analysis; it still publishes a new immutable complete model.
  model(SE, &code, 3, 0.07, &alternate_owner, &alternate);
  cudaq_edem_view source_model = f.definitions[SE].model;
  f.definitions[SE].model = alternate;
  TA_CHECK(cudaq_edem_diagram_construct(&f.d, 0, owners + 1, views + 1) ==
           CUDAQ_EDEM_OK);
  cudaq_edem_diagram_info probability_only = inspect(views[1]);
  TA_CHECK(
      probability_only.boundaries[0].width == original.boundaries[0].width &&
      probability_only.boundaries[2].offset == original.boundaries[2].offset);
  cudaq_edem_fault_group original_group, replacement_group;
  cudaq_gf2_wire wire;
  cudaq_edem_endpoint fault = {CUDAQ_EDEM_FAULT, 0};
  TA_CHECK(cudaq_edem_resolve_endpoint(&source_model, CUDAQ_EDEM_INPUT, fault,
                                       &wire) == CUDAQ_EDEM_OK);
  TA_CHECK(cudaq_edem_get_group(&source_model, wire.bundle, 0,
                                &original_group) == CUDAQ_EDEM_OK);
  TA_CHECK(cudaq_edem_resolve_endpoint(&alternate, CUDAQ_EDEM_INPUT, fault,
                                       &wire) == CUDAQ_EDEM_OK);
  TA_CHECK(cudaq_edem_get_group(&alternate, wire.bundle, 0,
                                &replacement_group) == CUDAQ_EDEM_OK);
  TA_CHECK(original_group.probabilities[0] == 0.03 &&
           replacement_group.probabilities[0] == 0.07);
  check_boundaries(probability_only);
  cudaq_gf2_owned_buffer_release(owners + 1);
  f.definitions[SE].model = source_model;
  cudaq_gf2_owned_buffer_release(&alternate_owner);
  model(SE, &code, 4, 0.07, &alternate_owner, &alternate);
  f.definitions[SE].model = alternate;
  TA_CHECK(cudaq_edem_diagram_construct(&f.d, 0, owners + 1, views + 1) ==
           CUDAQ_EDEM_OK);
  cudaq_edem_diagram_info changed = inspect(views[1]);
  TA_CHECK(original.boundary_count == changed.boundary_count);
  TA_CHECK(original.boundaries[0].width == 3 &&
           changed.boundaries[0].width == 4);
  TA_CHECK(original.boundaries[2].offset == 4 &&
           changed.boundaries[2].offset == 5);
  TA_CHECK(original.description.registry.definitions[SE].model.map.data !=
           alternate.map.data);
  check_boundaries(changed);
  check_boundaries(original); // replacement cannot mutate an existing diagram
  for (unsigned i = 0; i < 2; ++i)
    cudaq_gf2_owned_buffer_release(owners + i);
  f.definitions[SE].model = source_model;
  cudaq_gf2_owned_buffer_release(&alternate_owner);

  // Reusing one physical block after destructive readout starts a disconnected
  // lifetime: prep->read; prep->read. No edge joins old state to fresh prep.
  f.d.input_count = f.d.output_count = 0;
  f.d.connection_count = 0;
  f.d.occurrence_count = 4;
  for (unsigned i = 0; i < 4; ++i)
    f.occurrences[i].definition = i % 2 ? READ : PREP;
  connect(&f, 0, 0, 1, 0);
  connect(&f, 2, 0, 3, 0);
  TA_CHECK(cudaq_edem_diagram_construct(&f.d, 0, owners, views) ==
           CUDAQ_EDEM_OK);
  check_graph(inspect(views[0]));
  connect(&f, 1, 0, 2, 0); // neither readout output nor prep input exists
  reject(&f.d);
  cudaq_gf2_owned_buffer_release(owners);
  release(&f);
}

static void zero_cycles_and_mapping(void) {
  int identity;
  const uint32_t indices[] = {0, 0};
  const cudaq_edem_code_presentation code = {"zero", 4, 0, 0, 0};
  cudaq_edem_description m = {0};
  m.code_table = (cudaq_edem_code_table){1, &code};
  m.shape.input_code_count = m.shape.output_code_count = 2;
  const cudaq_edem_fault_distribution no_faults = {0};
  const uint32_t zero_size = 0;
  m.shape.fault_ports_count = m.shape.detector_ports_count = 1;
  m.shape.observable_ports_count = m.shape.success_ports_count = 1;
  m.fault_ports_distributions = &no_faults;
  m.detector_ports_sizes = m.observable_ports_sizes = m.success_ports_sizes =
      &zero_size;
  m.input_code_indices = m.output_code_indices = indices;
  cudaq_gf2_owned_buffer model_owner = {0}, owner = {0};
  cudaq_edem_view model_view;
  TA_CHECK(cudaq_edem_construct(&m, 0, &model_owner, &model_view) ==
           CUDAQ_EDEM_OK);
  // Input and output mappings are independent and preserve local port ordinals.
  const cudaq_edem_diagram_port ports[] = {{1, 0}, {0, 1}};
  const cudaq_edem_diagram_definition def = {model_view, 2, ports};
  const cudaq_edem_diagram_occurrence occurrences[] = {{0}, {0}};
  cudaq_edem_diagram_connection edges[] = {{{0, 0}, {1, 1}}, {{1, 0}, {0, 1}}};
  cudaq_edem_diagram_code_endpoint ins[] = {{0, 0}, {1, 0}},
                                   outs[] = {{0, 1}, {1, 1}};
  cudaq_edem_diagram_description d = {
      &identity, {&identity, 1, &def}, 2, occurrences, 1, edges, 3, ins, 3,
      outs};
  // Supply ordered open ends for every zero-width bundle not on the one edge.
  cudaq_edem_diagram_code_endpoint open_in[] = {{1, 0}, {0, 1}, {0, 0}},
                                   open_out[] = {{1, 1}, {0, 1}, {1, 0}};
  d.inputs = open_in;
  d.outputs = open_out;
  cudaq_edem_diagram_view v;
  TA_CHECK(cudaq_edem_diagram_construct(&d, 0, &owner, &v) == CUDAQ_EDEM_OK);
  cudaq_edem_diagram_info info = inspect(v);
  TA_CHECK(info.boundary_count == 8 && info.description.input_count == 3 &&
           info.description.output_count == 3);
  for (uint64_t b = 0; b < info.boundary_count; ++b)
    TA_CHECK(info.boundaries[b].width == 0 && info.boundaries[b].offset == 0);
  TA_CHECK(
      info.description.inputs[0].occurrence == 1 &&
      info.description.registry.definitions[0].ports[0].input_code_bundle == 1);
  cudaq_gf2_owned_buffer_release(&owner);
  d.input_count = d.output_count = 2;
  d.inputs = ins;
  d.outputs = outs;
  d.connection_count = 2;
  reject(&d); // two-node cycle with all-zero models and interfaces
  d.occurrence_count = 1;
  d.connection_count = 1;
  edges[0] = (cudaq_edem_diagram_connection){{0, 0}, {0, 1}};
  d.input_count = d.output_count = 1;
  reject(&d); // self-loop, remaining local port sides exposed
  d.connection_count = 0;
  d.input_count = d.output_count = 2;
  cudaq_edem_diagram_code_endpoint duplicates[] = {{0, 0}, {0, 0}};
  d.inputs = duplicates;
  d.outputs = duplicates;
  reject(&d); // duplicate external ends
  cudaq_gf2_owned_buffer_release(&model_owner);
}

static void scaling_and_arguments(void) {
  int identity;
  cudaq_edem_diagram_description d = {0};
  d.source_identity = d.registry.source_identity = &identity;
  cudaq_gf2_owned_buffer owner = {0};
  cudaq_edem_diagram_view v = {0};
  TA_CHECK(cudaq_edem_diagram_construct(&d, 0, &owner, &v) == CUDAQ_EDEM_OK);
  TA_CHECK(inspect(v).outgoing_offsets[0] == 0);
  cudaq_gf2_owned_buffer saved = owner;
  cudaq_edem_diagram_view sentinel = v;
  TA_CHECK(cudaq_edem_diagram_construct(&d, 0, &owner, &v) ==
           CUDAQ_EDEM_INVALID_ARGUMENT);
  TA_CHECK(test_same_owner(owner, saved) && v.data == sentinel.data);
  TA_CHECK(cudaq_edem_diagram_construct(&d, 0, 0, &v) ==
           CUDAQ_EDEM_INVALID_ARGUMENT);
  TA_CHECK(cudaq_edem_diagram_construct(&d, 0, &owner, 0) ==
           CUDAQ_EDEM_INVALID_ARGUMENT);
  TA_CHECK(cudaq_edem_diagram_validate(0, 0) == CUDAQ_EDEM_INVALID_ARGUMENT);
  cudaq_edem_diagram_info info = inspect(v), saved_info = info;
  TA_CHECK(cudaq_edem_diagram_inspect(0, &info) == CUDAQ_EDEM_INVALID_ARGUMENT);
  TA_CHECK(memcmp(&info, &saved_info, sizeof(info)) == 0);
  cudaq_edem_diagram_view empty = {0};
  TA_CHECK(cudaq_edem_diagram_inspect(&empty, &info) ==
           CUDAQ_EDEM_INVALID_ARGUMENT);
  TA_CHECK(cudaq_edem_diagram_inspect(&v, 0) == CUDAQ_EDEM_INVALID_ARGUMENT);
  cudaq_gf2_owned_buffer_release(&owner);
  d.occurrence_count = 1;
  TA_CHECK(cudaq_edem_diagram_validate(&d, 0) == CUDAQ_EDEM_INVALID_ARGUMENT);
  d.occurrence_count = 0;
  d.source_identity = 0;
  TA_CHECK(cudaq_edem_diagram_validate(&d, 0) == CUDAQ_EDEM_INVALID_ARGUMENT);
  d.source_identity = &identity;
  cudaq_gf2_memory invalid_memory = {0};
  TA_CHECK(cudaq_edem_diagram_validate(&d, &invalid_memory) ==
           CUDAQ_EDEM_INVALID_ARGUMENT);
  failure_sites(&d);

  // A long open sparse chain: storage grows with incidences, never N*N or the
  // boundary-output by input grid. The model is shared at every occurrence.
  fixture f;
  init(&f);
  enum { N = 10000 };
  cudaq_edem_diagram_occurrence *occurrences = calloc(N, sizeof(*occurrences));
  cudaq_edem_diagram_connection *edges = calloc(N - 1, sizeof(*edges));
  TA_CHECK(occurrences && edges);
  for (uint32_t i = 0; i < N; ++i) {
    occurrences[i].definition = SE;
    if (i + 1 < N)
      edges[i] = (cudaq_edem_diagram_connection){{i, 0}, {i + 1, 0}};
  }
  cudaq_edem_diagram_code_endpoint in = {0, 0}, out = {N - 1, 0};
  f.d.occurrence_count = N;
  f.d.occurrences = occurrences;
  f.d.connection_count = N - 1;
  f.d.connections = edges;
  f.d.input_count = f.d.output_count = 1;
  f.d.inputs = &in;
  f.d.outputs = &out;
  test_resource scratch, result;
  test_resource_init(&scratch);
  test_resource_init(&result);
  cudaq_gf2_memory memory = test_memory(&scratch, &result);
  TA_CHECK(cudaq_edem_diagram_construct(&f.d, &memory, &owner, &v) ==
           CUDAQ_EDEM_OK);
  TA_CHECK(owner.size < 320ull * N && scratch.largest_request < 400ull * N);
  info = inspect(v);
  TA_CHECK(info.boundary_count == 6ull * N &&
           info.topological_order[N - 1] == N - 1);
  cudaq_gf2_owned_buffer_release(&owner);
  test_resource_balanced(&scratch);
  test_resource_balanced(&result);
  free(occurrences);
  free(edges);
  release(&f);
}

int main(void) {
  cnot_experiment();
  bounded_ownership();
  invalid();
  open_reuse_and_replacement();
  zero_cycles_and_mapping();
  scaling_and_arguments();
  return 0;
}
