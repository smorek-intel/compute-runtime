/*
 * Copyright (C) 2024 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/release_helper/release_helper.h"
#include "shared/source/release_helper/release_helper_base.inl"

#include "release_definitions.h"

namespace NEO {
constexpr auto release = ReleaseType::release2001;

template <>
bool ReleaseHelperHw<release>::shouldAdjustDepth() const {
    return true;
}

template <>
inline bool ReleaseHelperHw<release>::isAuxSurfaceModeOverrideRequired() const {
    return true;
}

} // namespace NEO

#include "shared/source/release_helper/release_helper_common_xe2_hpg.inl"

template class NEO::ReleaseHelperHw<NEO::release>;
