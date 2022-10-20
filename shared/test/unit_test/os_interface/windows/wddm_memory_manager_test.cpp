/*
 * Copyright (C) 2022 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/test/common/helpers/execution_environment_helper.h"
#include "shared/test/common/mocks/mock_device.h"
#include "shared/test/common/mocks/mock_memory_manager.h"
#include "shared/test/common/os_interface/windows/mock_wddm_memory_manager.h"
#include "shared/test/common/os_interface/windows/wddm_fixture.h"

using namespace NEO;
using namespace ::testing;

class MockAllocateGraphicsMemoryWithAlignmentWddm : public MemoryManagerCreate<WddmMemoryManager> {
  public:
    using WddmMemoryManager::allocateGraphicsMemoryWithAlignment;
    MockAllocateGraphicsMemoryWithAlignmentWddm(ExecutionEnvironment &executionEnvironment) : MemoryManagerCreate(false, false, executionEnvironment) {}
    bool allocateSystemMemoryAndCreateGraphicsAllocationFromItCalled = false;
    bool allocateGraphicsMemoryUsingKmdAndMapItToCpuVACalled = false;
    bool mapGpuVirtualAddressWithCpuPtr = false;

    GraphicsAllocation *allocateSystemMemoryAndCreateGraphicsAllocationFromIt(const AllocationData &allocationData) override {
        allocateSystemMemoryAndCreateGraphicsAllocationFromItCalled = true;

        return nullptr;
    }
    GraphicsAllocation *allocateGraphicsMemoryUsingKmdAndMapItToCpuVA(const AllocationData &allocationData, bool allowLargePages) override {
        allocateGraphicsMemoryUsingKmdAndMapItToCpuVACalled = true;

        return nullptr;
    }
    bool mapGpuVirtualAddress(WddmAllocation *graphicsAllocation, const void *requiredGpuPtr) override {
        if (requiredGpuPtr != nullptr) {
            mapGpuVirtualAddressWithCpuPtr = true;
        } else {
            mapGpuVirtualAddressWithCpuPtr = false;
        }

        return true;
    }
};

class WddmMemoryManagerTests : public ::testing::Test {
  public:
    MockAllocateGraphicsMemoryWithAlignmentWddm *memoryManager = nullptr;
    WddmMock *wddm = nullptr;
    ExecutionEnvironment *executionEnvironment = nullptr;

    void SetUp() override {
        HardwareInfo *hwInfo = nullptr;
        executionEnvironment = getExecutionEnvironmentImpl(hwInfo, 1);

        memoryManager = new MockAllocateGraphicsMemoryWithAlignmentWddm(*executionEnvironment);
        executionEnvironment->memoryManager.reset(memoryManager);
        wddm = static_cast<WddmMock *>(executionEnvironment->rootDeviceEnvironments[0]->osInterface->getDriverModel()->as<Wddm>());
    }

    void TearDown() override {
        delete executionEnvironment;
    }
};

TEST_F(WddmMemoryManagerTests, GivenAllocDataWithSVMCPUSetWhenAllocateGraphicsMemoryWithAlignmentThenProperFunctionIsUsed) {
    NEO::AllocationData allocData = {};
    allocData.type = NEO::AllocationType::SVM_CPU;
    memoryManager->allocateGraphicsMemoryWithAlignment(allocData);

    if (preferredAllocationMethod == GfxMemoryAllocationMethod::AllocateByKmd) {
        EXPECT_TRUE(memoryManager->allocateGraphicsMemoryUsingKmdAndMapItToCpuVACalled);
    } else {
        EXPECT_TRUE(memoryManager->allocateSystemMemoryAndCreateGraphicsAllocationFromItCalled);
    }
}

class MockAllocateGraphicsMemoryUsingKmdAndMapItToCpuVAWddm : public MemoryManagerCreate<WddmMemoryManager> {
  public:
    using WddmMemoryManager::allocateGraphicsMemoryUsingKmdAndMapItToCpuVA;
    using WddmMemoryManager::mapGpuVirtualAddress;
    MockAllocateGraphicsMemoryUsingKmdAndMapItToCpuVAWddm(ExecutionEnvironment &executionEnvironment) : MemoryManagerCreate(false, false, executionEnvironment) {}

    bool mapGpuVirtualAddressWithCpuPtr = false;

    bool mapGpuVirtualAddress(WddmAllocation *graphicsAllocation, const void *requiredGpuPtr) override {
        if (requiredGpuPtr != nullptr) {
            mapGpuVirtualAddressWithCpuPtr = true;
        } else {
            mapGpuVirtualAddressWithCpuPtr = false;
        }

        return true;
    }
};

class WddmMemoryManagerAllocPathTests : public ::testing::Test {
  public:
    MockAllocateGraphicsMemoryUsingKmdAndMapItToCpuVAWddm *memoryManager = nullptr;
    WddmMock *wddm = nullptr;
    ExecutionEnvironment *executionEnvironment = nullptr;

    void SetUp() override {
        HardwareInfo *hwInfo = nullptr;
        executionEnvironment = getExecutionEnvironmentImpl(hwInfo, 1);

        memoryManager = new MockAllocateGraphicsMemoryUsingKmdAndMapItToCpuVAWddm(*executionEnvironment);
        executionEnvironment->memoryManager.reset(memoryManager);
        wddm = static_cast<WddmMock *>(executionEnvironment->rootDeviceEnvironments[0]->osInterface->getDriverModel()->as<Wddm>());
    }

    void TearDown() override {
        delete executionEnvironment;
    }
};

TEST_F(WddmMemoryManagerAllocPathTests, givenAllocateGraphicsMemoryUsingKmdAndMapItToCpuVAWhenPreferedAllocationMethodThenProperArgumentsAreSet) {
    {
        NEO::AllocationData allocData = {};
        allocData.type = NEO::AllocationType::SVM_CPU;
        auto graphicsAllocation = memoryManager->allocateGraphicsMemoryUsingKmdAndMapItToCpuVA(allocData, false);

        if (preferredAllocationMethod == GfxMemoryAllocationMethod::AllocateByKmd) {
            EXPECT_FALSE(memoryManager->mapGpuVirtualAddressWithCpuPtr);
        } else {
            EXPECT_TRUE(memoryManager->mapGpuVirtualAddressWithCpuPtr);
        }

        memoryManager->freeGraphicsMemory(graphicsAllocation);
    }
    {
        NEO::AllocationData allocData = {};
        allocData.type = NEO::AllocationType::EXTERNAL_HOST_PTR;
        auto graphicsAllocation = memoryManager->allocateGraphicsMemoryUsingKmdAndMapItToCpuVA(allocData, false);

        EXPECT_TRUE(memoryManager->mapGpuVirtualAddressWithCpuPtr);

        memoryManager->freeGraphicsMemory(graphicsAllocation);
    }
}