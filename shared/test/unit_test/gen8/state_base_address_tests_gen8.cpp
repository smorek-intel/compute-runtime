/*
 * Copyright (C) 2020-2023 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/command_stream/memory_compression_state.h"
#include "shared/source/gen8/hw_cmds.h"
#include "shared/test/common/test_macros/header/per_product_test_definitions.h"
#include "shared/test/common/test_macros/test.h"
#include "shared/test/unit_test/helpers/state_base_address_tests.h"

BDWTEST_F(SbaTest, givenUsedBindlessBuffersWhenAppendStateBaseAddressParametersIsCalledThenSBACmdHasNotBindingSurfaceStateProgrammed) {
    using STATE_BASE_ADDRESS = typename FamilyType::STATE_BASE_ADDRESS;

    debugManager.flags.UseBindlessMode.set(1);

    STATE_BASE_ADDRESS stateBaseAddress = {};
    STATE_BASE_ADDRESS stateBaseAddressReference = {};

    StateBaseAddressHelperArgs<FamilyType> args = createSbaHelperArgs<FamilyType>(&stateBaseAddress, nullptr, &ssh, nullptr, nullptr);

    StateBaseAddressHelper<FamilyType>::appendStateBaseAddressParameters(args);

    EXPECT_EQ(0u, ssh.getUsed());
    EXPECT_EQ(0, memcmp(&stateBaseAddressReference, &stateBaseAddress, sizeof(STATE_BASE_ADDRESS)));
}

BDWTEST_F(SbaTest,
          givenUsedBindlessBuffersAndOverridenSurfaceStateBaseAddressWhenAppendStateBaseAddressParametersIsCalledThenSbaCmdHasCorrectSurfaceStateBaseAddress) {
    using STATE_BASE_ADDRESS = typename FamilyType::STATE_BASE_ADDRESS;

    constexpr uint64_t surfaceStateBaseAddress = 0xBADA550000;

    STATE_BASE_ADDRESS stateBaseAddressCmd = {};

    StateBaseAddressHelperArgs<FamilyType> args = createSbaHelperArgs<FamilyType>(&stateBaseAddressCmd, nullptr, &ssh, nullptr, nullptr);
    args.surfaceStateBaseAddress = surfaceStateBaseAddress;
    args.overrideSurfaceStateBaseAddress = true;

    StateBaseAddressHelper<FamilyType>::programStateBaseAddress(args);

    EXPECT_TRUE(stateBaseAddressCmd.getSurfaceStateBaseAddressModifyEnable());
    EXPECT_EQ(surfaceStateBaseAddress, stateBaseAddressCmd.getSurfaceStateBaseAddress());
}
