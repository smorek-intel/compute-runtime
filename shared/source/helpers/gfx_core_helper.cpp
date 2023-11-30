/*
 * Copyright (C) 2018-2023 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/helpers/gfx_core_helper.h"

#include "shared/source/debug_settings/debug_settings_manager.h"
#include "shared/source/helpers/constants.h"
#include "shared/source/helpers/hw_info.h"
#include "shared/source/os_interface/product_helper.h"

#include <algorithm>

namespace NEO {

GfxCoreHelperCreateFunctionType gfxCoreHelperFactory[IGFX_MAX_CORE] = {};

const char *deviceHierarchyComposite = "COMPOSITE";
const char *deviceHierarchyFlat = "FLAT";
const char *deviceHierarchyUnk = "UNK";

std::unique_ptr<GfxCoreHelper> GfxCoreHelper::create(const GFXCORE_FAMILY gfxCoreFamily) {

    auto createFunction = gfxCoreHelperFactory[gfxCoreFamily];
    if (createFunction == nullptr) {
        return nullptr;
    }
    auto gfxCoreHelper = createFunction();
    return gfxCoreHelper;
}

bool GfxCoreHelper::compressedBuffersSupported(const HardwareInfo &hwInfo) {
    if (debugManager.flags.RenderCompressedBuffersEnabled.get() != -1) {
        return !!debugManager.flags.RenderCompressedBuffersEnabled.get();
    }
    return hwInfo.capabilityTable.ftrRenderCompressedBuffers;
}

bool GfxCoreHelper::compressedImagesSupported(const HardwareInfo &hwInfo) {
    if (debugManager.flags.RenderCompressedImagesEnabled.get() != -1) {
        return !!debugManager.flags.RenderCompressedImagesEnabled.get();
    }
    return hwInfo.capabilityTable.ftrRenderCompressedImages;
}

bool GfxCoreHelper::cacheFlushAfterWalkerSupported(const HardwareInfo &hwInfo) {
    int32_t dbgFlag = debugManager.flags.EnableCacheFlushAfterWalker.get();
    if (dbgFlag == 1) {
        return true;
    } else if (dbgFlag == 0) {
        return false;
    }
    return hwInfo.capabilityTable.supportCacheFlushAfterWalker;
}

uint32_t GfxCoreHelper::getMaxThreadsForVfe(const HardwareInfo &hwInfo) {
    uint32_t threadsPerEU = (hwInfo.gtSystemInfo.ThreadCount / hwInfo.gtSystemInfo.EUCount) + hwInfo.capabilityTable.extraQuantityThreadsPerEU;
    auto maxHwThreadsCapable = hwInfo.gtSystemInfo.EUCount * threadsPerEU;
    auto maxHwThreadsReturned = maxHwThreadsCapable;
    if (debugManager.flags.MaxHwThreadsPercent.get() != 0) {
        maxHwThreadsReturned = int(maxHwThreadsCapable * (debugManager.flags.MaxHwThreadsPercent.get() / 100.0f));
    }
    if (debugManager.flags.MinHwThreadsUnoccupied.get() != 0) {
        maxHwThreadsReturned = std::min(maxHwThreadsReturned, maxHwThreadsCapable - debugManager.flags.MinHwThreadsUnoccupied.get());
    }
    return maxHwThreadsReturned;
}

uint32_t GfxCoreHelper::getSubDevicesCount(const HardwareInfo *pHwInfo) {
    if (debugManager.flags.CreateMultipleSubDevices.get() > 0) {
        return debugManager.flags.CreateMultipleSubDevices.get();
    } else if (pHwInfo->gtSystemInfo.MultiTileArchInfo.IsValid && pHwInfo->gtSystemInfo.MultiTileArchInfo.TileCount > 0u) {
        return pHwInfo->gtSystemInfo.MultiTileArchInfo.TileCount;
    } else {
        return 1u;
    }
}

uint32_t GfxCoreHelper::getHighestEnabledSlice(const HardwareInfo &hwInfo) {
    uint32_t highestEnabledSlice = 1;
    if (!hwInfo.gtSystemInfo.IsDynamicallyPopulated) {
        return hwInfo.gtSystemInfo.MaxSlicesSupported;
    }
    for (int highestSlice = GT_MAX_SLICE - 1; highestSlice >= 0; highestSlice--) {
        if (hwInfo.gtSystemInfo.SliceInfo[highestSlice].Enabled) {
            highestEnabledSlice = highestSlice + 1;
            break;
        }
    }
    return highestEnabledSlice;
}

uint32_t GfxCoreHelper::getHighestEnabledDualSubSlice(const HardwareInfo &hwInfo) {
    uint32_t highestDualSubSlice = hwInfo.gtSystemInfo.MaxDualSubSlicesSupported;

    if (!hwInfo.gtSystemInfo.IsDynamicallyPopulated) {
        return highestDualSubSlice;
    }

    uint32_t numDssPerSlice = highestDualSubSlice / hwInfo.gtSystemInfo.MaxSlicesSupported;
    uint32_t highestEnabledSliceIdx = getHighestEnabledSlice(hwInfo) - 1;

    for (uint32_t dssID = 0; dssID < GT_MAX_DUALSUBSLICE_PER_SLICE; dssID++) {
        if (hwInfo.gtSystemInfo.SliceInfo[highestEnabledSliceIdx].DSSInfo[dssID].Enabled) {
            highestDualSubSlice = std::max(highestDualSubSlice, (highestEnabledSliceIdx * numDssPerSlice) + dssID + 1);
        }
    }

    return highestDualSubSlice;
}

bool GfxCoreHelper::isWorkaroundRequired(uint32_t lowestSteppingWithBug, uint32_t steppingWithFix, const HardwareInfo &hwInfo, const ProductHelper &productHelper) {
    auto lowestHwRevIdWithBug = productHelper.getHwRevIdFromStepping(lowestSteppingWithBug, hwInfo);
    auto hwRevIdWithFix = productHelper.getHwRevIdFromStepping(steppingWithFix, hwInfo);
    if ((lowestHwRevIdWithBug == CommonConstants::invalidStepping) || (hwRevIdWithFix == CommonConstants::invalidStepping)) {
        return false;
    }
    return (lowestHwRevIdWithBug <= hwInfo.platform.usRevId && hwInfo.platform.usRevId < hwRevIdWithFix);
}

} // namespace NEO
