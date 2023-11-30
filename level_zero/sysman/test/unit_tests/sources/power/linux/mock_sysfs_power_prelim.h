/*
 * Copyright (C) 2020-2023 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once

#include "shared/source/helpers/string.h"

#include "level_zero/sysman/source/api/power/linux/sysman_os_power_imp_prelim.h"
#include "level_zero/sysman/source/api/power/sysman_power_imp.h"
#include "level_zero/sysman/source/device/sysman_device_imp.h"
#include "level_zero/sysman/source/shared/linux/pmt/sysman_pmt.h"
#include "level_zero/sysman/source/sysman_const.h"
#include "level_zero/sysman/test/unit_tests/sources/linux/mock_sysman_fixture.h"

namespace L0 {
namespace Sysman {
namespace ult {

constexpr uint64_t setEnergyCounter = (83456u * 1048576u);
constexpr uint64_t offset = 0x400;
constexpr uint32_t mockLimitCount = 2u;
const std::string deviceName("device");
const std::string baseTelemSysFS("/sys/class/intel_pmt");
const std::string hwmonDir("device/hwmon");
const std::string i915HwmonDir("device/hwmon/hwmon2");
const std::string nonI915HwmonDir("device/hwmon/hwmon1");
const std::string i915HwmonDirTile0("device/hwmon/hwmon3");
const std::string i915HwmonDirTile1("device/hwmon/hwmon4");
const std::vector<std::string> listOfMockedHwmonDirs = {"hwmon0", "hwmon1", "hwmon2", "hwmon3", "hwmon4"};
const std::string sustainedPowerLimit("power1_max");
const std::string sustainedPowerLimitInterval("power1_max_interval");
const std::string criticalPowerLimit1("curr1_crit");
const std::string criticalPowerLimit2("power1_crit");
const std::string energyCounterNode("energy1_input");
const std::string defaultPowerLimit("power1_rated_max");
constexpr uint64_t expectedEnergyCounter = 123456785u;
constexpr uint64_t expectedEnergyCounterTile0 = 123456785u;
constexpr uint64_t expectedEnergyCounterTile1 = 128955785u;
constexpr uint32_t mockDefaultPowerLimitVal = 300000000;
constexpr uint64_t mockMinPowerLimitVal = 300000000;
constexpr uint64_t mockMaxPowerLimitVal = 600000000;
const std::map<std::string, uint64_t> deviceKeyOffsetMapPower = {
    {"PACKAGE_ENERGY", 0x400},
    {"COMPUTE_TEMPERATURES", 0x68},
    {"SOC_TEMPERATURES", 0x60},
    {"CORE_TEMPERATURES", 0x6c}};

struct MockPowerSysfsAccess : public L0::Sysman::SysFsAccessInterface {
    ze_result_t mockReadResult = ZE_RESULT_SUCCESS;
    ze_result_t mockReadPeakResult = ZE_RESULT_SUCCESS;
    ze_result_t mockWriteResult = ZE_RESULT_SUCCESS;
    ze_result_t mockReadIntResult = ZE_RESULT_SUCCESS;
    ze_result_t mockWritePeakLimitResult = ZE_RESULT_SUCCESS;
    ze_result_t mockscanDirEntriesResult = ZE_RESULT_SUCCESS;
    std::vector<ze_result_t> mockReadValUnsignedLongResult{};
    std::vector<ze_result_t> mockWriteUnsignedResult{};

    ze_result_t getValString(const std::string file, std::string &val) {
        ze_result_t result = ZE_RESULT_ERROR_UNKNOWN;
        if (file.compare(i915HwmonDir + "/" + "name") == 0) {
            val = "i915";
            result = ZE_RESULT_SUCCESS;
        } else if (file.compare(nonI915HwmonDir + "/" + "name") == 0) {
            result = ZE_RESULT_ERROR_NOT_AVAILABLE;
        } else if (file.compare(i915HwmonDirTile1 + "/" + "name") == 0) {
            val = "i915_gt1";
            result = ZE_RESULT_SUCCESS;
        } else if (file.compare(i915HwmonDirTile0 + "/" + "name") == 0) {
            val = "i915_gt0";
            result = ZE_RESULT_SUCCESS;
        } else {
            val = "garbageI915";
            result = ZE_RESULT_SUCCESS;
        }
        return result;
    }

    uint64_t sustainedPowerLimitVal = 0;
    uint64_t criticalPowerLimitVal = 0;
    int32_t sustainedPowerLimitIntervalVal = 0;

    ze_result_t getValUnsignedLongHelper(const std::string file, uint64_t &val);
    ze_result_t getValUnsignedLong(const std::string file, uint64_t &val) {
        ze_result_t result = ZE_RESULT_SUCCESS;
        if (file.compare(i915HwmonDir + "/" + sustainedPowerLimit) == 0) {
            val = sustainedPowerLimitVal;
        } else if ((file.compare(i915HwmonDir + "/" + criticalPowerLimit1) == 0) || (file.compare(i915HwmonDir + "/" + criticalPowerLimit2) == 0)) {
            if (mockReadPeakResult != ZE_RESULT_SUCCESS) {
                return mockReadPeakResult;
            }
            val = criticalPowerLimitVal;
        } else if (file.compare(i915HwmonDirTile0 + "/" + energyCounterNode) == 0) {
            val = expectedEnergyCounterTile0;
        } else if (file.compare(i915HwmonDirTile1 + "/" + energyCounterNode) == 0) {
            val = expectedEnergyCounterTile1;
        } else if (file.compare(i915HwmonDir + "/" + energyCounterNode) == 0) {
            val = expectedEnergyCounter;
        } else if (file.compare(i915HwmonDir + "/" + defaultPowerLimit) == 0) {
            val = mockDefaultPowerLimitVal;
        } else {
            result = ZE_RESULT_ERROR_NOT_AVAILABLE;
        }

        return result;
    }

    ze_result_t getValUnsignedInt(const std::string file, uint32_t &val) {
        ze_result_t result = ZE_RESULT_SUCCESS;
        if (file.compare(i915HwmonDir + "/" + defaultPowerLimit) == 0) {
            val = mockDefaultPowerLimitVal;
        } else {
            result = ZE_RESULT_ERROR_NOT_AVAILABLE;
        }
        return result;
    }

    ze_result_t setVal(const std::string file, const int val) {
        ze_result_t result = ZE_RESULT_SUCCESS;
        if (file.compare(i915HwmonDir + "/" + sustainedPowerLimit) == 0) {
            sustainedPowerLimitVal = static_cast<uint64_t>(val);
        } else if ((file.compare(i915HwmonDir + "/" + sustainedPowerLimitInterval) == 0)) {
            sustainedPowerLimitIntervalVal = val;
        } else if ((file.compare(i915HwmonDir + "/" + criticalPowerLimit1) == 0) || (file.compare(i915HwmonDir + "/" + criticalPowerLimit2) == 0)) {
            if (mockWritePeakLimitResult != ZE_RESULT_SUCCESS) {
                return mockWritePeakLimitResult;
            }
            criticalPowerLimitVal = static_cast<uint64_t>(val);
        } else {
            result = ZE_RESULT_ERROR_NOT_AVAILABLE;
        }

        return result;
    }

    ze_result_t getscanDirEntries(const std::string file, std::vector<std::string> &listOfEntries) {
        if (file.compare(hwmonDir) == 0) {
            listOfEntries = listOfMockedHwmonDirs;
            return ZE_RESULT_SUCCESS;
        }
        return ZE_RESULT_ERROR_NOT_AVAILABLE;
    }

    ze_result_t read(const std::string file, uint64_t &val) override {
        ze_result_t result = ZE_RESULT_SUCCESS;
        if (!mockReadValUnsignedLongResult.empty()) {
            result = mockReadValUnsignedLongResult.front();
            mockReadValUnsignedLongResult.erase(mockReadValUnsignedLongResult.begin());
            if (result != ZE_RESULT_SUCCESS) {
                return result;
            }
        }

        return getValUnsignedLong(file, val);
    }

    ze_result_t read(const std::string file, int32_t &val) override {
        if (mockReadIntResult != ZE_RESULT_SUCCESS) {
            return mockReadIntResult;
        }

        if (file.compare(i915HwmonDir + "/" + sustainedPowerLimitInterval) == 0) {
            val = sustainedPowerLimitIntervalVal;
            return ZE_RESULT_SUCCESS;
        }

        return ZE_RESULT_ERROR_NOT_AVAILABLE;
    }

    ze_result_t read(const std::string file, std::string &val) override {
        if (mockReadResult != ZE_RESULT_SUCCESS) {
            return mockReadResult;
        }

        return getValString(file, val);
    }

    ze_result_t read(const std::string file, uint32_t &val) override {
        if (mockReadResult != ZE_RESULT_SUCCESS) {
            return mockReadResult;
        }
        return getValUnsignedInt(file, val);
    }

    ze_result_t write(const std::string file, const int val) override {
        if (mockWriteResult != ZE_RESULT_SUCCESS) {
            return mockWriteResult;
        }

        return setVal(file, val);
    }

    ze_result_t write(const std::string file, const uint64_t val) override {
        ze_result_t result = ZE_RESULT_SUCCESS;
        if (!mockWriteUnsignedResult.empty()) {
            result = mockWriteUnsignedResult.front();
            mockWriteUnsignedResult.erase(mockWriteUnsignedResult.begin());
            if (result != ZE_RESULT_SUCCESS) {
                return result;
            }
        }

        if (file.compare(i915HwmonDir + "/" + sustainedPowerLimit) == 0) {
            if (val < mockMinPowerLimitVal) {
                sustainedPowerLimitVal = mockMinPowerLimitVal;
            } else if (val > mockMaxPowerLimitVal) {
                sustainedPowerLimitVal = mockMaxPowerLimitVal;
            } else {
                sustainedPowerLimitVal = val;
            }
        } else if ((file.compare(i915HwmonDir + "/" + criticalPowerLimit1) == 0) || (file.compare(i915HwmonDir + "/" + criticalPowerLimit2) == 0)) {
            if (mockWritePeakLimitResult != ZE_RESULT_SUCCESS) {
                return mockWritePeakLimitResult;
            }
            criticalPowerLimitVal = val;
        } else {
            result = ZE_RESULT_ERROR_NOT_AVAILABLE;
        }

        return result;
    }

    ze_result_t scanDirEntries(const std::string file, std::vector<std::string> &listOfEntries) override {
        if (mockscanDirEntriesResult != ZE_RESULT_SUCCESS) {
            return mockscanDirEntriesResult;
        }

        return getscanDirEntries(file, listOfEntries);
    }

    MockPowerSysfsAccess() = default;
};

struct MockPowerPmt : public L0::Sysman::PlatformMonitoringTech {

    MockPowerPmt(L0::Sysman::FsAccessInterface *pFsAccess, ze_bool_t onSubdevice, uint32_t subdeviceId) : L0::Sysman::PlatformMonitoringTech(pFsAccess, onSubdevice, subdeviceId) {}
    using L0::Sysman::PlatformMonitoringTech::keyOffsetMap;
    using L0::Sysman::PlatformMonitoringTech::preadFunction;
    using L0::Sysman::PlatformMonitoringTech::telemetryDeviceEntry;
    ~MockPowerPmt() override {
        rootDeviceTelemNodeIndex = 0;
    }

    void mockedInit(L0::Sysman::FsAccessInterface *pFsAccess) {
        std::string gpuUpstreamPortPath = "/sys/devices/pci0000:89/0000:89:02.0/0000:8a:00.0";
        if (ZE_RESULT_SUCCESS != L0::Sysman::PlatformMonitoringTech::enumerateRootTelemIndex(pFsAccess, gpuUpstreamPortPath)) {
            return;
        }

        telemetryDeviceEntry = "/sys/class/intel_pmt/telem2/telem";
    }
};

struct MockPowerFsAccess : public L0::Sysman::FsAccessInterface {

    ze_result_t listDirectory(const std::string directory, std::vector<std::string> &listOfTelemNodes) override {
        if (directory.compare(baseTelemSysFS) == 0) {
            listOfTelemNodes.push_back("telem1");
            listOfTelemNodes.push_back("telem2");
            listOfTelemNodes.push_back("telem3");
            listOfTelemNodes.push_back("telem4");
            listOfTelemNodes.push_back("telem5");
            return ZE_RESULT_SUCCESS;
        }

        return ZE_RESULT_ERROR_NOT_AVAILABLE;
    }

    ze_result_t getRealPath(const std::string path, std::string &buf) override {
        if (path.compare("/sys/class/intel_pmt/telem1") == 0) {
            buf = "/sys/devices/pci0000:89/0000:89:02.0/0000:86:00.0/0000:8b:02.0/0000:8e:00.1/pmt_telemetry.1.auto/intel_pmt/telem1";
        } else if (path.compare("/sys/class/intel_pmt/telem2") == 0) {
            buf = "/sys/devices/pci0000:89/0000:89:02.0/0000:86:00.0/0000:8b:02.0/0000:8e:00.1/pmt_telemetry.1.auto/intel_pmt/telem2";
        } else if (path.compare("/sys/class/intel_pmt/telem3") == 0) {
            buf = "/sys/devices/pci0000:89/0000:89:02.0/0000:8a:00.0/0000:8b:02.0/0000:8e:00.1/pmt_telemetry.1.auto/intel_pmt/telem3";
        } else if (path.compare("/sys/class/intel_pmt/telem4") == 0) {
            buf = "/sys/devices/pci0000:89/0000:89:02.0/0000:8a:00.0/0000:8b:02.0/0000:8e:00.1/pmt_telemetry.1.auto/intel_pmt/telem4";
        } else if (path.compare("/sys/class/intel_pmt/telem5") == 0) {
            buf = "/sys/devices/pci0000:89/0000:89:02.0/0000:8a:00.0/0000:8b:02.0/0000:8e:00.1/pmt_telemetry.1.auto/intel_pmt/telem5";
        } else {
            return ZE_RESULT_ERROR_NOT_AVAILABLE;
        }

        return ZE_RESULT_SUCCESS;
    }

    MockPowerFsAccess() = default;
};

class PublicLinuxPowerImp : public L0::Sysman::LinuxPowerImp {
  public:
    PublicLinuxPowerImp(L0::Sysman::OsSysman *pOsSysman, ze_bool_t onSubdevice, uint32_t subdeviceId) : L0::Sysman::LinuxPowerImp(pOsSysman, onSubdevice, subdeviceId) {}
    using L0::Sysman::LinuxPowerImp::pPmt;
    using L0::Sysman::LinuxPowerImp::pSysfsAccess;
};

class SysmanDevicePowerFixture : public SysmanDeviceFixture {
  protected:
    L0::Sysman::SysmanDevice *device = nullptr;
    std::unique_ptr<PublicLinuxPowerImp> pPublicLinuxPowerImp;
    std::unique_ptr<MockPowerPmt> pPmt;
    std::unique_ptr<MockPowerFsAccess> pFsAccess;
    std::unique_ptr<MockPowerSysfsAccess> pSysfsAccess;
    L0::Sysman::SysFsAccessInterface *pSysfsAccessOld = nullptr;
    L0::Sysman::FsAccessInterface *pFsAccessOriginal = nullptr;
    L0::Sysman::OsPower *pOsPowerOriginal = nullptr;
    std::map<uint32_t, L0::Sysman::PlatformMonitoringTech *> pmtMapOriginal;
    void SetUp() override {
        SysmanDeviceFixture::SetUp();
        device = pSysmanDevice;
        pFsAccess = std::make_unique<MockPowerFsAccess>();
        pFsAccessOriginal = pLinuxSysmanImp->pFsAccess;
        pLinuxSysmanImp->pFsAccess = pFsAccess.get();
        pSysfsAccessOld = pLinuxSysmanImp->pSysfsAccess;
        pSysfsAccess = std::make_unique<MockPowerSysfsAccess>();
        pLinuxSysmanImp->pSysfsAccess = pSysfsAccess.get();

        pmtMapOriginal = pLinuxSysmanImp->mapOfSubDeviceIdToPmtObject;
        pLinuxSysmanImp->mapOfSubDeviceIdToPmtObject.clear();

        auto subDeviceCount = pLinuxSysmanImp->getSubDeviceCount();
        uint32_t subdeviceId = 0;
        do {
            ze_bool_t onSubdevice = (subDeviceCount == 0) ? false : true;
            auto pPmt = new MockPowerPmt(pFsAccess.get(), onSubdevice, subdeviceId);
            pPmt->mockedInit(pFsAccess.get());
            pPmt->keyOffsetMap = deviceKeyOffsetMapPower;
            pLinuxSysmanImp->mapOfSubDeviceIdToPmtObject.emplace(subdeviceId, pPmt);
        } while (++subdeviceId < subDeviceCount);

        getPowerHandles(0);
    }
    void TearDown() override {
        pLinuxSysmanImp->releasePmtObject();
        pLinuxSysmanImp->mapOfSubDeviceIdToPmtObject = pmtMapOriginal;
        pLinuxSysmanImp->pFsAccess = pFsAccessOriginal;
        pLinuxSysmanImp->pSysfsAccess = pSysfsAccessOld;
        SysmanDeviceFixture::TearDown();
    }
    std::vector<zes_pwr_handle_t> getPowerHandles(uint32_t count) {
        std::vector<zes_pwr_handle_t> handles(count, nullptr);
        EXPECT_EQ(zesDeviceEnumPowerDomains(device->toHandle(), &count, handles.data()), ZE_RESULT_SUCCESS);
        return handles;
    }
};

class SysmanDevicePowerMultiDeviceFixture : public SysmanMultiDeviceFixture {
  protected:
    L0::Sysman::SysmanDevice *device = nullptr;
    std::unique_ptr<PublicLinuxPowerImp> pPublicLinuxPowerImp;
    std::unique_ptr<MockPowerPmt> pPmt;
    std::unique_ptr<MockPowerFsAccess> pFsAccess;
    std::unique_ptr<MockPowerSysfsAccess> pSysfsAccess;
    L0::Sysman::SysFsAccessInterface *pSysfsAccessOld = nullptr;
    L0::Sysman::FsAccessInterface *pFsAccessOriginal = nullptr;
    L0::Sysman::OsPower *pOsPowerOriginal = nullptr;
    std::map<uint32_t, L0::Sysman::PlatformMonitoringTech *> mapOriginal;
    void SetUp() override {
        SysmanMultiDeviceFixture::SetUp();
        device = pSysmanDevice;
        pFsAccess = std::make_unique<MockPowerFsAccess>();
        pFsAccessOriginal = pLinuxSysmanImp->pFsAccess;
        pLinuxSysmanImp->pFsAccess = pFsAccess.get();
        pSysfsAccessOld = pLinuxSysmanImp->pSysfsAccess;
        pSysfsAccess = std::make_unique<MockPowerSysfsAccess>();
        pLinuxSysmanImp->pSysfsAccess = pSysfsAccess.get();

        mapOriginal = pLinuxSysmanImp->mapOfSubDeviceIdToPmtObject;
        pLinuxSysmanImp->mapOfSubDeviceIdToPmtObject.clear();

        auto subDeviceCount = pLinuxSysmanImp->getSubDeviceCount();
        uint32_t subdeviceId = 0;
        do {
            ze_bool_t onSubdevice = (subDeviceCount == 0) ? false : true;
            auto pPmt = new MockPowerPmt(pFsAccess.get(), onSubdevice, subdeviceId);
            pPmt->mockedInit(pFsAccess.get());
            pPmt->keyOffsetMap = deviceKeyOffsetMapPower;
            pLinuxSysmanImp->mapOfSubDeviceIdToPmtObject.emplace(subdeviceId, pPmt);
        } while (++subdeviceId < subDeviceCount);
    }

    void TearDown() override {
        for (auto &pmtMapElement : pLinuxSysmanImp->mapOfSubDeviceIdToPmtObject) {
            if (pmtMapElement.second) {
                delete pmtMapElement.second;
                pmtMapElement.second = nullptr;
            }
        }
        pLinuxSysmanImp->pFsAccess = pFsAccessOriginal;
        pLinuxSysmanImp->pSysfsAccess = pSysfsAccessOld;
        pLinuxSysmanImp->mapOfSubDeviceIdToPmtObject = mapOriginal;
        SysmanMultiDeviceFixture::TearDown();
    }
    std::vector<zes_pwr_handle_t> getPowerHandles(uint32_t count) {
        std::vector<zes_pwr_handle_t> handles(count, nullptr);
        EXPECT_EQ(zesDeviceEnumPowerDomains(device->toHandle(), &count, handles.data()), ZE_RESULT_SUCCESS);
        return handles;
    }
};

} // namespace ult
} // namespace Sysman
} // namespace L0
