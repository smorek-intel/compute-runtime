/*
 * Copyright (C) 2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "hw_cmds.h"

namespace NEO {
struct XeHpFamily;
using Family = XeHpFamily;
} // namespace NEO

#include "shared/source/helpers/hw_helper.h"
#include "shared/source/helpers/preamble_xehp_and_later.inl"
namespace NEO {

template <>
void PreambleHelper<Family>::appendProgramVFEState(const HardwareInfo &hwInfo, const StreamProperties &streamProperties, void *cmd) {
    auto command = static_cast<typename Family::CFE_STATE *>(cmd);

    command->setComputeOverdispatchDisable(streamProperties.frontEndState.disableOverdispatch.value == 1);
    command->setSingleSliceDispatchCcsMode(streamProperties.frontEndState.singleSliceDispatchCcsMode.value);

    if (DebugManager.flags.CFEComputeOverdispatchDisable.get() != -1) {
        command->setComputeOverdispatchDisable(DebugManager.flags.CFEComputeOverdispatchDisable.get());
    }

    if (DebugManager.flags.CFEWeightedDispatchModeDisable.get() != -1) {
        command->setWeightedDispatchModeDisable(DebugManager.flags.CFEWeightedDispatchModeDisable.get());
    }

    if (DebugManager.flags.CFESingleSliceDispatchCCSMode.get() != -1) {
        command->setSingleSliceDispatchCcsMode(DebugManager.flags.CFESingleSliceDispatchCCSMode.get());
    }
}

template struct PreambleHelper<Family>;

} // namespace NEO
