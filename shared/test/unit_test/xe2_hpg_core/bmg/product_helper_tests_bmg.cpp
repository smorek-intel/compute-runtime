/*
 * Copyright (C) 2024 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/command_stream/stream_properties.h"
#include "shared/source/helpers/compiler_product_helper.h"
#include "shared/source/os_interface/product_helper.h"
#include "shared/source/xe2_hpg_core/hw_cmds_bmg.h"
#include "shared/source/xe2_hpg_core/hw_info_xe2_hpg_core.h"
#include "shared/test/common/helpers/default_hw_info.h"
#include "shared/test/common/test_macros/header/per_product_test_definitions.h"
#include "shared/test/common/test_macros/test.h"
#include "shared/test/unit_test/os_interface/product_helper_tests.h"

#include "aubstream/product_family.h"
#include "platforms.h"

using namespace NEO;

using BmgProductHelper = ProductHelperTest;

BMGTEST_F(BmgProductHelper, whenGettingAubstreamProductFamilyThenProperEnumValueIsReturned) {
    EXPECT_EQ(aub_stream::ProductFamily::Bmg, productHelper->getAubStreamProductFamily());
}

BMGTEST_F(BmgProductHelper, givenProductHelperWhenGettingEvictIfNecessaryFlagSupportedThenExpectTrue) {

    EXPECT_TRUE(productHelper->isEvictionIfNecessaryFlagSupported());
}

BMGTEST_F(BmgProductHelper, whenGettingMidThreadPreemptionSupportForRtKernelsThenExpectNoSupport) {

    EXPECT_TRUE(productHelper->isMidThreadPreemptionDisallowedForRayTracingKernels());
}

BMGTEST_F(BmgProductHelper, givenBmgProductHelperWhenIsInitBuiltinAsyncSupportedThenReturnFalse) {
    EXPECT_FALSE(productHelper->isInitBuiltinAsyncSupported(*defaultHwInfo));
}

BMGTEST_F(BmgProductHelper, givenProductHelperWhenCheckIsCopyBufferRectSplitSupportedThenReturnsTrue) {
    EXPECT_TRUE(productHelper->isCopyBufferRectSplitSupported());
}

BMGTEST_F(BmgProductHelper, givenProductHelperWhenGetCommandsStreamPropertiesSupportThenExpectCorrectValues) {

    EXPECT_TRUE(productHelper->getScmPropertyThreadArbitrationPolicySupport());
    EXPECT_TRUE(productHelper->getScmPropertyCoherencyRequiredSupport());
    EXPECT_FALSE(productHelper->getScmPropertyZPassAsyncComputeThreadLimitSupport());
    EXPECT_FALSE(productHelper->getScmPropertyPixelAsyncComputeThreadLimitSupport());
    EXPECT_TRUE(productHelper->getScmPropertyLargeGrfModeSupport());
    EXPECT_FALSE(productHelper->getScmPropertyDevicePreemptionModeSupport());

    EXPECT_TRUE(productHelper->getStateBaseAddressPropertyBindingTablePoolBaseAddressSupport());

    EXPECT_TRUE(productHelper->getFrontEndPropertyScratchSizeSupport());
    EXPECT_TRUE(productHelper->getFrontEndPropertyPrivateScratchSizeSupport());

    EXPECT_FALSE(productHelper->getPreemptionDbgPropertyPreemptionModeSupport());
    EXPECT_TRUE(productHelper->getPreemptionDbgPropertyStateSipSupport());
    EXPECT_TRUE(productHelper->getPreemptionDbgPropertyCsrSurfaceSupport());

    EXPECT_FALSE(productHelper->getFrontEndPropertyComputeDispatchAllWalkerSupport());
    EXPECT_FALSE(productHelper->getFrontEndPropertyDisableEuFusionSupport());
    EXPECT_TRUE(productHelper->getFrontEndPropertyDisableOverDispatchSupport());
    EXPECT_TRUE(productHelper->getFrontEndPropertySingleSliceDispatchCcsModeSupport());

    EXPECT_FALSE(productHelper->getPipelineSelectPropertyMediaSamplerDopClockGateSupport());
    EXPECT_FALSE(productHelper->getPipelineSelectPropertySystolicModeSupport());
}

BMGTEST_F(BmgProductHelper, WhenFillingScmPropertiesSupportThenExpectUseCorrectExtraGetters) {
    StateComputeModePropertiesSupport scmPropertiesSupport = {};

    productHelper->fillScmPropertiesSupportStructure(scmPropertiesSupport);

    EXPECT_EQ(true, scmPropertiesSupport.allocationForScratchAndMidthreadPreemption);
}

BMGTEST_F(BmgProductHelper, givenProductHelperWhenAdditionalKernelExecInfoSupportCheckedThenCorrectValueIsReturned) {

    EXPECT_TRUE(productHelper->isDisableOverdispatchAvailable(*defaultHwInfo));

    FrontEndPropertiesSupport fePropertiesSupport{};
    productHelper->fillFrontEndPropertiesSupportStructure(fePropertiesSupport, *defaultHwInfo);
    EXPECT_TRUE(fePropertiesSupport.disableOverdispatch);
}

BMGTEST_F(BmgProductHelper, givenCompilerProductHelperWhenGetDefaultHwIpVersionThenCorrectValueIsSet) {
    EXPECT_EQ(compilerProductHelper->getDefaultHwIpVersion(), AOT::BMG_G21_B0);
}

BMGTEST_F(BmgProductHelper, givenProductHelperWhenCheckingIsBufferPoolAllocatorSupportedThenCorrectValueIsReturned) {
    EXPECT_TRUE(productHelper->isBufferPoolAllocatorSupported());
}

BMGTEST_F(BmgProductHelper, givenProductHelperWhenAdjustNumberOfCcsThenOverrideToSingleCcs) {
    auto hwInfo = *defaultHwInfo;
    hwInfo.gtSystemInfo.CCSInfo.NumberOfCCSEnabled = 223;
    productHelper->adjustNumberOfCcs(hwInfo);
    EXPECT_EQ(hwInfo.gtSystemInfo.CCSInfo.NumberOfCCSEnabled, 1u);
}

BMGTEST_F(BmgProductHelper, givenProductHelperWhenGettingThreadEuRatioForScratchThen16IsReturned) {
    auto hwInfo = *defaultHwInfo;
    EXPECT_EQ(16u, productHelper->getThreadEuRatioForScratch(hwInfo));
}
