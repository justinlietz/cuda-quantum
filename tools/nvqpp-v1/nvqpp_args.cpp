/****************************************************************-*- C++ -*-****
 * Copyright (c) 2022 - 2023 NVIDIA Corporation & Affiliates.                  *
 * All rights reserved.                                                        *
 *                                                                             *
 * This source code and the accompanying materials are made available under    *
 * the terms of the Apache License 2.0 which accompanies this distribution.    *
 ******************************************************************************/
#include "nvqpp_args.h"
namespace {
// FIXME: these implementations should be located elsewhere (e.g., near the
// ServerHelper impl), not here.
struct IonQTargetPlatformArgs : public cudaq::TargetPlatformArgs {
  virtual cudaq::TargetPlatformArgs::Data
  parsePlatformArgs(ArgvStorageBase &args) override {
    std::string platformExtraArgs;
    // Note: erase args within the loop
    for (auto it = args.begin(); it != args.end(); ++it) {
      auto arg = llvm::StringRef(*it);
      if (arg.equals("--ionq-machine")) {
        platformExtraArgs += std::string(";qpu;");
        platformExtraArgs += std::string(*std::next(it));
        it = args.erase(it, std::next(it, 2));
      }

      if (arg.equals("--ionq-noise-model")) {
        platformExtraArgs += std::string(";noise;");
        platformExtraArgs += std::string(*std::next(it));
        it = args.erase(it, std::next(it, 2));
      }
    }

    return cudaq::TargetPlatformArgs::Data{.genTargetBackend = true,
                                           .linkFlags = {"-lcudaq-rest-qpu"},
                                           .libraryMode = false,
                                           .platformExtraArgs =
                                               platformExtraArgs};
  }
};
} // namespace
namespace cudaq {
std::shared_ptr<TargetPlatformArgs>
getTargetPlatformArgs(const std::string &targetName) {
  static std::unordered_map<std::string, std::shared_ptr<TargetPlatformArgs>>
      TARGET_ARGS_HANDLERS = {
          {"ionq", std::make_shared<IonQTargetPlatformArgs>()}};
  return TARGET_ARGS_HANDLERS[targetName];
}
} // namespace cudaq