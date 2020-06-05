/*
 * Copyright (C) 2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/command_container/command_encoder.h"

#include "opencl/test/unit_test/fixtures/cl_device_fixture.h"
#include "opencl/test/unit_test/helpers/unit_test_helper.h"
#include "test.h"

using namespace NEO;

class CommandEncoderTests : public ClDeviceFixture,
                            public ::testing::Test {

  public:
    void SetUp() override {
        ::testing::Test::SetUp();
        ClDeviceFixture::SetUp();
    }
    void TearDown() override {
        ClDeviceFixture::TearDown();
        ::testing::Test::TearDown();
    }
};

HWTEST_F(CommandEncoderTests, givenImmDataWriteWhenProgrammingMiFlushDwThenSetAllRequiredFields) {
    using MI_FLUSH_DW = typename FamilyType::MI_FLUSH_DW;
    uint8_t buffer[2 * sizeof(MI_FLUSH_DW)] = {};
    LinearStream linearStream(buffer, sizeof(buffer));

    uint64_t gpuAddress = 0x1230000;
    uint64_t immData = 456;

    EncodeMiFlushDW<FamilyType>::programMiFlushDw(linearStream, gpuAddress, immData, false, true);
    auto miFlushDwCmd = reinterpret_cast<MI_FLUSH_DW *>(buffer);

    unsigned int sizeMultiplier = 1;
    if (UnitTestHelper<FamilyType>::additionalMiFlushDwRequired) {
        sizeMultiplier = 2;
        uint64_t gpuAddress = 0x0;
        uint64_t immData = 0;

        EXPECT_EQ(MI_FLUSH_DW::POST_SYNC_OPERATION_NO_WRITE, miFlushDwCmd->getPostSyncOperation());
        EXPECT_EQ(gpuAddress, miFlushDwCmd->getDestinationAddress());
        EXPECT_EQ(immData, miFlushDwCmd->getImmediateData());
        miFlushDwCmd++;
    }

    EXPECT_EQ(sizeMultiplier * sizeof(MI_FLUSH_DW), linearStream.getUsed());
    EXPECT_EQ(MI_FLUSH_DW::POST_SYNC_OPERATION_WRITE_IMMEDIATE_DATA_QWORD, miFlushDwCmd->getPostSyncOperation());
    EXPECT_EQ(gpuAddress, miFlushDwCmd->getDestinationAddress());
    EXPECT_EQ(immData, miFlushDwCmd->getImmediateData());
}

HWTEST_F(CommandEncoderTests, whenEncodeMemoryPrefetchCalledThenDoNothing) {
    uint8_t buffer[MemoryConstants::pageSize] = {};
    LinearStream linearStream(buffer, sizeof(buffer));

    GraphicsAllocation allocation(0, GraphicsAllocation::AllocationType::UNKNOWN, nullptr, 123, 456, 789, MemoryPool::LocalMemory);

    EncodeMemoryPrefetch<FamilyType>::programMemoryPrefetch(linearStream, allocation, 2);

    EXPECT_EQ(0u, linearStream.getUsed());
    EXPECT_EQ(0u, EncodeMemoryPrefetch<FamilyType>::getSizeForMemoryPrefetch());
}

HWCMDTEST_F(IGFX_GEN8_CORE, CommandEncoderTests, WhenAnyParameterIsProvidedThenRuntimeGenerationLocalIdsIsRequired) {
    uint32_t workDim = 1;
    uint32_t simd = 8;
    size_t lws[3] = {16, 1, 1};
    std::array<uint8_t, 3> walkOrder = {};
    uint32_t requiredWalkOrder = 0u;

    EXPECT_TRUE(EncodeDispatchKernel<FamilyType>::isRuntimeLocalIdsGenerationRequired(
        workDim, lws, walkOrder, true, requiredWalkOrder, simd));
}
