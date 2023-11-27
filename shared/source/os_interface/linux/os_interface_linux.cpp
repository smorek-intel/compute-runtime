/*
 * Copyright (C) 2020-2023 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/debug_settings/debug_settings_manager.h"
#include "shared/source/execution_environment/root_device_environment.h"
#include "shared/source/os_interface/linux/drm_memory_operations_handler.h"
#include "shared/source/os_interface/linux/drm_neo.h"
#include "shared/source/os_interface/linux/hw_device_id.h"
#include "shared/source/os_interface/product_helper.h"

#include <sys/stat.h>
#include <system_error>
#include <unistd.h>

namespace NEO {

bool OSInterface::osEnabled64kbPages = false;
bool OSInterface::newResourceImplicitFlush = true;
bool OSInterface::gpuIdleImplicitFlush = true;
bool OSInterface::requiresSupportForWddmTrimNotification = false;

bool OSInterface::isDebugAttachAvailable() const {
    if (driverModel && driverModel->getDriverModelType() == DriverModelType::DRM) {
        return driverModel->as<Drm>()->isDebugAttachAvailable();
    }
    return false;
}

bool OSInterface::isLockablePointer(bool isLockable) const {
    return true;
}

bool initDrmOsInterface(std::unique_ptr<HwDeviceId> &&hwDeviceId, uint32_t rootDeviceIndex,
                        RootDeviceEnvironment *rootDeviceEnv) {
    auto hwDeviceIdDrm = std::unique_ptr<HwDeviceIdDrm>(reinterpret_cast<HwDeviceIdDrm *>(hwDeviceId.release()));

    Drm *drm = Drm::create(std::move(hwDeviceIdDrm), *rootDeviceEnv);
    if (!drm) {
        return false;
    }

    auto &dstOsInterface = rootDeviceEnv->osInterface;
    dstOsInterface.reset(new OSInterface());
    dstOsInterface->setDriverModel(std::unique_ptr<DriverModel>(drm));
    auto hardwareInfo = rootDeviceEnv->getMutableHardwareInfo();
    auto &productHelper = rootDeviceEnv->getHelper<ProductHelper>();
    if (productHelper.configureHwInfoDrm(hardwareInfo, hardwareInfo, *rootDeviceEnv)) {
        return false;
    }

    const bool isCsrHwWithAub = DebugManager.flags.SetCommandStreamReceiver.get() == CommandStreamReceiverType::CSR_HW_WITH_AUB;
    rootDeviceEnv->memoryOperationsInterface = DrmMemoryOperationsHandler::create(*drm, rootDeviceIndex, isCsrHwWithAub);

    [[maybe_unused]] bool result = rootDeviceEnv->initAilConfiguration();
    DEBUG_BREAK_IF(!result);

    return true;
}

} // namespace NEO
