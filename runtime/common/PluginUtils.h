/****************************************************************-*- C++ -*-****
 * Copyright (c) 2022 - 2025 NVIDIA Corporation & Affiliates.                  *
 * All rights reserved.                                                        *
 *                                                                             *
 * This source code and the accompanying materials are made available under    *
 * the terms of the Apache License 2.0 which accompanies this distribution.    *
 ******************************************************************************/

#pragma once
#include "Logger.h"
#include <dlfcn.h>
#include <mutex>
#include <string_view>

namespace cudaq {
/// @brief Extract the Plugin generator function with the provided symbol,
/// execute the function and return the plugin pointer.
/// @tparam PluginPointerType The Plugin type pointer
/// @param symbolName The name of the generator function
/// @return The plugin instance
template <typename PluginPointerType>
PluginPointerType *getUniquePluginInstance(const std::string_view symbolName,
                                           const char *libName = nullptr) {
  cudaq::info("Requesting {} plugin via symbol name {}.",
              typeid(PluginPointerType).name(), symbolName);
  std::mutex m;
  std::lock_guard<std::mutex> l(m);
  using GetPluginFunction = PluginPointerType *(*)();
  auto handle = dlopen(libName, RTLD_LAZY);
  GetPluginFunction fcn =
      (GetPluginFunction)(intptr_t)dlsym(handle, symbolName.data());
  if (!fcn)
    throw std::runtime_error(
        fmt::format("Could not load the requested plugin. \n{}\n", dlerror()));

  cudaq::info("Successfully loaded the plugin.");
  return fcn();
}
} // namespace cudaq
