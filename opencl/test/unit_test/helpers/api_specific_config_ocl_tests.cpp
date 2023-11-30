/*
 * Copyright (C) 2021-2023 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/helpers/api_specific_config.h"
#include "shared/source/memory_manager/allocation_properties.h"
#include "shared/source/memory_manager/compression_selector.h"
#include "shared/test/common/helpers/debug_manager_state_restore.h"
#include "shared/test/common/helpers/default_hw_info.h"

#include "opencl/source/os_interface/ocl_reg_path.h"

#include "gtest/gtest.h"

namespace NEO {

TEST(ApiSpecificConfigOclTests, WhenGettingApiTypeThenCorrectTypeIsReturned) {
    EXPECT_EQ(ApiSpecificConfig::OCL, ApiSpecificConfig::getApiType());
}

TEST(ApiSpecificConfigOclTests, WhenGettingAUBPrefixByApiTypeOCLIsReturned) {
    EXPECT_EQ(0, strcmp("ocl_", ApiSpecificConfig::getAubPrefixForSpecificApi().c_str()));
}

TEST(ApiSpecificConfigOclTests, WhenGettingNameOCLIsReturned) {
    EXPECT_EQ(0, strcmp("ocl", ApiSpecificConfig::getName().c_str()));
}

TEST(ApiSpecificConfigOclTests, WhenCheckingIfStatelessCompressionIsSupportedThenReturnTrue) {
    EXPECT_TRUE(ApiSpecificConfig::isStatelessCompressionSupported());
}

TEST(ApiSpecificConfigOclTests, givenMaxAllocSizeWhenGettingReducedMaxAllocSizeThenReturnHalfOfThat) {
    EXPECT_EQ(512u, ApiSpecificConfig::getReducedMaxAllocSize(1024));
}

TEST(ApiSpecificConfigOclTests, WhenGettingRegistryPathThenOclRegistryPathIsReturned) {
    EXPECT_STREQ(oclRegPath, ApiSpecificConfig::getRegistryPath());
}

TEST(ApiSpecificConfigOclTests, WhenCheckingIfDeviceAllocationCacheIsEnabledThenReturnFalse) {
    EXPECT_FALSE(ApiSpecificConfig::isDeviceAllocationCacheEnabled());
}

TEST(ApiSpecificConfigOclTests, WhenCheckingIfDynamicPostSyncAllocLayoutEnabledThenReturnFalse) {
    DebugManagerStateRestore restore;

    EXPECT_FALSE(ApiSpecificConfig::isDynamicPostSyncAllocLayoutEnabled());

    debugManager.flags.EnableDynamicPostSyncAllocLayout.set(1);

    EXPECT_FALSE(ApiSpecificConfig::isDynamicPostSyncAllocLayoutEnabled());
}

TEST(ApiSpecificConfigOclTests, givenEnableStatelessCompressionWhenProvidingSvmGpuAllocationThenPreferCompressedBuffer) {
    DebugManagerStateRestore dbgRestorer;
    debugManager.flags.RenderCompressedBuffersEnabled.set(1);
    debugManager.flags.EnableStatelessCompression.set(1);

    DeviceBitfield deviceBitfield{0x0};
    AllocationProperties properties(0, MemoryConstants::pageSize,
                                    AllocationType::SVM_GPU,
                                    deviceBitfield);

    EXPECT_TRUE(NEO::CompressionSelector::preferCompressedAllocation(properties));
}

TEST(ApiSpecificConfigOclTests, givenEnableStatelessCompressionWhenProvidingPrintfSurfaceThenPreferCompressedBuffer) {
    DebugManagerStateRestore dbgRestorer;
    debugManager.flags.RenderCompressedBuffersEnabled.set(1);
    debugManager.flags.EnableStatelessCompression.set(1);

    DeviceBitfield deviceBitfield{0x0};
    AllocationProperties properties(0, MemoryConstants::pageSize,
                                    AllocationType::PRINTF_SURFACE,
                                    deviceBitfield);

    EXPECT_TRUE(NEO::CompressionSelector::preferCompressedAllocation(properties));
}
} // namespace NEO
