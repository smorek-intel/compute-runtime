/*
 * Copyright (C) 2021-2023 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/os_interface/driver_info.h"
#include "shared/test/common/mocks/linux/mock_ioctl_helper.h"
#include "shared/test/common/test_macros/hw_test.h"

#include "level_zero/sysman/source/api/memory/linux/sysman_os_memory_imp_prelim.h"
#include "level_zero/sysman/source/shared/linux/pmt/sysman_pmt_xml_offsets.h"
#include "level_zero/sysman/source/shared/linux/sysman_kmd_interface.h"
#include "level_zero/sysman/source/sysman_const.h"
#include "level_zero/sysman/test/unit_tests/sources/linux/mock_sysman_fixture.h"
#include "level_zero/sysman/test/unit_tests/sources/memory/linux/mock_memory_prelim.h"

namespace L0 {
namespace Sysman {
namespace ult {
constexpr int32_t memoryBusWidth = 128; // bus width in bits
constexpr int32_t numMemoryChannels = 8;
constexpr uint32_t memoryHandleComponentCount = 1u;
const std::string sampleGuid1 = "0xb15a0edc";

class SysmanMemoryMockIoctlHelper : public NEO::MockIoctlHelper {

  public:
    using NEO::MockIoctlHelper::MockIoctlHelper;
    bool returnEmptyMemoryInfo = false;
    int32_t mockErrorNumber = 0;

    std::unique_ptr<MemoryInfo> createMemoryInfo() override {
        if (returnEmptyMemoryInfo) {
            errno = mockErrorNumber;
            return {};
        }
        return NEO::MockIoctlHelper::createMemoryInfo();
    }
};

class SysmanDeviceMemoryFixture : public SysmanDeviceFixture {
  protected:
    std::unique_ptr<MockMemorySysfsAccess> pSysfsAccess;
    std::unique_ptr<MockMemoryFsAccess> pFsAccess;
    L0::Sysman::SysFsAccessInterface *pSysfsAccessOld = nullptr;
    L0::Sysman::FsAccessInterface *pFsAccessOriginal = nullptr;
    MockMemoryNeoDrm *pDrm = nullptr;
    Drm *pOriginalDrm = nullptr;
    L0::Sysman::SysmanDevice *device = nullptr;
    DebugManagerStateRestore restorer;
    PRODUCT_FAMILY productFamily;
    uint16_t stepping;
    std::map<uint32_t, L0::Sysman::PlatformMonitoringTech *> pmtMapOriginal;

    void SetUp() override {
        debugManager.flags.EnableLocalMemory.set(1);

        SysmanDeviceFixture::SetUp();

        pSysfsAccessOld = pLinuxSysmanImp->pSysfsAccess;
        pSysfsAccess = std::make_unique<MockMemorySysfsAccess>();
        pLinuxSysmanImp->pSysfsAccess = pSysfsAccess.get();
        pDrm = new MockMemoryNeoDrm(const_cast<NEO::RootDeviceEnvironment &>(pSysmanDeviceImp->getRootDeviceEnvironment()));
        auto &osInterface = pSysmanDeviceImp->getRootDeviceEnvironment().osInterface;
        osInterface->setDriverModel(std::unique_ptr<MockMemoryNeoDrm>(pDrm));

        pFsAccess = std::make_unique<MockMemoryFsAccess>();
        pFsAccessOriginal = pLinuxSysmanImp->pFsAccess;
        pLinuxSysmanImp->pFsAccess = pFsAccess.get();
        pDrm->setMemoryType(INTEL_HWCONFIG_MEMORY_TYPE_HBM2e);
        pDrm->ioctlHelper = static_cast<std::unique_ptr<NEO::IoctlHelper>>(std::make_unique<SysmanMemoryMockIoctlHelper>(*pDrm));

        pSysmanDeviceImp->pMemoryHandleContext->handleList.clear();
        pmtMapOriginal = pLinuxSysmanImp->mapOfSubDeviceIdToPmtObject;
        pLinuxSysmanImp->mapOfSubDeviceIdToPmtObject.clear();
        auto subdeviceId = 0u;
        auto subDeviceCount = pLinuxSysmanImp->getSubDeviceCount();
        do {
            ze_bool_t onSubdevice = subDeviceCount == 0 ? false : true;
            auto pPmt = new MockMemoryPmt(pFsAccess.get(), onSubdevice,
                                          subdeviceId);
            pLinuxSysmanImp->mapOfSubDeviceIdToPmtObject.emplace(subdeviceId, pPmt);
        } while (++subdeviceId < subDeviceCount);

        auto &hwInfo = pLinuxSysmanImp->getSysmanDeviceImp()->getHardwareInfo();
        productFamily = hwInfo.platform.eProductFamily;
        auto &productHelper = pLinuxSysmanImp->getSysmanDeviceImp()->getRootDeviceEnvironment().getHelper<NEO::ProductHelper>();
        stepping = productHelper.getSteppingFromHwRevId(hwInfo);
        device = pSysmanDevice;
        getMemoryHandles(0);
    }

    void TearDown() override {
        pLinuxSysmanImp->releasePmtObject();
        pLinuxSysmanImp->mapOfSubDeviceIdToPmtObject = pmtMapOriginal;
        pLinuxSysmanImp->pFsAccess = pFsAccessOriginal;
        pLinuxSysmanImp->pSysfsAccess = pSysfsAccessOld;
        SysmanDeviceFixture::TearDown();
    }

    void setLocalSupportedAndReinit(bool supported) {
        debugManager.flags.EnableLocalMemory.set(supported == true ? 1 : 0);

        pSysmanDeviceImp->pMemoryHandleContext->handleList.clear();
        pSysmanDeviceImp->pMemoryHandleContext->init(pOsSysman->getSubDeviceCount());
    }

    std::vector<zes_mem_handle_t> getMemoryHandles(uint32_t count) {
        std::vector<zes_mem_handle_t> handles(count, nullptr);
        EXPECT_EQ(zesDeviceEnumMemoryModules(device->toHandle(), &count, handles.data()), ZE_RESULT_SUCCESS);
        return handles;
    }
};

TEST_F(SysmanDeviceMemoryFixture, GivenKmdInterfaceWhenGettingSysfsFileNamesForI915VersionThenProperPathsAreReturned) {
    auto pSysmanKmdInterface = std::make_unique<SysmanKmdInterfaceI915>(productFamily);
    EXPECT_STREQ("gt/gt0/addr_range", pSysmanKmdInterface->getSysfsFilePathForPhysicalMemorySize(0).c_str());
    EXPECT_STREQ("gt/gt0/mem_RP0_freq_mhz", pSysmanKmdInterface->getSysfsFilePath(SysfsName::sysfsNameMaxMemoryFrequency, 0, true).c_str());
    EXPECT_STREQ("gt/gt0/mem_RPn_freq_mhz", pSysmanKmdInterface->getSysfsFilePath(SysfsName::sysfsNameMinMemoryFrequency, 0, true).c_str());
}

TEST_F(SysmanDeviceMemoryFixture, GivenKmdInterfaceWhenGettingSysfsFileNamesForXeVersionThenProperPathsAreReturned) {
    auto pSysmanKmdInterface = std::make_unique<SysmanKmdInterfaceXe>(productFamily);
    EXPECT_STREQ("device/tile0/physical_vram_size_bytes", pSysmanKmdInterface->getSysfsFilePathForPhysicalMemorySize(0).c_str());
    EXPECT_STREQ("device/tile0/gt0/freq_vram_rp0", pSysmanKmdInterface->getSysfsFilePath(SysfsName::sysfsNameMaxMemoryFrequency, 0, true).c_str());
    EXPECT_STREQ("device/tile0/gt0/freq_vram_rpn", pSysmanKmdInterface->getSysfsFilePath(SysfsName::sysfsNameMinMemoryFrequency, 0, true).c_str());
}

TEST_F(SysmanDeviceMemoryFixture, GivenComponentCountZeroWhenEnumeratingMemoryModulesWithLocalMemorySupportThenValidCountIsReturned) {
    setLocalSupportedAndReinit(true);

    uint32_t count = 0;
    EXPECT_EQ(zesDeviceEnumMemoryModules(device->toHandle(), &count, nullptr), ZE_RESULT_SUCCESS);
    EXPECT_EQ(count, memoryHandleComponentCount);
}

TEST_F(SysmanDeviceMemoryFixture, GivenInvalidComponentCountWhenEnumeratingMemoryModulesWithLocalMemorySupportThenValidCountIsReturned) {
    setLocalSupportedAndReinit(true);

    uint32_t count = 0;
    EXPECT_EQ(zesDeviceEnumMemoryModules(device->toHandle(), &count, nullptr), ZE_RESULT_SUCCESS);
    EXPECT_EQ(count, memoryHandleComponentCount);

    count = count + 1;
    EXPECT_EQ(zesDeviceEnumMemoryModules(device->toHandle(), &count, nullptr), ZE_RESULT_SUCCESS);
    EXPECT_EQ(count, memoryHandleComponentCount);
}

TEST_F(SysmanDeviceMemoryFixture, GivenComponentCountZeroWhenEnumeratingMemoryModulesWithLocalMemorySupportThenValidHandlesIsReturned) {
    setLocalSupportedAndReinit(true);

    uint32_t count = 0;
    EXPECT_EQ(zesDeviceEnumMemoryModules(device->toHandle(), &count, nullptr), ZE_RESULT_SUCCESS);
    EXPECT_EQ(count, memoryHandleComponentCount);

    std::vector<zes_mem_handle_t> handles(count, nullptr);
    EXPECT_EQ(zesDeviceEnumMemoryModules(device->toHandle(), &count, handles.data()), ZE_RESULT_SUCCESS);
    for (auto handle : handles) {
        EXPECT_NE(handle, nullptr);
    }
}

TEST_F(SysmanDeviceMemoryFixture, GivenComponentCountZeroWhenEnumeratingMemoryModulesWithNoLocalMemorySupportThenZeroCountIsReturned) {
    setLocalSupportedAndReinit(false);

    uint32_t count = 0;
    EXPECT_EQ(zesDeviceEnumMemoryModules(device->toHandle(), &count, nullptr), ZE_RESULT_SUCCESS);
    EXPECT_EQ(count, 0u);
}

TEST_F(SysmanDeviceMemoryFixture, GivenInvalidComponentCountWhenEnumeratingMemoryModulesWithNoLocalMemorySupportThenZeroCountIsReturned) {
    setLocalSupportedAndReinit(false);

    uint32_t count = 0;
    EXPECT_EQ(zesDeviceEnumMemoryModules(device->toHandle(), &count, nullptr), ZE_RESULT_SUCCESS);
    EXPECT_EQ(count, 0u);

    count = count + 1;
    EXPECT_EQ(zesDeviceEnumMemoryModules(device->toHandle(), &count, nullptr), ZE_RESULT_SUCCESS);
    EXPECT_EQ(count, 0u);
}

TEST_F(SysmanDeviceMemoryFixture, GivenComponentCountZeroWhenEnumeratingMemoryModulesWithNoLocalMemorySupportThenValidHandlesAreReturned) {
    setLocalSupportedAndReinit(false);

    uint32_t count = 0;
    EXPECT_EQ(zesDeviceEnumMemoryModules(device->toHandle(), &count, nullptr), ZE_RESULT_SUCCESS);
    EXPECT_EQ(count, 0u);

    std::vector<zes_mem_handle_t> handles(count, nullptr);
    EXPECT_EQ(zesDeviceEnumMemoryModules(device->toHandle(), &count, handles.data()), ZE_RESULT_SUCCESS);
    for (auto handle : handles) {
        EXPECT_NE(handle, nullptr);
    }
}

TEST_F(SysmanDeviceMemoryFixture, GivenValidMemoryHandleWhenCallingZetSysmanMemoryGetPropertiesWithLocalMemoryThenVerifySysmanMemoryGetPropertiesCallSucceeds) {
    setLocalSupportedAndReinit(true);

    auto handles = getMemoryHandles(memoryHandleComponentCount);

    for (auto handle : handles) {
        ASSERT_NE(nullptr, handle);
        zes_mem_properties_t properties;

        ze_result_t result = zesMemoryGetProperties(handle, &properties);

        EXPECT_EQ(result, ZE_RESULT_SUCCESS);
        EXPECT_EQ(properties.type, ZES_MEM_TYPE_HBM);

        EXPECT_EQ(properties.location, ZES_MEM_LOC_DEVICE);
        EXPECT_FALSE(properties.onSubdevice);
        EXPECT_EQ(properties.subdeviceId, 0u);
        EXPECT_EQ(properties.physicalSize, 0u);
        EXPECT_EQ(properties.numChannels, numMemoryChannels);
        EXPECT_EQ(properties.busWidth, memoryBusWidth);
    }
}

TEST_F(SysmanDeviceMemoryFixture, GivenValidMemoryHandleWhenCallingZetSysmanMemoryGetPropertiesAndQuerySystemInfoFailsThenVerifySysmanMemoryGetPropertiesCallReturnsMemoryTypeAsDdrAndNumberOfChannelsAsUnknown) {
    pDrm->mockQuerySystemInfoReturnValue.push_back(false);
    setLocalSupportedAndReinit(true);

    auto handles = getMemoryHandles(memoryHandleComponentCount);

    for (auto handle : handles) {
        ASSERT_NE(nullptr, handle);
        zes_mem_properties_t properties;

        ze_result_t result = zesMemoryGetProperties(handle, &properties);

        EXPECT_EQ(result, ZE_RESULT_SUCCESS);
        EXPECT_EQ(properties.type, ZES_MEM_TYPE_DDR);
        EXPECT_EQ(properties.location, ZES_MEM_LOC_DEVICE);
        EXPECT_EQ(properties.numChannels, -1);
        EXPECT_FALSE(properties.onSubdevice);
        EXPECT_EQ(properties.subdeviceId, 0u);
        EXPECT_EQ(properties.physicalSize, 0u);
        EXPECT_EQ(properties.busWidth, memoryBusWidth);
    }
}

TEST_F(SysmanDeviceMemoryFixture, GivenValidMemoryHandleWhenCallingZetSysmanMemoryGetPropertiesAndQuerySystemInfoSucceedsButMemSysInfoIsNullThenVerifySysmanMemoryGetPropertiesCallReturnsMemoryTypeAsDdrAndNumberOfChannelsAsUnknown) {
    pDrm->mockQuerySystemInfoReturnValue.push_back(true);
    setLocalSupportedAndReinit(true);

    auto handles = getMemoryHandles(memoryHandleComponentCount);

    for (auto handle : handles) {
        ASSERT_NE(nullptr, handle);
        zes_mem_properties_t properties;

        ze_result_t result = zesMemoryGetProperties(handle, &properties);

        EXPECT_EQ(result, ZE_RESULT_SUCCESS);
        EXPECT_EQ(properties.type, ZES_MEM_TYPE_DDR);
        EXPECT_EQ(properties.location, ZES_MEM_LOC_DEVICE);
        EXPECT_EQ(properties.numChannels, -1);
        EXPECT_FALSE(properties.onSubdevice);
        EXPECT_EQ(properties.subdeviceId, 0u);
        EXPECT_EQ(properties.physicalSize, 0u);
        EXPECT_EQ(properties.busWidth, memoryBusWidth);
    }
}

TEST_F(SysmanDeviceMemoryFixture, GivenValidMemoryHandleWhenCallingZetSysmanMemoryGetPropertiesWithHBMLocalMemoryThenVerifySysmanMemoryGetPropertiesCallSucceeds) {
    pDrm->setMemoryType(INTEL_HWCONFIG_MEMORY_TYPE_HBM2);
    setLocalSupportedAndReinit(true);

    auto handles = getMemoryHandles(memoryHandleComponentCount);

    for (auto handle : handles) {
        ASSERT_NE(nullptr, handle);
        zes_mem_properties_t properties;

        ze_result_t result = zesMemoryGetProperties(handle, &properties);

        EXPECT_EQ(result, ZE_RESULT_SUCCESS);
        EXPECT_EQ(properties.type, ZES_MEM_TYPE_HBM);
        EXPECT_EQ(properties.location, ZES_MEM_LOC_DEVICE);
        EXPECT_FALSE(properties.onSubdevice);
        EXPECT_EQ(properties.subdeviceId, 0u);
        EXPECT_EQ(properties.physicalSize, 0u);
        EXPECT_EQ(properties.numChannels, numMemoryChannels);
        EXPECT_EQ(properties.busWidth, memoryBusWidth);
    }
}

TEST_F(SysmanDeviceMemoryFixture, GivenValidMemoryHandleWhenCallingZetSysmanMemoryGetPropertiesWithLPDDR4LocalMemoryThenVerifySysmanMemoryGetPropertiesCallSucceeds) {
    pDrm->setMemoryType(INTEL_HWCONFIG_MEMORY_TYPE_LPDDR4);
    setLocalSupportedAndReinit(true);

    auto handles = getMemoryHandles(memoryHandleComponentCount);

    for (auto handle : handles) {
        ASSERT_NE(nullptr, handle);
        zes_mem_properties_t properties;

        ze_result_t result = zesMemoryGetProperties(handle, &properties);

        EXPECT_EQ(result, ZE_RESULT_SUCCESS);
        EXPECT_EQ(properties.type, ZES_MEM_TYPE_LPDDR4);
        EXPECT_EQ(properties.location, ZES_MEM_LOC_DEVICE);
        EXPECT_FALSE(properties.onSubdevice);
        EXPECT_EQ(properties.subdeviceId, 0u);
        EXPECT_EQ(properties.physicalSize, 0u);
        EXPECT_EQ(properties.numChannels, numMemoryChannels);
        EXPECT_EQ(properties.busWidth, memoryBusWidth);
    }
}

TEST_F(SysmanDeviceMemoryFixture, GivenValidMemoryHandleWhenCallingZetSysmanMemoryGetPropertiesWithLPDDR5LocalMemoryThenVerifySysmanMemoryGetPropertiesCallSucceeds) {
    pDrm->setMemoryType(INTEL_HWCONFIG_MEMORY_TYPE_LPDDR5);
    setLocalSupportedAndReinit(true);

    auto handles = getMemoryHandles(memoryHandleComponentCount);

    for (auto handle : handles) {
        ASSERT_NE(nullptr, handle);
        zes_mem_properties_t properties;

        ze_result_t result = zesMemoryGetProperties(handle, &properties);

        EXPECT_EQ(result, ZE_RESULT_SUCCESS);
        EXPECT_EQ(properties.type, ZES_MEM_TYPE_LPDDR5);
        EXPECT_EQ(properties.location, ZES_MEM_LOC_DEVICE);
        EXPECT_FALSE(properties.onSubdevice);
        EXPECT_EQ(properties.subdeviceId, 0u);
        EXPECT_EQ(properties.physicalSize, 0u);
        EXPECT_EQ(properties.numChannels, numMemoryChannels);
        EXPECT_EQ(properties.busWidth, memoryBusWidth);
    }
}

TEST_F(SysmanDeviceMemoryFixture, GivenValidMemoryHandleWhenCallingZetSysmanMemoryGetPropertiesWithDDRLocalMemoryThenVerifySysmanMemoryGetPropertiesCallSucceeds) {
    pDrm->setMemoryType(INTEL_HWCONFIG_MEMORY_TYPE_GDDR6);
    setLocalSupportedAndReinit(true);

    auto handles = getMemoryHandles(memoryHandleComponentCount);

    for (auto handle : handles) {
        ASSERT_NE(nullptr, handle);
        zes_mem_properties_t properties;

        ze_result_t result = zesMemoryGetProperties(handle, &properties);

        EXPECT_EQ(result, ZE_RESULT_SUCCESS);
        EXPECT_EQ(properties.type, ZES_MEM_TYPE_DDR);
        EXPECT_EQ(properties.location, ZES_MEM_LOC_DEVICE);
        EXPECT_FALSE(properties.onSubdevice);
        EXPECT_EQ(properties.subdeviceId, 0u);
        EXPECT_EQ(properties.physicalSize, 0u);
        EXPECT_EQ(properties.numChannels, numMemoryChannels);
        EXPECT_EQ(properties.busWidth, memoryBusWidth);
    }
}

TEST_F(SysmanDeviceMemoryFixture, GivenValidMemoryHandleWhenCallingZetSysmanMemoryGetStateThenVerifySysmanMemoryGetStateCallSucceeds) {
    setLocalSupportedAndReinit(true);

    auto handles = getMemoryHandles(memoryHandleComponentCount);

    for (auto handle : handles) {
        ASSERT_NE(nullptr, handle);
        zes_mem_state_t state;

        ze_result_t result = zesMemoryGetState(handle, &state);

        EXPECT_EQ(result, ZE_RESULT_SUCCESS);
        EXPECT_EQ(state.health, ZES_MEM_HEALTH_OK);
        EXPECT_EQ(state.size, NEO::probedSizeRegionOne);
        EXPECT_EQ(state.free, NEO::unallocatedSizeRegionOne);
    }
}

TEST_F(SysmanDeviceMemoryFixture, GivenValidMemoryHandleWhenCallingZetSysmanMemoryGetStateAndIoctlReturnedErrorThenApiReturnsError) {
    setLocalSupportedAndReinit(true);

    auto ioctlHelper = static_cast<SysmanMemoryMockIoctlHelper *>(pDrm->ioctlHelper.get());
    ioctlHelper->returnEmptyMemoryInfo = true;
    auto handles = getMemoryHandles(memoryHandleComponentCount);
    for (auto handle : handles) {
        ASSERT_NE(nullptr, handle);
        zes_mem_state_t state;

        ze_result_t result = zesMemoryGetState(handle, &state);

        EXPECT_EQ(result, ZE_RESULT_ERROR_UNKNOWN);
        EXPECT_EQ(state.size, 0u);
        EXPECT_EQ(state.free, 0u);
    }
}

TEST_F(SysmanDeviceMemoryFixture, GivenValidMemoryHandleWhenCallingZetSysmanMemoryGetStateAndDeviceIsNotAvailableThenDeviceLostErrorIsReturned) {
    setLocalSupportedAndReinit(true);

    auto ioctlHelper = static_cast<SysmanMemoryMockIoctlHelper *>(pDrm->ioctlHelper.get());
    ioctlHelper->returnEmptyMemoryInfo = true;
    ioctlHelper->mockErrorNumber = ENODEV;
    auto handles = getMemoryHandles(memoryHandleComponentCount);
    for (auto handle : handles) {
        ASSERT_NE(nullptr, handle);
        zes_mem_state_t state;

        ze_result_t result = zesMemoryGetState(handle, &state);

        EXPECT_EQ(result, ZE_RESULT_ERROR_DEVICE_LOST);
        EXPECT_EQ(state.size, 0u);
        EXPECT_EQ(state.free, 0u);
        errno = 0;
    }
}

TEST_F(SysmanDeviceMemoryFixture, GivenSysmanResourcesAreReleasedAndReInitializedWhenCallingZesSysmanMemoryGetStateThenVerifySysmanMemoryGetStateCallSucceeds) {
    pLinuxSysmanImp->releaseSysmanDeviceResources();
    EXPECT_EQ(ZE_RESULT_SUCCESS, pLinuxSysmanImp->reInitSysmanDeviceResources());

    VariableBackup<std::map<uint32_t, L0::Sysman::PlatformMonitoringTech *>> pmtBackup(&pLinuxSysmanImp->mapOfSubDeviceIdToPmtObject);
    pLinuxSysmanImp->mapOfSubDeviceIdToPmtObject.clear();
    auto subdeviceId = 0u;
    auto subDeviceCount = pLinuxSysmanImp->getSubDeviceCount();
    do {
        ze_bool_t onSubdevice = subDeviceCount == 0 ? false : true;
        auto pPmt = new MockMemoryPmt(pFsAccess.get(), onSubdevice,
                                      subdeviceId);
        pLinuxSysmanImp->mapOfSubDeviceIdToPmtObject.emplace(subdeviceId, pPmt);
    } while (++subdeviceId < subDeviceCount);

    VariableBackup<L0::Sysman::FirmwareUtil *> backup(&pLinuxSysmanImp->pFwUtilInterface);
    pLinuxSysmanImp->pFwUtilInterface = new MockFwUtilInterface();

    auto handles = getMemoryHandles(memoryHandleComponentCount);

    for (auto handle : handles) {
        ASSERT_NE(nullptr, handle);
        zes_mem_state_t state;

        ze_result_t result = zesMemoryGetState(handle, &state);

        EXPECT_EQ(result, ZE_RESULT_SUCCESS);
        EXPECT_EQ(state.health, ZES_MEM_HEALTH_OK);
        EXPECT_EQ(state.size, NEO::probedSizeRegionOne);
        EXPECT_EQ(state.free, NEO::unallocatedSizeRegionOne);
    }

    pLinuxSysmanImp->releasePmtObject();
    delete pLinuxSysmanImp->pFwUtilInterface;
    pLinuxSysmanImp->pFwUtilInterface = nullptr;
}

TEST_F(SysmanDeviceMemoryFixture, GivenValidMemoryHandleWhenCallingzesSysmanMemoryGetBandwidthWhenPmtObjectIsNullThenFailureRetuned) {
    for (auto &subDeviceIdToPmtEntry : pLinuxSysmanImp->mapOfSubDeviceIdToPmtObject) {
        if (subDeviceIdToPmtEntry.second != nullptr) {
            delete subDeviceIdToPmtEntry.second;
            subDeviceIdToPmtEntry.second = nullptr;
        }
    }
    setLocalSupportedAndReinit(true);
    auto handles = getMemoryHandles(memoryHandleComponentCount);
    for (auto &handle : handles) {
        ASSERT_NE(nullptr, handle);
        zes_mem_bandwidth_t bandwidth;
        EXPECT_EQ(zesMemoryGetBandwidth(handle, &bandwidth), ZE_RESULT_ERROR_UNSUPPORTED_FEATURE);
    }
}

HWTEST2_F(SysmanDeviceMemoryFixture, GivenValidMemoryHandleWhenCallingzesSysmanMemoryGetBandwidthWhenVFID0IsActiveThenSuccessIsReturnedAndBandwidthIsValid, IsPVC) {
    setLocalSupportedAndReinit(true);
    auto handles = getMemoryHandles(memoryHandleComponentCount);

    for (auto &handle : handles) {
        ASSERT_NE(nullptr, handle);
        zes_mem_bandwidth_t bandwidth{};
        uint64_t expectedReadCounters = 0, expectedWriteCounters = 0;
        uint64_t expectedBandwidth = 0;
        zes_mem_properties_t properties = {ZES_STRUCTURE_TYPE_MEM_PROPERTIES};
        zesMemoryGetProperties(handle, &properties);

        auto hwInfo = pSysmanDeviceImp->getRootDeviceEnvironment().getMutableHardwareInfo();
        auto &productHelper = pSysmanDeviceImp->getRootDeviceEnvironment().getHelper<NEO::ProductHelper>();
        hwInfo->platform.usRevId = productHelper.getHwRevIdFromStepping(REVISION_B, *hwInfo);

        auto pPmt = static_cast<MockMemoryPmt *>(pLinuxSysmanImp->getPlatformMonitoringTechAccess(properties.subdeviceId));
        pPmt->setGuid(guid64BitMemoryCounters);
        pPmt->mockVfid0Status = true;
        pSysfsAccess->mockReadUInt64Value.push_back(hbmRP0Frequency);
        pSysfsAccess->mockReadReturnStatus.push_back(ZE_RESULT_SUCCESS);

        EXPECT_EQ(zesMemoryGetBandwidth(handle, &bandwidth), ZE_RESULT_SUCCESS);
        expectedReadCounters |= vF0HbmHRead;
        expectedReadCounters = (expectedReadCounters << 32) | vF0HbmLRead;
        expectedReadCounters = expectedReadCounters * transactionSize;
        EXPECT_EQ(bandwidth.readCounter, expectedReadCounters);
        expectedWriteCounters |= vF0HbmHWrite;
        expectedWriteCounters = (expectedWriteCounters << 32) | vF0HbmLWrite;
        expectedWriteCounters = expectedWriteCounters * transactionSize;
        EXPECT_EQ(bandwidth.writeCounter, expectedWriteCounters);
        expectedBandwidth = 128 * hbmRP0Frequency * 1000 * 1000 * 4;
        EXPECT_EQ(bandwidth.maxBandwidth, expectedBandwidth);
    }
}

HWTEST2_F(SysmanDeviceMemoryFixture, GivenValidMemoryHandleWhenCallingzesSysmanMemoryGetBandwidthWhenVFID1IsActiveThenSuccessIsReturnedAndBandwidthIsValid, IsPVC) {
    setLocalSupportedAndReinit(true);
    auto handles = getMemoryHandles(memoryHandleComponentCount);

    for (auto &handle : handles) {
        ASSERT_NE(nullptr, handle);
        zes_mem_bandwidth_t bandwidth{};
        uint64_t expectedReadCounters = 0, expectedWriteCounters = 0;
        uint64_t expectedBandwidth = 0;
        zes_mem_properties_t properties = {ZES_STRUCTURE_TYPE_MEM_PROPERTIES};
        zesMemoryGetProperties(handle, &properties);

        auto hwInfo = pSysmanDeviceImp->getRootDeviceEnvironment().getMutableHardwareInfo();
        auto &productHelper = pSysmanDeviceImp->getRootDeviceEnvironment().getHelper<NEO::ProductHelper>();
        hwInfo->platform.usRevId = productHelper.getHwRevIdFromStepping(REVISION_B, *hwInfo);

        auto pPmt = static_cast<MockMemoryPmt *>(pLinuxSysmanImp->getPlatformMonitoringTechAccess(properties.subdeviceId));
        pPmt->setGuid(guid64BitMemoryCounters);
        pPmt->mockVfid1Status = true;
        pSysfsAccess->mockReadUInt64Value.push_back(hbmRP0Frequency);
        pSysfsAccess->mockReadReturnStatus.push_back(ZE_RESULT_SUCCESS);

        EXPECT_EQ(zesMemoryGetBandwidth(handle, &bandwidth), ZE_RESULT_SUCCESS);
        expectedReadCounters |= vF0HbmHRead;
        expectedReadCounters = (expectedReadCounters << 32) | vF0HbmLRead;
        expectedReadCounters = expectedReadCounters * transactionSize;
        EXPECT_EQ(bandwidth.readCounter, expectedReadCounters);
        expectedWriteCounters |= vF0HbmHWrite;
        expectedWriteCounters = (expectedWriteCounters << 32) | vF0HbmLWrite;
        expectedWriteCounters = expectedWriteCounters * transactionSize;
        EXPECT_EQ(bandwidth.writeCounter, expectedWriteCounters);
        expectedBandwidth = 128 * hbmRP0Frequency * 1000 * 1000 * 4;
        EXPECT_EQ(bandwidth.maxBandwidth, expectedBandwidth);
    }
}

HWTEST2_F(SysmanDeviceMemoryFixture, GivenValidUsRevIdForRevisionBWhenCallingzesSysmanMemoryGetBandwidthThenSuccessIsReturnedAndBandwidthIsValid, IsPVC) {
    setLocalSupportedAndReinit(true);
    auto handles = getMemoryHandles(memoryHandleComponentCount);

    for (auto &handle : handles) {
        ASSERT_NE(nullptr, handle);
        zes_mem_bandwidth_t bandwidth{};
        zes_mem_properties_t properties = {ZES_STRUCTURE_TYPE_MEM_PROPERTIES};
        zesMemoryGetProperties(handle, &properties);

        auto hwInfo = pSysmanDeviceImp->getRootDeviceEnvironment().getMutableHardwareInfo();
        auto &productHelper = pSysmanDeviceImp->getRootDeviceEnvironment().getHelper<NEO::ProductHelper>();
        hwInfo->platform.usRevId = productHelper.getHwRevIdFromStepping(REVISION_B, *hwInfo);

        auto pPmt = static_cast<MockMemoryPmt *>(pLinuxSysmanImp->getPlatformMonitoringTechAccess(properties.subdeviceId));
        pPmt->setGuid(guid64BitMemoryCounters);
        pPmt->mockVfid1Status = true;
        pSysfsAccess->mockReadUInt64Value.push_back(hbmRP0Frequency);
        pSysfsAccess->mockReadReturnStatus.push_back(ZE_RESULT_SUCCESS);

        EXPECT_EQ(zesMemoryGetBandwidth(handle, &bandwidth), ZE_RESULT_SUCCESS);
        uint64_t expectedBandwidth = 128 * hbmRP0Frequency * 1000 * 1000 * 4;
        EXPECT_EQ(bandwidth.maxBandwidth, expectedBandwidth);
    }
}

TEST_F(SysmanDeviceMemoryFixture, GivenValidMemoryHandleWhenCallingzesSysmanMemoryGetBandwidthForDg2PlatformThenSuccessIsReturnedAndBandwidthIsValid) {
    setLocalSupportedAndReinit(true);
    auto hwInfo = pLinuxSysmanImp->getSysmanDeviceImp()->getRootDeviceEnvironment().getMutableHardwareInfo();
    hwInfo->platform.eProductFamily = IGFX_DG2;

    auto handles = getMemoryHandles(memoryHandleComponentCount);

    for (const auto &handle : handles) {
        ASSERT_NE(nullptr, handle);
        zes_mem_properties_t properties = {};
        zesMemoryGetProperties(handle, &properties);

        zes_mem_bandwidth_t bandwidth;
        uint64_t expectedReadCounters = 0, expectedWriteCounters = 0, expectedBandwidth = 0;
        uint64_t mockMaxBwDg2 = 1343616u;
        pSysfsAccess->mockReadUInt64Value.push_back(mockMaxBwDg2);
        pSysfsAccess->mockReadReturnStatus.push_back(ZE_RESULT_SUCCESS);
        EXPECT_EQ(zesMemoryGetBandwidth(handle, &bandwidth), ZE_RESULT_SUCCESS);
        expectedReadCounters = numberMcChannels * (mockIdiReadVal + mockDisplayVc1ReadVal) * transactionSize;
        EXPECT_EQ(expectedReadCounters, bandwidth.readCounter);
        expectedWriteCounters = numberMcChannels * mockIdiWriteVal * transactionSize;
        EXPECT_EQ(expectedWriteCounters, bandwidth.writeCounter);
        expectedBandwidth = mockMaxBwDg2 * MbpsToBytesPerSecond;
        EXPECT_EQ(expectedBandwidth, bandwidth.maxBandwidth);
        EXPECT_GT(bandwidth.timestamp, 0u);
    }
}

TEST_F(SysmanDeviceMemoryFixture, GivenValidMemoryHandleWhenCallingzesSysmanMemoryGetBandwidthForUnknownPlatformThenFailureIsReturned) {
    setLocalSupportedAndReinit(true);
    auto hwInfo = pLinuxSysmanImp->getSysmanDeviceImp()->getRootDeviceEnvironment().getMutableHardwareInfo();
    hwInfo->platform.eProductFamily = IGFX_UNKNOWN;

    auto handles = getMemoryHandles(memoryHandleComponentCount);

    for (const auto &handle : handles) {
        ASSERT_NE(nullptr, handle);
        zes_mem_bandwidth_t bandwidth;
        EXPECT_EQ(zesMemoryGetBandwidth(handle, &bandwidth), ZE_RESULT_ERROR_UNSUPPORTED_FEATURE);
    }
}

HWTEST2_F(SysmanDeviceMemoryFixture, GivenValidMemoryHandleWhenCallingzesSysmanMemoryGetBandwidthForDg2PlatformIfIdiReadFailsTheFailureIsReturned, IsDG2) {
    setLocalSupportedAndReinit(true);
    auto handles = getMemoryHandles(memoryHandleComponentCount);

    for (auto handle : handles) {
        ASSERT_NE(nullptr, handle);
        zes_mem_properties_t properties = {};
        zesMemoryGetProperties(handle, &properties);

        zes_mem_bandwidth_t bandwidth;

        auto pPmt = static_cast<MockMemoryPmt *>(pLinuxSysmanImp->getPlatformMonitoringTechAccess(properties.subdeviceId));
        pPmt->mockIdiReadValueFailureReturnStatus = ZE_RESULT_ERROR_UNKNOWN;
        EXPECT_EQ(zesMemoryGetBandwidth(handle, &bandwidth), ZE_RESULT_ERROR_UNKNOWN);
    }
}

HWTEST2_F(SysmanDeviceMemoryFixture, GivenValidMemoryHandleWhenCallingzesSysmanMemoryGetBandwidthForDg2PlatformAndReadingMaxBwFailsThenMaxBwIsReturnedAsZero, IsDG2) {
    setLocalSupportedAndReinit(true);
    auto handles = getMemoryHandles(memoryHandleComponentCount);

    for (const auto &handle : handles) {
        ASSERT_NE(nullptr, handle);
        zes_mem_properties_t properties = {};
        zesMemoryGetProperties(handle, &properties);

        zes_mem_bandwidth_t bandwidth;
        pSysfsAccess->mockReadReturnStatus.push_back(ZE_RESULT_ERROR_UNSUPPORTED_FEATURE);

        EXPECT_EQ(zesMemoryGetBandwidth(handle, &bandwidth), ZE_RESULT_SUCCESS);
        EXPECT_EQ(bandwidth.maxBandwidth, 0u);
    }
}

HWTEST2_F(SysmanDeviceMemoryFixture, GivenValidMemoryHandleWhenCallingzesSysmanMemoryGetBandwidthForDg2PlatformIfIdiWriteFailsTheFailureIsReturned, IsDG2) {
    setLocalSupportedAndReinit(true);

    auto handles = getMemoryHandles(memoryHandleComponentCount);

    for (auto handle : handles) {
        ASSERT_NE(nullptr, handle);
        zes_mem_properties_t properties = {};
        zesMemoryGetProperties(handle, &properties);

        zes_mem_bandwidth_t bandwidth;

        auto pPmt = static_cast<MockMemoryPmt *>(pLinuxSysmanImp->getPlatformMonitoringTechAccess(properties.subdeviceId));
        pPmt->mockIdiWriteFailureReturnStatus = ZE_RESULT_ERROR_UNKNOWN;
        EXPECT_EQ(zesMemoryGetBandwidth(handle, &bandwidth), ZE_RESULT_ERROR_UNKNOWN);
    }
}

TEST_F(SysmanDeviceMemoryFixture, GivenValidMemoryHandleWhenCallingzesSysmanMemoryGetBandwidthForDg2PlatformIfDisplayVc1ReadFailsTheFailureIsReturned) {
    setLocalSupportedAndReinit(true);
    auto hwInfo = pLinuxSysmanImp->getSysmanDeviceImp()->getRootDeviceEnvironment().getMutableHardwareInfo();
    hwInfo->platform.eProductFamily = IGFX_DG2;
    auto handles = getMemoryHandles(memoryHandleComponentCount);

    for (auto handle : handles) {
        ASSERT_NE(nullptr, handle);
        zes_mem_properties_t properties = {};
        zesMemoryGetProperties(handle, &properties);

        zes_mem_bandwidth_t bandwidth;

        auto pPmt = static_cast<MockMemoryPmt *>(pLinuxSysmanImp->getPlatformMonitoringTechAccess(properties.subdeviceId));
        pPmt->mockDisplayVc1ReadFailureReturnStatus = ZE_RESULT_ERROR_UNKNOWN;
        EXPECT_EQ(zesMemoryGetBandwidth(handle, &bandwidth), ZE_RESULT_ERROR_UNKNOWN);
    }
}

TEST_F(SysmanDeviceMemoryFixture, GivenCallinggetHbmFrequencyWhenProductFamilyIsPVCForSteppingIsBAndOnSubDeviceThenHbmFrequencyShouldNotBeZero) {
    PublicLinuxMemoryImp *pLinuxMemoryImp = new PublicLinuxMemoryImp(pOsSysman, true, 1);
    uint64_t hbmFrequency = 0;
    pSysfsAccess->mockReadUInt64Value.push_back(hbmRP0Frequency);
    pSysfsAccess->mockReadReturnStatus.push_back(ZE_RESULT_SUCCESS);
    pLinuxMemoryImp->getHbmFrequency(IGFX_PVC, REVISION_B, hbmFrequency);
    EXPECT_EQ(hbmFrequency, hbmRP0Frequency * 1000 * 1000);
    delete pLinuxMemoryImp;
}

TEST_F(SysmanDeviceMemoryFixture, GivenCallinggetHbmFrequencyWhenProductFamilyIsPVCForSteppingA0ThenHbmFrequencyShouldBeNotZero) {
    PublicLinuxMemoryImp *pLinuxMemoryImp = new PublicLinuxMemoryImp(pOsSysman, true, 1);
    uint64_t hbmFrequency = 0;
    pLinuxMemoryImp->getHbmFrequency(IGFX_PVC, REVISION_A0, hbmFrequency);
    uint64_t expectedHbmFrequency = 3.2 * gigaUnitTransferToUnitTransfer;
    EXPECT_EQ(hbmFrequency, expectedHbmFrequency);
    delete pLinuxMemoryImp;
}

TEST_F(SysmanDeviceMemoryFixture, GivenCallinggetHbmFrequencyWhenProductFamilyIsUnsupportedThenHbmFrequencyShouldBeZero) {
    PublicLinuxMemoryImp *pLinuxMemoryImp = new PublicLinuxMemoryImp(pOsSysman, true, 1);
    uint64_t hbmFrequency = 0;
    pLinuxMemoryImp->getHbmFrequency(PRODUCT_FAMILY_FORCE_ULONG, REVISION_B, hbmFrequency);
    EXPECT_EQ(hbmFrequency, 0u);
    delete pLinuxMemoryImp;
}

TEST_F(SysmanDeviceMemoryFixture, GivenCallinggetHbmFrequencyWhenProductFamilyIsPVCWhenSteppingIsUnknownThenHbmFrequencyShouldBeZero) {
    PublicLinuxMemoryImp *pLinuxMemoryImp = new PublicLinuxMemoryImp(pOsSysman, true, 1);
    uint64_t hbmFrequency = 0;
    pLinuxMemoryImp->getHbmFrequency(IGFX_PVC, 255, hbmFrequency);
    EXPECT_EQ(hbmFrequency, 0u);
    delete pLinuxMemoryImp;
}

TEST_F(SysmanDeviceMemoryFixture, GivenCallinggetHbmFrequencyWhenProductFamilyIsPVCForSteppingIsBAndFailedToReadFrequencyThenHbmFrequencyShouldBeZero) {
    PublicLinuxMemoryImp *pLinuxMemoryImp = new PublicLinuxMemoryImp(pOsSysman, true, 1);
    uint64_t hbmFrequency = 0;
    pSysfsAccess->mockReadReturnStatus.push_back(ZE_RESULT_ERROR_NOT_AVAILABLE);
    pLinuxMemoryImp->getHbmFrequency(IGFX_PVC, REVISION_B, hbmFrequency);
    EXPECT_EQ(hbmFrequency, 0u);
    delete pLinuxMemoryImp;
}

TEST_F(SysmanDeviceMemoryFixture, GivenValidMemoryHandleWhenBothVfid0AndVfid1AreTrueThenErrorIsReturnedWhileGettingMemoryBandwidth) {
    setLocalSupportedAndReinit(true);
    auto hwInfo = pLinuxSysmanImp->getSysmanDeviceImp()->getRootDeviceEnvironment().getMutableHardwareInfo();
    hwInfo->platform.eProductFamily = IGFX_PVC;
    auto handles = getMemoryHandles(memoryHandleComponentCount);

    for (auto handle : handles) {
        ASSERT_NE(nullptr, handle);
        zes_mem_properties_t properties = {};
        zesMemoryGetProperties(handle, &properties);

        zes_mem_bandwidth_t bandwidth;

        auto pPmt = static_cast<MockMemoryPmt *>(pLinuxSysmanImp->getPlatformMonitoringTechAccess(properties.subdeviceId));
        pPmt->setGuid(guid64BitMemoryCounters);
        pPmt->mockReadArgumentValue.push_back(1);
        pPmt->mockReadValueReturnStatus.push_back(ZE_RESULT_SUCCESS); // Return success after reading VF0_VFID
        pPmt->mockReadArgumentValue.push_back(1);
        pPmt->mockReadValueReturnStatus.push_back(ZE_RESULT_SUCCESS); // Return success after reading VF1_VFID
        EXPECT_EQ(zesMemoryGetBandwidth(handle, &bandwidth), ZE_RESULT_ERROR_UNKNOWN);
    }
}

TEST_F(SysmanDeviceMemoryFixture, GivenValidMemoryHandleWhenBothVfid0AndVfid1AreFalseThenErrorIsReturnedWhileGettingMemoryBandwidth) {
    setLocalSupportedAndReinit(true);
    auto hwInfo = pLinuxSysmanImp->getSysmanDeviceImp()->getRootDeviceEnvironment().getMutableHardwareInfo();
    hwInfo->platform.eProductFamily = IGFX_PVC;
    auto handles = getMemoryHandles(memoryHandleComponentCount);

    for (auto handle : handles) {
        ASSERT_NE(nullptr, handle);
        zes_mem_properties_t properties = {};
        zesMemoryGetProperties(handle, &properties);

        zes_mem_bandwidth_t bandwidth;

        auto pPmt = static_cast<MockMemoryPmt *>(pLinuxSysmanImp->getPlatformMonitoringTechAccess(properties.subdeviceId));
        pPmt->setGuid(guid64BitMemoryCounters);
        pPmt->mockReadArgumentValue.push_back(0);
        pPmt->mockReadValueReturnStatus.push_back(ZE_RESULT_SUCCESS); // Return success after reading VF0_VFID
        pPmt->mockReadArgumentValue.push_back(0);
        pPmt->mockReadValueReturnStatus.push_back(ZE_RESULT_SUCCESS); // Return success after reading VF1_VFID
        EXPECT_EQ(zesMemoryGetBandwidth(handle, &bandwidth), ZE_RESULT_ERROR_UNKNOWN);
    }
}

TEST_F(SysmanDeviceMemoryFixture, GivenCallinggetHbmFrequencyWhenProductFamilyIsPVCAndSteppingIsNotA0ThenHbmFrequencyWillBeZero) {
    PublicLinuxMemoryImp *pLinuxMemoryImp = new PublicLinuxMemoryImp;
    uint64_t hbmFrequency = 0;
    pLinuxMemoryImp->getHbmFrequency(IGFX_PVC, REVISION_A1, hbmFrequency);
    EXPECT_EQ(hbmFrequency, 0u);
    delete pLinuxMemoryImp;
}

TEST_F(SysmanDeviceMemoryFixture, GivenValidMemoryHandleWhenCallingZesSysmanMemoryGetStateAndFwUtilInterfaceIsAbsentThenMemoryHealthWillBeUnknown) {
    setLocalSupportedAndReinit(true);

    pSysfsAccess->mockReadReturnStatus.push_back(ZE_RESULT_ERROR_NOT_AVAILABLE);
    auto handles = getMemoryHandles(memoryHandleComponentCount);
    VariableBackup<L0::Sysman::FirmwareUtil *> backup(&pLinuxSysmanImp->pFwUtilInterface);
    pLinuxSysmanImp->pFwUtilInterface = nullptr;

    for (auto handle : handles) {
        ASSERT_NE(nullptr, handle);
        zes_mem_state_t state;
        ze_result_t result = zesMemoryGetState(handle, &state);

        EXPECT_EQ(result, ZE_RESULT_SUCCESS);
        EXPECT_EQ(state.health, ZES_MEM_HEALTH_UNKNOWN);
    }
}

TEST_F(SysmanMultiDeviceFixture, GivenValidDevicePointerWhenGettingMemoryPropertiesThenValidMemoryPropertiesRetrieved) {
    zes_mem_properties_t properties = {};
    ze_bool_t isSubdevice = pLinuxSysmanImp->getSubDeviceCount() == 0 ? false : true;
    auto subDeviceId = std::max(0u, pLinuxSysmanImp->getSubDeviceCount() - 1);
    L0::Sysman::LinuxMemoryImp *pLinuxMemoryImp = new L0::Sysman::LinuxMemoryImp(pOsSysman, isSubdevice, subDeviceId);
    EXPECT_EQ(ZE_RESULT_SUCCESS, pLinuxMemoryImp->getProperties(&properties));
    EXPECT_EQ(properties.subdeviceId, subDeviceId);
    EXPECT_EQ(properties.onSubdevice, isSubdevice);
    delete pLinuxMemoryImp;
}

class SysmanMultiDeviceMemoryFixture : public SysmanMultiDeviceFixture {
  protected:
    std::unique_ptr<MockMemorySysfsAccess> pSysfsAccess;
    L0::Sysman::SysFsAccessInterface *pSysfsAccessOld = nullptr;
    MockMemoryNeoDrm *pDrm = nullptr;
    Drm *pOriginalDrm = nullptr;
    L0::Sysman::SysmanDevice *device = nullptr;

    void SetUp() override {
        debugManager.flags.EnableLocalMemory.set(1);
        SysmanMultiDeviceFixture::SetUp();

        pSysfsAccessOld = pLinuxSysmanImp->pSysfsAccess;
        pSysfsAccess = std::make_unique<MockMemorySysfsAccess>();
        pLinuxSysmanImp->pSysfsAccess = pSysfsAccess.get();
        pDrm = new MockMemoryNeoDrm(const_cast<NEO::RootDeviceEnvironment &>(pSysmanDeviceImp->getRootDeviceEnvironment()));
        pDrm->ioctlHelper = static_cast<std::unique_ptr<NEO::IoctlHelper>>(std::make_unique<NEO::MockIoctlHelper>(*pDrm));
        auto &osInterface = pSysmanDeviceImp->getRootDeviceEnvironment().osInterface;
        osInterface->setDriverModel(std::unique_ptr<MockMemoryNeoDrm>(pDrm));

        pSysmanDeviceImp->pMemoryHandleContext->handleList.clear();
        device = pSysmanDeviceImp;
        getMemoryHandles(0);
    }

    void TearDown() override {
        SysmanMultiDeviceFixture::TearDown();
        pLinuxSysmanImp->pSysfsAccess = pSysfsAccessOld;
    }

    void setLocalSupportedAndReinit(bool supported) {
        debugManager.flags.EnableLocalMemory.set(supported == true ? 1 : 0);

        pSysmanDeviceImp->pMemoryHandleContext->handleList.clear();
        pSysmanDeviceImp->pMemoryHandleContext->init(pOsSysman->getSubDeviceCount());
    }

    std::vector<zes_mem_handle_t> getMemoryHandles(uint32_t count) {
        std::vector<zes_mem_handle_t> handles(count, nullptr);
        EXPECT_EQ(zesDeviceEnumMemoryModules(device->toHandle(), &count, handles.data()), ZE_RESULT_SUCCESS);
        return handles;
    }
};

TEST_F(SysmanMultiDeviceMemoryFixture, GivenValidMemoryHandleWhenGettingMemoryPropertiesWhileCallingGetValErrorThenValidMemoryPropertiesRetrieved) {
    pSysfsAccess->mockReadStringValue.push_back("0");
    pSysfsAccess->mockReadReturnStatus.push_back(ZE_RESULT_ERROR_NOT_AVAILABLE);

    pSysmanDeviceImp->pMemoryHandleContext->init(pOsSysman->getSubDeviceCount());
    for (auto subDeviceId = 0u; subDeviceId < pOsSysman->getSubDeviceCount(); subDeviceId++) {
        zes_mem_properties_t properties = {};
        auto isSubDevice = pOsSysman->getSubDeviceCount() > 0u;
        L0::Sysman::LinuxMemoryImp *pLinuxMemoryImp = new L0::Sysman::LinuxMemoryImp(pOsSysman, isSubDevice, subDeviceId);
        EXPECT_EQ(ZE_RESULT_SUCCESS, pLinuxMemoryImp->getProperties(&properties));
        EXPECT_EQ(properties.subdeviceId, subDeviceId);
        EXPECT_EQ(properties.onSubdevice, isSubDevice);
        EXPECT_EQ(properties.physicalSize, 0u);
        delete pLinuxMemoryImp;
    }
}

TEST_F(SysmanMultiDeviceMemoryFixture, GivenValidDevicePointerWhenGettingMemoryPropertiesThenValidMemoryPropertiesRetrieved) {
    pSysfsAccess->mockReadStringValue.push_back(mockPhysicalSize);
    pSysfsAccess->mockReadReturnStatus.push_back(ZE_RESULT_SUCCESS);
    pSysfsAccess->isRepeated = true;

    setLocalSupportedAndReinit(true);
    uint32_t count = 0;
    EXPECT_EQ(zesDeviceEnumMemoryModules(device->toHandle(), &count, nullptr), ZE_RESULT_SUCCESS);
    EXPECT_EQ(count, std::max(pOsSysman->getSubDeviceCount(), 1u));

    std::vector<zes_mem_handle_t> handles(count, nullptr);
    EXPECT_EQ(zesDeviceEnumMemoryModules(device->toHandle(), &count, handles.data()), ZE_RESULT_SUCCESS);
    for (auto handle : handles) {
        zes_mem_properties_t properties = {};
        EXPECT_EQ(zesMemoryGetProperties(handle, &properties), ZE_RESULT_SUCCESS);
        EXPECT_TRUE(properties.onSubdevice);
        EXPECT_EQ(properties.physicalSize, strtoull(mockPhysicalSize.c_str(), nullptr, 16));
    }
}

TEST_F(SysmanMultiDeviceMemoryFixture, GivenValidMemoryHandleWhenCallingZetSysmanMemoryGetStateThenVerifySysmanMemoryGetStateCallSucceeds) {
    setLocalSupportedAndReinit(true);

    auto handles = getMemoryHandles(pOsSysman->getSubDeviceCount());
    for (auto handle : handles) {
        ASSERT_NE(nullptr, handle);
    }

    zes_mem_state_t state1;
    ze_result_t result = zesMemoryGetState(handles[0], &state1);
    EXPECT_EQ(result, ZE_RESULT_SUCCESS);
    EXPECT_EQ(state1.health, ZES_MEM_HEALTH_OK);
    EXPECT_EQ(state1.size, NEO::probedSizeRegionOne);
    EXPECT_EQ(state1.free, NEO::unallocatedSizeRegionOne);

    zes_mem_state_t state2;
    result = zesMemoryGetState(handles[1], &state2);
    EXPECT_EQ(result, ZE_RESULT_SUCCESS);
    EXPECT_EQ(state2.health, ZES_MEM_HEALTH_OK);
    EXPECT_EQ(state2.size, NEO::probedSizeRegionFour);
    EXPECT_EQ(state2.free, NEO::unallocatedSizeRegionFour);
}

} // namespace ult
} // namespace Sysman
} // namespace L0
