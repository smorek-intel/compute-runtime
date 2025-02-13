/*
 * Copyright (C) 2020-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/debug_settings/debug_settings_manager.h"
#include "shared/source/helpers/api_specific_config.h"

namespace NEO {
bool ApiSpecificConfig::isStatelessCompressionSupported() {
    return false;
}

bool ApiSpecificConfig::getHeapConfiguration() {
    return DebugManager.flags.UseExternalAllocatorForSshAndDsh.get();
}

bool ApiSpecificConfig::getBindlessConfiguration() {
    if (DebugManager.flags.UseBindlessMode.get() != -1) {
        return DebugManager.flags.UseBindlessMode.get();
    } else {
        return false;
    }
}

ApiSpecificConfig::ApiType ApiSpecificConfig::getApiType() {
    return ApiSpecificConfig::L0;
}

const char *ApiSpecificConfig::getAubPrefixForSpecificApi() {
    return "l0_";
}

uint64_t ApiSpecificConfig::getReducedMaxAllocSize(uint64_t maxAllocSize) {
    return maxAllocSize;
}

} // namespace NEO
