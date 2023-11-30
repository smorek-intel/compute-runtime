/*
 * Copyright (C) 2020-2023 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "level_zero/core/source/kernel/kernel_imp.h"

#include "shared/source/assert_handler/assert_handler.h"
#include "shared/source/debugger/debugger_l0.h"
#include "shared/source/execution_environment/root_device_environment.h"
#include "shared/source/gmm_helper/gmm_helper.h"
#include "shared/source/helpers/basic_math.h"
#include "shared/source/helpers/bindless_heaps_helper.h"
#include "shared/source/helpers/blit_commands_helper.h"
#include "shared/source/helpers/gfx_core_helper.h"
#include "shared/source/helpers/hw_info.h"
#include "shared/source/helpers/kernel_helpers.h"
#include "shared/source/helpers/local_work_size.h"
#include "shared/source/helpers/per_thread_data.h"
#include "shared/source/helpers/ray_tracing_helper.h"
#include "shared/source/helpers/register_offsets.h"
#include "shared/source/helpers/simd_helper.h"
#include "shared/source/helpers/string.h"
#include "shared/source/helpers/surface_format_info.h"
#include "shared/source/kernel/implicit_args.h"
#include "shared/source/kernel/kernel_arg_descriptor.h"
#include "shared/source/kernel/kernel_descriptor.h"
#include "shared/source/memory_manager/allocation_properties.h"
#include "shared/source/memory_manager/memory_manager.h"
#include "shared/source/memory_manager/memory_operations_handler.h"
#include "shared/source/memory_manager/unified_memory_manager.h"
#include "shared/source/program/kernel_info.h"
#include "shared/source/program/work_size_info.h"
#include "shared/source/utilities/arrayref.h"

#include "level_zero/core/source/device/device.h"
#include "level_zero/core/source/device/device_imp.h"
#include "level_zero/core/source/driver/driver_handle_imp.h"
#include "level_zero/core/source/image/image.h"
#include "level_zero/core/source/image/image_format_desc_helper.h"
#include "level_zero/core/source/kernel/sampler_patch_values.h"
#include "level_zero/core/source/module/module.h"
#include "level_zero/core/source/module/module_imp.h"
#include "level_zero/core/source/printf_handler/printf_handler.h"
#include "level_zero/core/source/sampler/sampler.h"

#include "encode_surface_state_args.h"

#include <memory>

namespace L0 {
#include "level_zero/core/source/kernel/patch_with_implicit_surface.inl"

KernelImmutableData::KernelImmutableData(L0::Device *l0device) : device(l0device) {}

KernelImmutableData::~KernelImmutableData() {
    if (nullptr != isaGraphicsAllocation) {
        this->getDevice()->getNEODevice()->getMemoryManager()->freeGraphicsMemory(isaGraphicsAllocation.release());
    }
    crossThreadDataTemplate.reset();
    surfaceStateHeapTemplate.reset();
    dynamicStateHeapTemplate.reset();
}

ze_result_t KernelImmutableData::initialize(NEO::KernelInfo *kernelInfo, Device *device, uint32_t computeUnitsUsedForSratch,
                                            NEO::GraphicsAllocation *globalConstBuffer, NEO::GraphicsAllocation *globalVarBuffer,
                                            bool internalKernel) {

    UNRECOVERABLE_IF(kernelInfo == nullptr);
    this->kernelInfo = kernelInfo;
    this->kernelDescriptor = &kernelInfo->kernelDescriptor;

    DeviceImp *deviceImp = static_cast<DeviceImp *>(device);
    auto neoDevice = deviceImp->getActiveDevice();

    if (neoDevice->getDebugger() && kernelInfo->kernelDescriptor.external.debugData.get()) {
        createRelocatedDebugData(globalConstBuffer, globalVarBuffer);
    }

    this->crossThreadDataSize = this->kernelDescriptor->kernelAttributes.crossThreadDataSize;

    ArrayRef<uint8_t> crossThreadDataArrayRef;
    if (crossThreadDataSize != 0) {
        crossThreadDataTemplate.reset(new uint8_t[crossThreadDataSize]);

        if (kernelInfo->crossThreadData) {
            memcpy_s(crossThreadDataTemplate.get(), crossThreadDataSize,
                     kernelInfo->crossThreadData, crossThreadDataSize);
        } else {
            memset(crossThreadDataTemplate.get(), 0x00, crossThreadDataSize);
        }

        crossThreadDataArrayRef = ArrayRef<uint8_t>(this->crossThreadDataTemplate.get(), this->crossThreadDataSize);

        NEO::patchNonPointer<uint32_t>(crossThreadDataArrayRef,
                                       kernelDescriptor->payloadMappings.implicitArgs.simdSize, kernelDescriptor->kernelAttributes.simdSize);
    }

    if (NEO::KernelDescriptor::isBindlessAddressingKernel(kernelInfo->kernelDescriptor)) {
        auto &gfxCoreHelper = deviceImp->getNEODevice()->getGfxCoreHelper();
        auto surfaceStateSize = static_cast<uint32_t>(gfxCoreHelper.getRenderSurfaceStateSize());

        this->surfaceStateHeapSize = kernelInfo->kernelDescriptor.kernelAttributes.numArgsStateful * surfaceStateSize;
        DEBUG_BREAK_IF(kernelInfo->kernelDescriptor.kernelAttributes.numArgsStateful != kernelInfo->kernelDescriptor.getBindlessOffsetToSurfaceState().size());

        surfaceStateHeapTemplate.reset(new uint8_t[surfaceStateHeapSize]);
    } else if (kernelInfo->heapInfo.surfaceStateHeapSize != 0) {
        this->surfaceStateHeapSize = kernelInfo->heapInfo.surfaceStateHeapSize;
        surfaceStateHeapTemplate.reset(new uint8_t[surfaceStateHeapSize]);

        memcpy_s(surfaceStateHeapTemplate.get(), surfaceStateHeapSize,
                 kernelInfo->heapInfo.pSsh, surfaceStateHeapSize);
    }

    if (kernelInfo->heapInfo.dynamicStateHeapSize != 0) {
        this->dynamicStateHeapSize = kernelInfo->heapInfo.dynamicStateHeapSize;
        dynamicStateHeapTemplate.reset(new uint8_t[dynamicStateHeapSize]);

        memcpy_s(dynamicStateHeapTemplate.get(), dynamicStateHeapSize,
                 kernelInfo->heapInfo.pDsh, dynamicStateHeapSize);
    }

    ArrayRef<uint8_t> surfaceStateHeapArrayRef = ArrayRef<uint8_t>(surfaceStateHeapTemplate.get(), getSurfaceStateHeapSize());

    if (NEO::isValidOffset(kernelDescriptor->payloadMappings.implicitArgs.globalConstantsSurfaceAddress.stateless)) {
        UNRECOVERABLE_IF(nullptr == globalConstBuffer);

        patchWithImplicitSurface(crossThreadDataArrayRef, surfaceStateHeapArrayRef,
                                 static_cast<uintptr_t>(globalConstBuffer->getGpuAddressToPatch()),
                                 *globalConstBuffer, kernelDescriptor->payloadMappings.implicitArgs.globalConstantsSurfaceAddress,
                                 *neoDevice, kernelDescriptor->kernelAttributes.flags.useGlobalAtomics, deviceImp->isImplicitScalingCapable());
        this->residencyContainer.push_back(globalConstBuffer);
    } else if (nullptr != globalConstBuffer) {
        this->residencyContainer.push_back(globalConstBuffer);
    }

    if (NEO::isValidOffset(kernelDescriptor->payloadMappings.implicitArgs.globalVariablesSurfaceAddress.stateless)) {
        UNRECOVERABLE_IF(globalVarBuffer == nullptr);

        patchWithImplicitSurface(crossThreadDataArrayRef, surfaceStateHeapArrayRef,
                                 static_cast<uintptr_t>(globalVarBuffer->getGpuAddressToPatch()),
                                 *globalVarBuffer, kernelDescriptor->payloadMappings.implicitArgs.globalVariablesSurfaceAddress,
                                 *neoDevice, kernelDescriptor->kernelAttributes.flags.useGlobalAtomics, deviceImp->isImplicitScalingCapable());
        this->residencyContainer.push_back(globalVarBuffer);
    } else if (nullptr != globalVarBuffer) {
        this->residencyContainer.push_back(globalVarBuffer);
    }

    return ZE_RESULT_SUCCESS;
}

void KernelImmutableData::createRelocatedDebugData(NEO::GraphicsAllocation *globalConstBuffer,
                                                   NEO::GraphicsAllocation *globalVarBuffer) {
    NEO::Linker::SegmentInfo globalData;
    NEO::Linker::SegmentInfo constData;
    if (globalVarBuffer) {
        globalData.gpuAddress = globalVarBuffer->getGpuAddress();
        globalData.segmentSize = globalVarBuffer->getUnderlyingBufferSize();
    }
    if (globalConstBuffer) {
        constData.gpuAddress = globalConstBuffer->getGpuAddress();
        constData.segmentSize = globalConstBuffer->getUnderlyingBufferSize();
    }

    if (kernelInfo->kernelDescriptor.external.debugData.get()) {
        std::string outErrReason;
        std::string outWarning;
        auto decodedElf = NEO::Elf::decodeElf<NEO::Elf::EI_CLASS_64>(ArrayRef<const uint8_t>(reinterpret_cast<const uint8_t *>(kernelInfo->kernelDescriptor.external.debugData->vIsa),
                                                                                             kernelInfo->kernelDescriptor.external.debugData->vIsaSize),
                                                                     outErrReason, outWarning);

        if (decodedElf.getDebugInfoRelocations().size() > 1) {
            UNRECOVERABLE_IF(kernelInfo->kernelDescriptor.external.relocatedDebugData.get() != nullptr);

            auto size = kernelInfo->kernelDescriptor.external.debugData->vIsaSize;
            kernelInfo->kernelDescriptor.external.relocatedDebugData = std::make_unique<uint8_t[]>(size);

            memcpy_s(kernelInfo->kernelDescriptor.external.relocatedDebugData.get(), size, kernelInfo->kernelDescriptor.external.debugData->vIsa, kernelInfo->kernelDescriptor.external.debugData->vIsaSize);

            NEO::Linker::SegmentInfo textSegment = {getIsaGraphicsAllocation()->getGpuAddress(),
                                                    getIsaGraphicsAllocation()->getUnderlyingBufferSize()};

            NEO::Linker::applyDebugDataRelocations(decodedElf, ArrayRef<uint8_t>(kernelInfo->kernelDescriptor.external.relocatedDebugData.get(), size),
                                                   textSegment, globalData, constData);
        }
    }
}

NEO::GraphicsAllocation *KernelImmutableData::getIsaGraphicsAllocation() const {
    if (auto allocation = this->getIsaParentAllocation(); allocation != nullptr) {
        DEBUG_BREAK_IF(this->device->getL0Debugger() != nullptr);
        DEBUG_BREAK_IF(this->isaGraphicsAllocation != nullptr);
        return allocation;
    } else {
        DEBUG_BREAK_IF(this->isaGraphicsAllocation.get() == nullptr);
        return this->isaGraphicsAllocation.get();
    }
}

uint32_t KernelImmutableData::getIsaSize() const {
    if (this->getIsaParentAllocation()) {
        DEBUG_BREAK_IF(this->device->getL0Debugger() != nullptr);
        DEBUG_BREAK_IF(this->isaGraphicsAllocation != nullptr);
        return static_cast<uint32_t>(this->isaSubAllocationSize);
    } else {
        return static_cast<uint32_t>(this->isaGraphicsAllocation->getUnderlyingBufferSize());
    }
}

void KernelImmutableData::setIsaPerKernelAllocation(NEO::GraphicsAllocation *allocation) {
    DEBUG_BREAK_IF(this->isaParentAllocation != nullptr);
    this->isaGraphicsAllocation.reset(allocation);
}

ze_result_t KernelImp::getBaseAddress(uint64_t *baseAddress) {
    if (baseAddress) {
        auto gmmHelper = module->getDevice()->getNEODevice()->getGmmHelper();
        *baseAddress = gmmHelper->decanonize(this->kernelImmData->getIsaGraphicsAllocation()->getGpuAddress() +
                                             this->kernelImmData->getIsaOffsetInParentAllocation());
    }
    return ZE_RESULT_SUCCESS;
}

KernelImp::KernelImp(Module *module) : module(module) {}

KernelImp::~KernelImp() {
    if (nullptr != privateMemoryGraphicsAllocation) {
        module->getDevice()->getNEODevice()->getMemoryManager()->freeGraphicsMemory(privateMemoryGraphicsAllocation);
    }

    if (perThreadDataForWholeThreadGroup != nullptr) {
        alignedFree(perThreadDataForWholeThreadGroup);
    }
    if (printfBuffer != nullptr) {
        // not allowed to call virtual function on destructor, so calling printOutput directly
        PrintfHandler::printOutput(kernelImmData, this->printfBuffer, module->getDevice(), false);
        module->getDevice()->getNEODevice()->getMemoryManager()->freeGraphicsMemory(printfBuffer);
    }

    if (kernelImmData && kernelImmData->getDescriptor().kernelAttributes.flags.usesAssert && module &&
        module->getDevice()->getNEODevice()->getRootDeviceEnvironment().assertHandler.get()) {
        module->getDevice()->getNEODevice()->getRootDeviceEnvironment().assertHandler->printAssertAndAbort();
    }

    slmArgSizes.clear();
    crossThreadData.reset();
    surfaceStateHeapData.reset();
    dynamicStateHeapData.reset();
}

ze_result_t KernelImp::setArgumentValue(uint32_t argIndex, size_t argSize,
                                        const void *pArgValue) {
    if (argIndex >= kernelArgHandlers.size()) {
        return ZE_RESULT_ERROR_INVALID_ARGUMENT;
    }
    return (this->*kernelArgHandlers[argIndex])(argIndex, argSize, pArgValue);
}

void KernelImp::setGroupCount(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
    const NEO::KernelDescriptor &desc = kernelImmData->getDescriptor();
    uint32_t globalWorkSize[3] = {groupCountX * groupSize[0], groupCountY * groupSize[1],
                                  groupCountZ * groupSize[2]};
    auto dst = ArrayRef<uint8_t>(crossThreadData.get(), crossThreadDataSize);
    NEO::patchVecNonPointer(dst, desc.payloadMappings.dispatchTraits.globalWorkSize, globalWorkSize);

    uint32_t groupCount[3] = {groupCountX, groupCountY, groupCountZ};
    NEO::patchVecNonPointer(dst, desc.payloadMappings.dispatchTraits.numWorkGroups, groupCount);

    uint32_t workDim = 1;
    if (groupCountZ * groupSize[2] > 1) {
        workDim = 3;
    } else if (groupCountY * groupSize[1] > 1) {
        workDim = 2;
    }
    auto workDimOffset = desc.payloadMappings.dispatchTraits.workDim;
    if (NEO::isValidOffset(workDimOffset)) {
        auto destinationBuffer = ArrayRef<uint8_t>(crossThreadData.get(), crossThreadDataSize);
        NEO::patchNonPointer<uint32_t, uint32_t>(destinationBuffer, desc.payloadMappings.dispatchTraits.workDim, workDim);
    }

    if (pImplicitArgs) {
        pImplicitArgs->numWorkDim = workDim;

        pImplicitArgs->globalSizeX = globalWorkSize[0];
        pImplicitArgs->globalSizeY = globalWorkSize[1];
        pImplicitArgs->globalSizeZ = globalWorkSize[2];

        pImplicitArgs->groupCountX = groupCount[0];
        pImplicitArgs->groupCountY = groupCount[1];
        pImplicitArgs->groupCountZ = groupCount[2];
    }
}

ze_result_t KernelImp::setGroupSize(uint32_t groupSizeX, uint32_t groupSizeY,
                                    uint32_t groupSizeZ) {
    if ((0 == groupSizeX) || (0 == groupSizeY) || (0 == groupSizeZ)) {
        return ZE_RESULT_ERROR_INVALID_ARGUMENT;
    }

    if (this->groupSize[0] == groupSizeX &&
        this->groupSize[1] == groupSizeY &&
        this->groupSize[2] == groupSizeZ) {
        return ZE_RESULT_SUCCESS;
    }

    auto numChannels = kernelImmData->getDescriptor().kernelAttributes.numLocalIdChannels;
    Vec3<size_t> groupSize{groupSizeX, groupSizeY, groupSizeZ};
    auto itemsInGroup = Math::computeTotalElementsCount(groupSize);

    const NEO::KernelDescriptor &kernelDescriptor = kernelImmData->getDescriptor();
    if (auto maxGroupSize = module->getMaxGroupSize(kernelDescriptor); itemsInGroup > maxGroupSize) {
        NEO::printDebugString(NEO::debugManager.flags.PrintDebugMessages.get(), stderr,
                              "Requested work-group size (%lu) exceeds maximum value (%u) for the kernel \"%s\" \n",
                              itemsInGroup, maxGroupSize, kernelDescriptor.kernelMetadata.kernelName.c_str());
        DEBUG_BREAK_IF(true);
        return ZE_RESULT_ERROR_INVALID_GROUP_SIZE_DIMENSION;
    }

    this->groupSize[0] = groupSizeX;
    this->groupSize[1] = groupSizeY;
    this->groupSize[2] = groupSizeZ;
    for (uint32_t i = 0u; i < 3u; i++) {
        if (kernelDescriptor.kernelAttributes.requiredWorkgroupSize[i] != 0 &&
            kernelDescriptor.kernelAttributes.requiredWorkgroupSize[i] != this->groupSize[i]) {
            NEO::printDebugString(NEO::debugManager.flags.PrintDebugMessages.get(), stderr,
                                  "Invalid group size {%d, %d, %d} specified, requiredWorkGroupSize = {%d, %d, %d}\n",
                                  this->groupSize[0], this->groupSize[1], this->groupSize[2],
                                  kernelDescriptor.kernelAttributes.requiredWorkgroupSize[0],
                                  kernelDescriptor.kernelAttributes.requiredWorkgroupSize[1],
                                  kernelDescriptor.kernelAttributes.requiredWorkgroupSize[2]);
            return ZE_RESULT_ERROR_INVALID_GROUP_SIZE_DIMENSION;
        }
    }

    patchWorkgroupSizeInCrossThreadData(groupSizeX, groupSizeY, groupSizeZ);

    auto simdSize = kernelDescriptor.kernelAttributes.simdSize;
    auto remainderSimdLanes = itemsInGroup & (simdSize - 1u);
    threadExecutionMask = static_cast<uint32_t>(maxNBitValue(remainderSimdLanes));
    if (!threadExecutionMask) {
        threadExecutionMask = static_cast<uint32_t>(maxNBitValue((isSimd1(simdSize)) ? 32 : simdSize));
    }
    evaluateIfRequiresGenerationOfLocalIdsByRuntime(kernelDescriptor);

    auto grfSize = this->module->getDevice()->getHwInfo().capabilityTable.grfSize;
    auto &rootDeviceEnvironment = module->getDevice()->getNEODevice()->getRootDeviceEnvironment();
    auto &gfxCoreHelper = rootDeviceEnvironment.getHelper<NEO::GfxCoreHelper>();
    this->numThreadsPerThreadGroup = gfxCoreHelper.calculateNumThreadsPerThreadGroup(
        simdSize, static_cast<uint32_t>(itemsInGroup), grfSize, !kernelRequiresGenerationOfLocalIdsByRuntime);

    if (kernelRequiresGenerationOfLocalIdsByRuntime) {
        auto grfSize = this->module->getDevice()->getHwInfo().capabilityTable.grfSize;
        uint32_t perThreadDataSizeForWholeThreadGroupNeeded =
            static_cast<uint32_t>(NEO::PerThreadDataHelper::getPerThreadDataSizeTotal(
                simdSize, grfSize, numChannels, itemsInGroup, !kernelRequiresGenerationOfLocalIdsByRuntime, gfxCoreHelper));
        if (perThreadDataSizeForWholeThreadGroupNeeded >
            perThreadDataSizeForWholeThreadGroupAllocated) {
            alignedFree(perThreadDataForWholeThreadGroup);
            perThreadDataForWholeThreadGroup = static_cast<uint8_t *>(alignedMalloc(perThreadDataSizeForWholeThreadGroupNeeded, 32));
            perThreadDataSizeForWholeThreadGroupAllocated = perThreadDataSizeForWholeThreadGroupNeeded;
        }
        perThreadDataSizeForWholeThreadGroup = perThreadDataSizeForWholeThreadGroupNeeded;

        if (numChannels > 0) {
            UNRECOVERABLE_IF(3 != numChannels);
            NEO::generateLocalIDs(
                perThreadDataForWholeThreadGroup,
                static_cast<uint16_t>(simdSize),
                std::array<uint16_t, 3>{{static_cast<uint16_t>(groupSizeX),
                                         static_cast<uint16_t>(groupSizeY),
                                         static_cast<uint16_t>(groupSizeZ)}},
                std::array<uint8_t, 3>{{0, 1, 2}},
                false, grfSize, gfxCoreHelper);
        }

        this->perThreadDataSize = perThreadDataSizeForWholeThreadGroup / numThreadsPerThreadGroup;
    }
    return ZE_RESULT_SUCCESS;
}

ze_result_t KernelImp::suggestGroupSize(uint32_t globalSizeX, uint32_t globalSizeY,
                                        uint32_t globalSizeZ, uint32_t *groupSizeX,
                                        uint32_t *groupSizeY, uint32_t *groupSizeZ) {
    size_t retGroupSize[3] = {};
    const auto &kernelDescriptor = this->getImmutableData()->getDescriptor();
    auto maxWorkGroupSize = module->getMaxGroupSize(kernelDescriptor);
    auto simd = kernelDescriptor.kernelAttributes.simdSize;
    size_t workItems[3] = {globalSizeX, globalSizeY, globalSizeZ};
    uint32_t dim = (globalSizeY > 1U) ? 2 : 1U;
    dim = (globalSizeZ > 1U) ? 3 : dim;

    auto cachedGroupSize = std::find_if(this->suggestGroupSizeCache.begin(), this->suggestGroupSizeCache.end(), [&](const auto &other) {
        return other.groupSize == workItems &&
               other.slmArgsTotalSize == this->getSlmTotalSize();
    });
    if (cachedGroupSize != this->suggestGroupSizeCache.end()) {
        *groupSizeX = static_cast<uint32_t>(cachedGroupSize->suggestedGroupSize.x);
        *groupSizeY = static_cast<uint32_t>(cachedGroupSize->suggestedGroupSize.y);
        *groupSizeZ = static_cast<uint32_t>(cachedGroupSize->suggestedGroupSize.z);
        return ZE_RESULT_SUCCESS;
    }

    if (NEO::debugManager.flags.EnableComputeWorkSizeND.get()) {
        auto usesImages = kernelDescriptor.kernelAttributes.flags.usesImages;
        auto neoDevice = module->getDevice()->getNEODevice();
        const auto &deviceInfo = neoDevice->getDeviceInfo();
        uint32_t numThreadsPerSubSlice = (uint32_t)deviceInfo.maxNumEUsPerSubSlice * deviceInfo.numThreadsPerEU;
        uint32_t localMemSize = (uint32_t)deviceInfo.localMemSize;

        if (this->getSlmTotalSize() > 0 && localMemSize < this->getSlmTotalSize()) {
            const auto device = static_cast<DeviceImp *>(module->getDevice());
            const auto driverHandle = static_cast<DriverHandleImp *>(device->getDriverHandle());
            driverHandle->setErrorDescription("Size of SLM (%u) larger than available (%u)\n", this->getSlmTotalSize(), localMemSize);
            PRINT_DEBUG_STRING(NEO::debugManager.flags.PrintDebugMessages.get(), stderr, "Size of SLM (%u) larger than available (%u)\n", this->getSlmTotalSize(), localMemSize);
            return ZE_RESULT_ERROR_OUT_OF_DEVICE_MEMORY;
        }

        NEO::WorkSizeInfo wsInfo(maxWorkGroupSize, kernelDescriptor.kernelAttributes.usesBarriers(), simd, this->getSlmTotalSize(),
                                 neoDevice->getRootDeviceEnvironment(), numThreadsPerSubSlice, localMemSize,
                                 usesImages, false, kernelDescriptor.kernelAttributes.flags.requiresDisabledEUFusion);
        NEO::computeWorkgroupSizeND(wsInfo, retGroupSize, workItems, dim);
    } else {
        if (1U == dim) {
            NEO::computeWorkgroupSize1D(maxWorkGroupSize, retGroupSize, workItems, simd);
        } else if (NEO::debugManager.flags.EnableComputeWorkSizeSquared.get() && (2U == dim)) {
            NEO::computeWorkgroupSizeSquared(maxWorkGroupSize, retGroupSize, workItems, simd, dim);
        } else {
            NEO::computeWorkgroupSize2D(maxWorkGroupSize, retGroupSize, workItems, simd);
        }
    }
    *groupSizeX = static_cast<uint32_t>(retGroupSize[0]);
    *groupSizeY = static_cast<uint32_t>(retGroupSize[1]);
    *groupSizeZ = static_cast<uint32_t>(retGroupSize[2]);
    this->suggestGroupSizeCache.emplace_back(workItems, this->getSlmTotalSize(), retGroupSize);

    return ZE_RESULT_SUCCESS;
}

ze_result_t KernelImp::suggestMaxCooperativeGroupCount(uint32_t *totalGroupCount, NEO::EngineGroupType engineGroupType,
                                                       bool isEngineInstanced) {
    UNRECOVERABLE_IF(0 == groupSize[0]);
    UNRECOVERABLE_IF(0 == groupSize[1]);
    UNRECOVERABLE_IF(0 == groupSize[2]);

    auto &hardwareInfo = module->getDevice()->getHwInfo();

    auto dssCount = hardwareInfo.gtSystemInfo.DualSubSliceCount;
    if (dssCount == 0) {
        dssCount = hardwareInfo.gtSystemInfo.SubSliceCount;
    }

    auto &rootDeviceEnvironment = module->getDevice()->getNEODevice()->getRootDeviceEnvironment();
    auto &helper = rootDeviceEnvironment.getHelper<NEO::GfxCoreHelper>();
    auto &descriptor = kernelImmData->getDescriptor();
    auto availableThreadCount = helper.calculateAvailableThreadCount(hardwareInfo, descriptor.kernelAttributes.numGrfRequired);

    auto availableSlmSize = static_cast<uint32_t>(dssCount * KB * hardwareInfo.capabilityTable.slmSize);
    auto usedSlmSize = helper.alignSlmSize(slmArgsTotalSize + descriptor.kernelAttributes.slmInlineSize);
    auto maxBarrierCount = static_cast<uint32_t>(helper.getMaxBarrierRegisterPerSlice());
    auto barrierCount = descriptor.kernelAttributes.barrierCount;
    const uint32_t workDim = 3;
    const size_t localWorkSize[] = {groupSize[0], groupSize[1], groupSize[2]};

    *totalGroupCount = NEO::KernelHelper::getMaxWorkGroupCount(descriptor.kernelAttributes.simdSize,
                                                               availableThreadCount,
                                                               dssCount,
                                                               availableSlmSize,
                                                               usedSlmSize,
                                                               maxBarrierCount,
                                                               barrierCount,
                                                               workDim,
                                                               localWorkSize);
    *totalGroupCount = helper.adjustMaxWorkGroupCount(*totalGroupCount, engineGroupType, rootDeviceEnvironment, isEngineInstanced);
    return ZE_RESULT_SUCCESS;
}

ze_result_t KernelImp::setIndirectAccess(ze_kernel_indirect_access_flags_t flags) {
    if (NEO::debugManager.flags.DisableIndirectAccess.get() == 1) {
        return ZE_RESULT_SUCCESS;
    }

    if (flags & ZE_KERNEL_INDIRECT_ACCESS_FLAG_DEVICE) {
        this->unifiedMemoryControls.indirectDeviceAllocationsAllowed = true;
    }
    if (flags & ZE_KERNEL_INDIRECT_ACCESS_FLAG_HOST) {
        this->unifiedMemoryControls.indirectHostAllocationsAllowed = true;
    }
    if (flags & ZE_KERNEL_INDIRECT_ACCESS_FLAG_SHARED) {
        this->unifiedMemoryControls.indirectSharedAllocationsAllowed = true;
    }

    return ZE_RESULT_SUCCESS;
}

ze_result_t KernelImp::getIndirectAccess(ze_kernel_indirect_access_flags_t *flags) {
    *flags = 0;
    if (this->unifiedMemoryControls.indirectDeviceAllocationsAllowed) {
        *flags |= ZE_KERNEL_INDIRECT_ACCESS_FLAG_DEVICE;
    }
    if (this->unifiedMemoryControls.indirectHostAllocationsAllowed) {
        *flags |= ZE_KERNEL_INDIRECT_ACCESS_FLAG_HOST;
    }
    if (this->unifiedMemoryControls.indirectSharedAllocationsAllowed) {
        *flags |= ZE_KERNEL_INDIRECT_ACCESS_FLAG_SHARED;
    }

    return ZE_RESULT_SUCCESS;
}

ze_result_t KernelImp::getSourceAttributes(uint32_t *pSize, char **pString) {
    auto &desc = kernelImmData->getDescriptor();
    if (pString == nullptr) {
        *pSize = (uint32_t)desc.kernelMetadata.kernelLanguageAttributes.length() + 1;
    } else {
        strncpy_s(*pString, *pSize,
                  desc.kernelMetadata.kernelLanguageAttributes.c_str(),
                  desc.kernelMetadata.kernelLanguageAttributes.length());
    }
    return ZE_RESULT_SUCCESS;
}

ze_result_t KernelImp::setArgImmediate(uint32_t argIndex, size_t argSize, const void *argVal) {
    if (kernelImmData->getDescriptor().payloadMappings.explicitArgs.size() <= argIndex) {
        return ZE_RESULT_ERROR_INVALID_ARGUMENT;
    }

    const auto &arg = kernelImmData->getDescriptor().payloadMappings.explicitArgs[argIndex];

    for (const auto &element : arg.as<NEO::ArgDescValue>().elements) {
        if (element.sourceOffset < argSize) {
            size_t maxBytesToCopy = argSize - element.sourceOffset;
            size_t bytesToCopy = std::min(static_cast<size_t>(element.size), maxBytesToCopy);

            auto pDst = ptrOffset(crossThreadData.get(), element.offset);
            if (argVal) {
                auto pSrc = ptrOffset(argVal, element.sourceOffset);
                memcpy_s(pDst, element.size, pSrc, bytesToCopy);
            } else {
                uint64_t val = 0;
                memcpy_s(pDst, element.size,
                         reinterpret_cast<void *>(&val), bytesToCopy);
            }
        } else {
            return ZE_RESULT_ERROR_INVALID_ARGUMENT;
        }
    }
    return ZE_RESULT_SUCCESS;
}

ze_result_t KernelImp::setArgRedescribedImage(uint32_t argIndex, ze_image_handle_t argVal) {
    const auto &arg = kernelImmData->getDescriptor().payloadMappings.explicitArgs[argIndex].as<NEO::ArgDescImage>();
    if (argVal == nullptr) {
        residencyContainer[argIndex] = nullptr;
        return ZE_RESULT_SUCCESS;
    }

    const auto image = Image::fromHandle(argVal);

    if (kernelImmData->getDescriptor().kernelAttributes.imageAddressingMode == NEO::KernelDescriptor::Bindless) {

        NEO::BindlessHeapsHelper *bindlessHeapsHelper = this->module->getDevice()->getNEODevice()->getBindlessHeapsHelper();
        auto &gfxCoreHelper = this->module->getDevice()->getGfxCoreHelper();
        const auto surfaceStateSize = gfxCoreHelper.getRenderSurfaceStateSize();
        if (bindlessHeapsHelper) {

            if (image->allocateBindlessSlot() != ZE_RESULT_SUCCESS) {
                return ZE_RESULT_ERROR_OUT_OF_HOST_MEMORY;
            }

            auto ssInHeap = image->getBindlessSlot();
            auto patchLocation = ptrOffset(getCrossThreadData(), arg.bindless);
            // redescribed image's surface state is after image's state
            auto bindlessSlotOffset = ssInHeap->surfaceStateOffset + surfaceStateSize;
            auto patchValue = gfxCoreHelper.getBindlessSurfaceExtendedMessageDescriptorValue(static_cast<uint32_t>(bindlessSlotOffset));
            patchWithRequiredSize(const_cast<uint8_t *>(patchLocation), sizeof(patchValue), patchValue);

            image->copyRedescribedSurfaceStateToSSH(ptrOffset(ssInHeap->ssPtr, surfaceStateSize), 0u);
            isBindlessOffsetSet[argIndex] = true;
            this->residencyContainer.push_back(ssInHeap->heapAllocation);
        } else {
            usingSurfaceStateHeap[argIndex] = true;
            auto ssPtr = ptrOffset(surfaceStateHeapData.get(), getSurfaceStateIndexForBindlessOffset(arg.bindless) * surfaceStateSize);
            image->copyRedescribedSurfaceStateToSSH(ssPtr, 0u);
        }
    } else {
        image->copyRedescribedSurfaceStateToSSH(surfaceStateHeapData.get(), arg.bindful);
    }
    residencyContainer[argIndex] = image->getAllocation();

    return ZE_RESULT_SUCCESS;
}

ze_result_t KernelImp::setArgBufferWithAlloc(uint32_t argIndex, uintptr_t argVal, NEO::GraphicsAllocation *allocation, NEO::SvmAllocationData *peerAllocData) {
    const auto &arg = kernelImmData->getDescriptor().payloadMappings.explicitArgs[argIndex].as<NEO::ArgDescPointer>();
    const auto val = argVal;

    NEO::patchPointer(ArrayRef<uint8_t>(crossThreadData.get(), crossThreadDataSize), arg, val);
    if (NEO::isValidOffset(arg.bindful) || NEO::isValidOffset(arg.bindless)) {

        if (NEO::isValidOffset(arg.bindless)) {
            if (!this->module->getDevice()->getNEODevice()->getMemoryManager()->allocateBindlessSlot(allocation)) {
                return ZE_RESULT_ERROR_OUT_OF_HOST_MEMORY;
            }
        }

        setBufferSurfaceState(argIndex, reinterpret_cast<void *>(val), allocation);
    }
    NEO::SvmAllocationData *allocData = nullptr;
    if (peerAllocData) {
        allocData = peerAllocData;
    } else {
        allocData = this->module->getDevice()->getDriverHandle()->getSvmAllocsManager()->getSVMAlloc(reinterpret_cast<void *>(allocation->getGpuAddress()));
    }
    if (allocData) {
        bool argWasUncacheable = isArgUncached[argIndex];
        bool argIsUncacheable = allocData->allocationFlagsProperty.flags.locallyUncachedResource;
        if (argWasUncacheable == false && argIsUncacheable) {
            kernelRequiresUncachedMocsCount++;
        } else if (argWasUncacheable && argIsUncacheable == false) {
            kernelRequiresUncachedMocsCount--;
        }
        this->setKernelArgUncached(argIndex, argIsUncacheable);
    }
    residencyContainer[argIndex] = allocation;

    return ZE_RESULT_SUCCESS;
}

ze_result_t KernelImp::setArgUnknown(uint32_t argIndex, size_t argSize, const void *argVal) {
    return ZE_RESULT_SUCCESS;
}

ze_result_t KernelImp::setArgBuffer(uint32_t argIndex, size_t argSize, const void *argVal) {
    const auto device = static_cast<DeviceImp *>(this->module->getDevice());
    const auto driverHandle = static_cast<DriverHandleImp *>(device->getDriverHandle());
    const auto svmAllocsManager = driverHandle->getSvmAllocsManager();
    const auto allocationsCounter = svmAllocsManager->allocationsCounter.load();
    const auto &argInfo = this->kernelArgInfos[argIndex];
    NEO::SvmAllocationData *allocData = nullptr;
    if (argVal != nullptr) {
        const auto requestedAddress = *reinterpret_cast<void *const *>(argVal);
        if (argInfo.allocId > 0 &&
            argInfo.allocId < NEO::SvmAllocationData::uninitializedAllocId &&
            requestedAddress == argInfo.value) {
            bool reuseFromCache = false;
            if (allocationsCounter > 0) {
                if (allocationsCounter == argInfo.allocIdMemoryManagerCounter) {
                    reuseFromCache = true;
                } else {
                    allocData = svmAllocsManager->getSVMAlloc(requestedAddress);
                    if (allocData && allocData->getAllocId() == argInfo.allocId) {
                        reuseFromCache = true;
                        this->kernelArgInfos[argIndex].allocIdMemoryManagerCounter = allocationsCounter;
                    }
                }
                if (reuseFromCache) {
                    return ZE_RESULT_SUCCESS;
                }
            }
        }
    } else {
        if (argInfo.isSetToNullptr) {
            return ZE_RESULT_SUCCESS;
        }
    }

    const auto &allArgs = kernelImmData->getDescriptor().payloadMappings.explicitArgs;
    const auto &currArg = allArgs[argIndex];
    if (currArg.getTraits().getAddressQualifier() == NEO::KernelArgMetadata::AddrLocal) {
        slmArgSizes[argIndex] = static_cast<uint32_t>(argSize);
        kernelArgInfos[argIndex] = KernelArgInfo{nullptr, 0, 0, false};
        UNRECOVERABLE_IF(NEO::isUndefinedOffset(currArg.as<NEO::ArgDescPointer>().slmOffset));
        auto slmOffset = *reinterpret_cast<uint32_t *>(crossThreadData.get() + currArg.as<NEO::ArgDescPointer>().slmOffset);
        slmOffset += static_cast<uint32_t>(argSize);
        ++argIndex;
        while (argIndex < kernelImmData->getDescriptor().payloadMappings.explicitArgs.size()) {
            if (allArgs[argIndex].getTraits().getAddressQualifier() != NEO::KernelArgMetadata::AddrLocal) {
                ++argIndex;
                continue;
            }
            const auto &nextArg = allArgs[argIndex].as<NEO::ArgDescPointer>();
            UNRECOVERABLE_IF(0 == nextArg.requiredSlmAlignment);
            slmOffset = alignUp<uint32_t>(slmOffset, nextArg.requiredSlmAlignment);
            NEO::patchNonPointer<uint32_t, uint32_t>(ArrayRef<uint8_t>(crossThreadData.get(), crossThreadDataSize), nextArg.slmOffset, slmOffset);

            slmOffset += static_cast<uint32_t>(slmArgSizes[argIndex]);
            ++argIndex;
        }
        slmArgsTotalSize = static_cast<uint32_t>(alignUp(slmOffset, KB));
        return ZE_RESULT_SUCCESS;
    }

    if (nullptr == argVal) {
        residencyContainer[argIndex] = nullptr;
        const auto &arg = kernelImmData->getDescriptor().payloadMappings.explicitArgs[argIndex].as<NEO::ArgDescPointer>();
        uintptr_t nullBufferValue = 0;
        NEO::patchPointer(ArrayRef<uint8_t>(crossThreadData.get(), crossThreadDataSize), arg, nullBufferValue);
        kernelArgInfos[argIndex] = KernelArgInfo{nullptr, 0, 0, true};
        return ZE_RESULT_SUCCESS;
    }
    const auto requestedAddress = *reinterpret_cast<void *const *>(argVal);
    uintptr_t gpuAddress = 0u;
    NEO::GraphicsAllocation *alloc = driverHandle->getDriverSystemMemoryAllocation(requestedAddress,
                                                                                   1u,
                                                                                   module->getDevice()->getRootDeviceIndex(),
                                                                                   &gpuAddress);
    if (allocData == nullptr) {
        allocData = svmAllocsManager->getSVMAlloc(requestedAddress);
    }
    NEO::SvmAllocationData *peerAllocData = nullptr;
    if (allocData && driverHandle->isRemoteResourceNeeded(requestedAddress, alloc, allocData, device)) {

        uint64_t pbase = allocData->gpuAllocations.getDefaultGraphicsAllocation()->getGpuAddress();
        uint64_t offset = (uint64_t)requestedAddress - pbase;
        alloc = driverHandle->getPeerAllocation(device, allocData, reinterpret_cast<void *>(pbase), &gpuAddress, &peerAllocData);
        if (alloc == nullptr) {
            return ZE_RESULT_ERROR_INVALID_ARGUMENT;
        }
        gpuAddress += offset;
    }

    if (allocData == nullptr) {
        if (NEO::debugManager.flags.DisableSystemPointerKernelArgument.get() != 1) {
            const auto &argAsPtr = kernelImmData->getDescriptor().payloadMappings.explicitArgs[argIndex].as<NEO::ArgDescPointer>();
            auto patchLocation = ptrOffset(getCrossThreadData(), argAsPtr.stateless);
            patchWithRequiredSize(const_cast<uint8_t *>(patchLocation), argAsPtr.pointerSize, reinterpret_cast<uintptr_t>(requestedAddress));
            kernelArgInfos[argIndex] = KernelArgInfo{requestedAddress, 0, 0, false};
            return ZE_RESULT_SUCCESS;
        } else {
            return ZE_RESULT_ERROR_INVALID_ARGUMENT;
        }
    }

    const uint32_t allocId = allocData->getAllocId();
    kernelArgInfos[argIndex] = KernelArgInfo{requestedAddress, allocId, allocationsCounter, false};

    if (allocData->virtualReservationData) {
        for (const auto &mappedAllocationData : allocData->virtualReservationData->mappedAllocations) {
            // Add additional allocations to the residency container if the virtual reservation spans multiple allocations.
            if (requestedAddress != mappedAllocationData.second->ptr) {
                this->residencyContainer.push_back(mappedAllocationData.second->mappedAllocation->allocation);
            }
        }
    }

    return setArgBufferWithAlloc(argIndex, gpuAddress, alloc, peerAllocData);
}

ze_result_t KernelImp::setArgImage(uint32_t argIndex, size_t argSize, const void *argVal) {
    if (argVal == nullptr) {
        residencyContainer[argIndex] = nullptr;
        return ZE_RESULT_SUCCESS;
    }

    const auto &hwInfo = module->getDevice()->getNEODevice()->getHardwareInfo();
    auto isMediaBlockImage = (hwInfo.capabilityTable.supportsMediaBlock &&
                              kernelImmData->getDescriptor().payloadMappings.explicitArgs[argIndex].getExtendedTypeInfo().isMediaBlockImage);
    const auto &arg = kernelImmData->getDescriptor().payloadMappings.explicitArgs[argIndex].as<NEO::ArgDescImage>();
    const auto image = Image::fromHandle(*static_cast<const ze_image_handle_t *>(argVal));

    if (kernelImmData->getDescriptor().kernelAttributes.imageAddressingMode == NEO::KernelDescriptor::Bindless) {

        NEO::BindlessHeapsHelper *bindlessHeapsHelper = this->module->getDevice()->getNEODevice()->getBindlessHeapsHelper();
        auto &gfxCoreHelper = this->module->getDevice()->getNEODevice()->getRootDeviceEnvironmentRef().getHelper<NEO::GfxCoreHelper>();
        auto surfaceStateSize = gfxCoreHelper.getRenderSurfaceStateSize();
        if (bindlessHeapsHelper) {

            if (image->allocateBindlessSlot() != ZE_RESULT_SUCCESS) {
                return ZE_RESULT_ERROR_OUT_OF_HOST_MEMORY;
            }

            auto ssInHeap = image->getBindlessSlot();
            auto patchLocation = ptrOffset(getCrossThreadData(), arg.bindless);
            auto bindlessSlotOffset = ssInHeap->surfaceStateOffset;
            auto patchValue = gfxCoreHelper.getBindlessSurfaceExtendedMessageDescriptorValue(static_cast<uint32_t>(bindlessSlotOffset));
            patchWithRequiredSize(const_cast<uint8_t *>(patchLocation), sizeof(patchValue), patchValue);

            image->copySurfaceStateToSSH(ssInHeap->ssPtr, 0u, isMediaBlockImage);
            isBindlessOffsetSet[argIndex] = true;
            this->residencyContainer.push_back(ssInHeap->heapAllocation);
        } else {
            usingSurfaceStateHeap[argIndex] = true;
            auto ssPtr = ptrOffset(surfaceStateHeapData.get(), getSurfaceStateIndexForBindlessOffset(arg.bindless) * surfaceStateSize);
            image->copySurfaceStateToSSH(ssPtr, 0u, isMediaBlockImage);
        }
    } else {
        image->copySurfaceStateToSSH(surfaceStateHeapData.get(), arg.bindful, isMediaBlockImage);
    }

    residencyContainer[argIndex] = image->getAllocation();

    auto imageInfo = image->getImageInfo();
    auto clChannelType = getClChannelDataType(image->getImageDesc().format);
    auto clChannelOrder = getClChannelOrder(image->getImageDesc().format);

    // If the Module was built from a SPIRv, then the supported channel data type must be in the CL types otherwise it is unsupported.
    ModuleImp *moduleImp = reinterpret_cast<ModuleImp *>(this->module);
    if (moduleImp->isSPIRv()) {
        if (static_cast<int>(clChannelType) == CL_INVALID_VALUE) {
            return ZE_RESULT_ERROR_UNSUPPORTED_IMAGE_FORMAT;
        }
    }
    NEO::patchNonPointer<uint32_t, size_t>(ArrayRef<uint8_t>(crossThreadData.get(), crossThreadDataSize), arg.metadataPayload.imgWidth, imageInfo.imgDesc.imageWidth);
    NEO::patchNonPointer<uint32_t, size_t>(ArrayRef<uint8_t>(crossThreadData.get(), crossThreadDataSize), arg.metadataPayload.imgHeight, imageInfo.imgDesc.imageHeight);
    NEO::patchNonPointer<uint32_t, size_t>(ArrayRef<uint8_t>(crossThreadData.get(), crossThreadDataSize), arg.metadataPayload.imgDepth, imageInfo.imgDesc.imageDepth);
    NEO::patchNonPointer<uint32_t, uint32_t>(ArrayRef<uint8_t>(crossThreadData.get(), crossThreadDataSize), arg.metadataPayload.numSamples, imageInfo.imgDesc.numSamples);
    NEO::patchNonPointer<uint32_t, size_t>(ArrayRef<uint8_t>(crossThreadData.get(), crossThreadDataSize), arg.metadataPayload.arraySize, imageInfo.imgDesc.imageArraySize);
    NEO::patchNonPointer<cl_channel_type, cl_channel_type>(ArrayRef<uint8_t>(crossThreadData.get(), crossThreadDataSize), arg.metadataPayload.channelDataType, clChannelType);
    NEO::patchNonPointer<cl_channel_order, cl_channel_order>(ArrayRef<uint8_t>(crossThreadData.get(), crossThreadDataSize), arg.metadataPayload.channelOrder, clChannelOrder);
    NEO::patchNonPointer<uint32_t, uint32_t>(ArrayRef<uint8_t>(crossThreadData.get(), crossThreadDataSize), arg.metadataPayload.numMipLevels, imageInfo.imgDesc.numMipLevels);

    auto pixelSize = imageInfo.surfaceFormat->imageElementSizeInBytes;
    NEO::patchNonPointer<uint64_t, uint64_t>(ArrayRef<uint8_t>(crossThreadData.get(), crossThreadDataSize), arg.metadataPayload.flatBaseOffset, image->getAllocation()->getGpuAddress());
    NEO::patchNonPointer<uint32_t, size_t>(ArrayRef<uint8_t>(crossThreadData.get(), crossThreadDataSize), arg.metadataPayload.flatWidth, (imageInfo.imgDesc.imageWidth * pixelSize) - 1u);
    NEO::patchNonPointer<uint32_t, size_t>(ArrayRef<uint8_t>(crossThreadData.get(), crossThreadDataSize), arg.metadataPayload.flatHeight, (imageInfo.imgDesc.imageHeight * pixelSize) - 1u);
    NEO::patchNonPointer<uint32_t, size_t>(ArrayRef<uint8_t>(crossThreadData.get(), crossThreadDataSize), arg.metadataPayload.flatPitch, imageInfo.imgDesc.imageRowPitch - 1u);

    return ZE_RESULT_SUCCESS;
}

ze_result_t KernelImp::setArgSampler(uint32_t argIndex, size_t argSize, const void *argVal) {
    const auto &arg = kernelImmData->getDescriptor().payloadMappings.explicitArgs[argIndex].as<NEO::ArgDescSampler>();
    const auto sampler = Sampler::fromHandle(*static_cast<const ze_sampler_handle_t *>(argVal));
    sampler->copySamplerStateToDSH(dynamicStateHeapData.get(), dynamicStateHeapDataSize, arg.bindful);

    auto samplerDesc = sampler->getSamplerDesc();

    NEO::patchNonPointer<uint32_t, uint32_t>(ArrayRef<uint8_t>(crossThreadData.get(), crossThreadDataSize), arg.metadataPayload.samplerSnapWa, (samplerDesc.addressMode == ZE_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER && samplerDesc.filterMode == ZE_SAMPLER_FILTER_MODE_NEAREST) ? std::numeric_limits<uint32_t>::max() : 0u);
    NEO::patchNonPointer<uint32_t, uint32_t>(ArrayRef<uint8_t>(crossThreadData.get(), crossThreadDataSize), arg.metadataPayload.samplerAddressingMode, static_cast<uint32_t>(getAddrMode(samplerDesc.addressMode)));
    NEO::patchNonPointer<uint32_t, uint32_t>(ArrayRef<uint8_t>(crossThreadData.get(), crossThreadDataSize), arg.metadataPayload.samplerNormalizedCoords, samplerDesc.isNormalized ? static_cast<uint32_t>(SamplerPatchValues::NormalizedCoordsTrue) : static_cast<uint32_t>(SamplerPatchValues::NormalizedCoordsFalse));

    return ZE_RESULT_SUCCESS;
}

ze_result_t KernelImp::getKernelName(size_t *pSize, char *pName) {
    size_t kernelNameSize = this->kernelImmData->getDescriptor().kernelMetadata.kernelName.size() + 1;
    if (0 == *pSize || nullptr == pName) {
        *pSize = kernelNameSize;
        return ZE_RESULT_SUCCESS;
    }

    *pSize = std::min(*pSize, kernelNameSize);
    strncpy_s(pName, *pSize,
              this->kernelImmData->getDescriptor().kernelMetadata.kernelName.c_str(),
              this->kernelImmData->getDescriptor().kernelMetadata.kernelName.size());

    return ZE_RESULT_SUCCESS;
}

ze_result_t KernelImp::getProperties(ze_kernel_properties_t *pKernelProperties) {
    const auto &kernelDescriptor = this->kernelImmData->getDescriptor();
    pKernelProperties->numKernelArgs = static_cast<uint32_t>(kernelDescriptor.payloadMappings.explicitArgs.size());
    pKernelProperties->requiredGroupSizeX = kernelDescriptor.kernelAttributes.requiredWorkgroupSize[0];
    pKernelProperties->requiredGroupSizeY = kernelDescriptor.kernelAttributes.requiredWorkgroupSize[1];
    pKernelProperties->requiredGroupSizeZ = kernelDescriptor.kernelAttributes.requiredWorkgroupSize[2];
    pKernelProperties->requiredNumSubGroups = kernelDescriptor.kernelMetadata.compiledSubGroupsNumber;
    pKernelProperties->requiredSubgroupSize = kernelDescriptor.kernelMetadata.requiredSubGroupSize;
    pKernelProperties->maxSubgroupSize = kernelDescriptor.kernelAttributes.simdSize;
    pKernelProperties->localMemSize = kernelDescriptor.kernelAttributes.slmInlineSize;
    pKernelProperties->privateMemSize = kernelDescriptor.kernelAttributes.perHwThreadPrivateMemorySize;
    pKernelProperties->spillMemSize = kernelDescriptor.kernelAttributes.perThreadScratchSize[0];
    memset(pKernelProperties->uuid.kid, 0, ZE_MAX_KERNEL_UUID_SIZE);
    memset(pKernelProperties->uuid.mid, 0, ZE_MAX_MODULE_UUID_SIZE);

    const auto &gfxCoreHelper = this->module->getDevice()->getGfxCoreHelper();
    uint32_t maxKernelWorkGroupSize = static_cast<uint32_t>(this->module->getMaxGroupSize(kernelDescriptor));

    maxKernelWorkGroupSize = gfxCoreHelper.adjustMaxWorkGroupSize(kernelDescriptor.kernelAttributes.numGrfRequired, kernelDescriptor.kernelAttributes.simdSize, !kernelRequiresGenerationOfLocalIdsByRuntime, maxKernelWorkGroupSize);
    pKernelProperties->maxNumSubgroups = maxKernelWorkGroupSize / kernelDescriptor.kernelAttributes.simdSize;

    void *pNext = pKernelProperties->pNext;
    while (pNext) {
        ze_base_desc_t *extendedProperties = reinterpret_cast<ze_base_desc_t *>(pNext);
        if (extendedProperties->stype == ZE_STRUCTURE_TYPE_KERNEL_PREFERRED_GROUP_SIZE_PROPERTIES) {
            ze_kernel_preferred_group_size_properties_t *preferredGroupSizeProperties =
                reinterpret_cast<ze_kernel_preferred_group_size_properties_t *>(extendedProperties);

            preferredGroupSizeProperties->preferredMultiple = this->kernelImmData->getKernelInfo()->getMaxSimdSize();
            if (gfxCoreHelper.isFusedEuDispatchEnabled(this->module->getDevice()->getHwInfo(), kernelDescriptor.kernelAttributes.flags.requiresDisabledEUFusion)) {
                preferredGroupSizeProperties->preferredMultiple *= 2;
            }
        }
        getExtendedKernelProperties(extendedProperties);

        pNext = const_cast<void *>(extendedProperties->pNext);
    }

    return ZE_RESULT_SUCCESS;
}

NEO::GraphicsAllocation *KernelImp::allocatePrivateMemoryGraphicsAllocation() {
    auto &kernelAttributes = kernelImmData->getDescriptor().kernelAttributes;
    auto neoDevice = module->getDevice()->getNEODevice();

    auto privateSurfaceSize = NEO::KernelHelper::getPrivateSurfaceSize(kernelAttributes.perHwThreadPrivateMemorySize,
                                                                       neoDevice->getDeviceInfo().computeUnitsUsedForScratch);

    UNRECOVERABLE_IF(privateSurfaceSize == 0);
    auto privateMemoryGraphicsAllocation = neoDevice->getMemoryManager()->allocateGraphicsMemoryWithProperties(
        {neoDevice->getRootDeviceIndex(), privateSurfaceSize, NEO::AllocationType::PRIVATE_SURFACE, neoDevice->getDeviceBitfield()});

    UNRECOVERABLE_IF(privateMemoryGraphicsAllocation == nullptr);
    return privateMemoryGraphicsAllocation;
}

void KernelImp::patchCrossthreadDataWithPrivateAllocation(NEO::GraphicsAllocation *privateAllocation) {
    auto &kernelAttributes = kernelImmData->getDescriptor().kernelAttributes;
    auto device = module->getDevice();

    ArrayRef<uint8_t> crossThreadDataArrayRef = ArrayRef<uint8_t>(this->crossThreadData.get(), this->crossThreadDataSize);
    ArrayRef<uint8_t> surfaceStateHeapArrayRef = ArrayRef<uint8_t>(this->surfaceStateHeapData.get(), this->surfaceStateHeapDataSize);

    patchWithImplicitSurface(crossThreadDataArrayRef, surfaceStateHeapArrayRef,
                             static_cast<uintptr_t>(privateAllocation->getGpuAddressToPatch()),
                             *privateAllocation, kernelImmData->getDescriptor().payloadMappings.implicitArgs.privateMemoryAddress,
                             *device->getNEODevice(), kernelAttributes.flags.useGlobalAtomics, device->isImplicitScalingCapable());
}

void KernelImp::setInlineSamplers() {
    auto device = module->getDevice();
    const auto productFamily = device->getNEODevice()->getHardwareInfo().platform.eProductFamily;
    for (auto &inlineSampler : getKernelDescriptor().inlineSamplers) {
        ze_sampler_desc_t samplerDesc = {};
        samplerDesc.addressMode = static_cast<ze_sampler_address_mode_t>(inlineSampler.addrMode);
        samplerDesc.filterMode = static_cast<ze_sampler_filter_mode_t>(inlineSampler.filterMode);
        samplerDesc.isNormalized = inlineSampler.isNormalized;

        auto sampler = std::unique_ptr<L0::Sampler>(L0::Sampler::create(productFamily, device, &samplerDesc));
        UNRECOVERABLE_IF(sampler.get() == nullptr);
        sampler->copySamplerStateToDSH(dynamicStateHeapData.get(), dynamicStateHeapDataSize, inlineSampler.getSamplerBindfulOffset());
    }
}

ze_result_t KernelImp::initialize(const ze_kernel_desc_t *desc) {
    this->kernelImmData = module->getKernelImmutableData(desc->pKernelName);
    if (this->kernelImmData == nullptr) {
        return ZE_RESULT_ERROR_INVALID_KERNEL_NAME;
    }

    auto isaAllocation = this->kernelImmData->getIsaGraphicsAllocation();

    auto neoDevice = module->getDevice()->getNEODevice();
    const auto &productHelper = neoDevice->getProductHelper();
    auto &kernelDescriptor = kernelImmData->getDescriptor();
    auto ret = NEO::KernelHelper::checkIfThereIsSpaceForScratchOrPrivate(kernelDescriptor.kernelAttributes, neoDevice);
    if (ret == NEO::KernelHelper::ErrorCode::INVALID_KERNEL) {
        return ZE_RESULT_ERROR_INVALID_NATIVE_BINARY;
    }
    if (ret == NEO::KernelHelper::ErrorCode::OUT_OF_DEVICE_MEMORY) {
        return ZE_RESULT_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    UNRECOVERABLE_IF(!this->kernelImmData->getKernelInfo()->heapInfo.pKernelHeap);

    if (isaAllocation->getAllocationType() == NEO::AllocationType::KERNEL_ISA_INTERNAL && this->kernelImmData->getIsaParentAllocation() == nullptr) {
        isaAllocation->setTbxWritable(true, std::numeric_limits<uint32_t>::max());
        isaAllocation->setAubWritable(true, std::numeric_limits<uint32_t>::max());
        NEO::MemoryTransferHelper::transferMemoryToAllocation(productHelper.isBlitCopyRequiredForLocalMemory(neoDevice->getRootDeviceEnvironment(), *isaAllocation),
                                                              *neoDevice,
                                                              isaAllocation,
                                                              this->kernelImmData->getIsaOffsetInParentAllocation(),
                                                              this->kernelImmData->getKernelInfo()->heapInfo.pKernelHeap,
                                                              static_cast<size_t>(this->kernelImmData->getKernelInfo()->heapInfo.kernelHeapSize));
    }

    for (const auto &argT : kernelDescriptor.payloadMappings.explicitArgs) {
        switch (argT.type) {
        default:
            this->kernelArgHandlers.push_back(&KernelImp::setArgUnknown);
            break;
        case NEO::ArgDescriptor::ArgTPointer:
            this->kernelArgHandlers.push_back(&KernelImp::setArgBuffer);
            break;
        case NEO::ArgDescriptor::ArgTImage:
            this->kernelArgHandlers.push_back(&KernelImp::setArgImage);
            break;
        case NEO::ArgDescriptor::ArgTSampler:
            this->kernelArgHandlers.push_back(&KernelImp::setArgSampler);
            break;
        case NEO::ArgDescriptor::ArgTValue:
            this->kernelArgHandlers.push_back(&KernelImp::setArgImmediate);
            break;
        }
    }

    slmArgSizes.resize(this->kernelArgHandlers.size(), 0);
    kernelArgInfos.resize(this->kernelArgHandlers.size(), {});
    isArgUncached.resize(this->kernelArgHandlers.size(), 0);
    isBindlessOffsetSet.resize(this->kernelArgHandlers.size(), 0);
    usingSurfaceStateHeap.resize(this->kernelArgHandlers.size(), 0);

    if (kernelImmData->getSurfaceStateHeapSize() > 0) {
        this->surfaceStateHeapData.reset(new uint8_t[kernelImmData->getSurfaceStateHeapSize()]);
        memcpy_s(this->surfaceStateHeapData.get(),
                 kernelImmData->getSurfaceStateHeapSize(),
                 kernelImmData->getSurfaceStateHeapTemplate(),
                 kernelImmData->getSurfaceStateHeapSize());
        this->surfaceStateHeapDataSize = kernelImmData->getSurfaceStateHeapSize();
    }

    if (kernelDescriptor.kernelAttributes.crossThreadDataSize != 0) {
        this->crossThreadData.reset(new uint8_t[kernelDescriptor.kernelAttributes.crossThreadDataSize]);
        memcpy_s(this->crossThreadData.get(),
                 kernelDescriptor.kernelAttributes.crossThreadDataSize,
                 kernelImmData->getCrossThreadDataTemplate(),
                 kernelDescriptor.kernelAttributes.crossThreadDataSize);
        this->crossThreadDataSize = kernelDescriptor.kernelAttributes.crossThreadDataSize;
    }

    if (kernelImmData->getDynamicStateHeapDataSize() != 0) {
        this->dynamicStateHeapData.reset(new uint8_t[kernelImmData->getDynamicStateHeapDataSize()]);
        memcpy_s(this->dynamicStateHeapData.get(),
                 kernelImmData->getDynamicStateHeapDataSize(),
                 kernelImmData->getDynamicStateHeapTemplate(),
                 kernelImmData->getDynamicStateHeapDataSize());
        this->dynamicStateHeapDataSize = kernelImmData->getDynamicStateHeapDataSize();
    }

    if (kernelDescriptor.kernelAttributes.flags.requiresImplicitArgs) {
        pImplicitArgs = std::make_unique<NEO::ImplicitArgs>();
        *pImplicitArgs = {};
        pImplicitArgs->structSize = offsetof(NEO::ImplicitArgs, reserved);
        pImplicitArgs->structVersion = 0;
        pImplicitArgs->simdWidth = kernelDescriptor.kernelAttributes.simdSize;
    }

    if (kernelDescriptor.kernelAttributes.requiredWorkgroupSize[0] > 0) {
        auto *reqdSize = kernelDescriptor.kernelAttributes.requiredWorkgroupSize;
        UNRECOVERABLE_IF(reqdSize[1] == 0);
        UNRECOVERABLE_IF(reqdSize[2] == 0);
        auto result = setGroupSize(reqdSize[0], reqdSize[1], reqdSize[2]);
        if (result != ZE_RESULT_SUCCESS) {
            return result;
        }
    } else {
        auto result = setGroupSize(kernelDescriptor.kernelAttributes.simdSize, 1, 1);
        if (result != ZE_RESULT_SUCCESS) {
            return result;
        }
    }

    residencyContainer.resize(this->kernelArgHandlers.size(), nullptr);

    auto &kernelAttributes = kernelDescriptor.kernelAttributes;
    if ((kernelAttributes.perHwThreadPrivateMemorySize != 0U) && (false == module->shouldAllocatePrivateMemoryPerDispatch())) {
        this->privateMemoryGraphicsAllocation = allocatePrivateMemoryGraphicsAllocation();
        this->patchCrossthreadDataWithPrivateAllocation(this->privateMemoryGraphicsAllocation);
        this->residencyContainer.push_back(this->privateMemoryGraphicsAllocation);
    }

    this->createPrintfBuffer();

    this->setInlineSamplers();

    this->setAssertBuffer();

    residencyContainer.insert(residencyContainer.end(), kernelImmData->getResidencyContainer().begin(),
                              kernelImmData->getResidencyContainer().end());
    ModuleImp *moduleImp = reinterpret_cast<ModuleImp *>(this->module);
    bool detectIndirectAccessInKernel = productHelper.isDetectIndirectAccessInKernelSupported(kernelDescriptor, moduleImp->isPrecompiled());
    if (NEO::debugManager.flags.DetectIndirectAccessInKernel.get() != -1) {
        detectIndirectAccessInKernel = NEO::debugManager.flags.DetectIndirectAccessInKernel.get() == 1;
    }
    if (detectIndirectAccessInKernel) {
        kernelHasIndirectAccess = kernelDescriptor.kernelAttributes.hasNonKernelArgLoad ||
                                  kernelDescriptor.kernelAttributes.hasNonKernelArgStore ||
                                  kernelDescriptor.kernelAttributes.hasNonKernelArgAtomic ||
                                  kernelDescriptor.kernelAttributes.hasIndirectStatelessAccess ||
                                  NEO::KernelHelper::isAnyArgumentPtrByValue(kernelDescriptor);
    } else {
        kernelHasIndirectAccess = true;
    }

    if (this->usesRayTracing()) {
        uint32_t bvhLevels = NEO::RayTracingHelper::maxBvhLevels;
        auto arg = this->getImmutableData()->getDescriptor().payloadMappings.implicitArgs.rtDispatchGlobals;
        neoDevice->initializeRayTracing(bvhLevels);

        auto rtDispatchGlobalsInfo = neoDevice->getRTDispatchGlobals(bvhLevels);
        if (rtDispatchGlobalsInfo == nullptr) {
            return ZE_RESULT_ERROR_OUT_OF_HOST_MEMORY;
        }

        for (auto rtStack : rtDispatchGlobalsInfo->rtStacks) {
            this->residencyContainer.push_back(rtStack);
        }

        auto address = rtDispatchGlobalsInfo->rtDispatchGlobalsArray->getGpuAddressToPatch();
        if (NEO::isValidOffset(arg.stateless)) {
            NEO::patchPointer(ArrayRef<uint8_t>(crossThreadData.get(), crossThreadDataSize),
                              arg,
                              static_cast<uintptr_t>(address));
        }
        if (this->pImplicitArgs) {
            pImplicitArgs->rtGlobalBufferPtr = address;
        }

        this->residencyContainer.push_back(rtDispatchGlobalsInfo->rtDispatchGlobalsArray);
    }
    this->midThreadPreemptionDisallowedForRayTracingKernels = productHelper.isMidThreadPreemptionDisallowedForRayTracingKernels();
    return ZE_RESULT_SUCCESS;
}

void KernelImp::createPrintfBuffer() {
    if (this->kernelImmData->getDescriptor().kernelAttributes.flags.usesPrintf || pImplicitArgs) {
        this->printfBuffer = PrintfHandler::createPrintfBuffer(this->module->getDevice());
        this->residencyContainer.push_back(printfBuffer);
        if (this->kernelImmData->getDescriptor().kernelAttributes.flags.usesPrintf) {
            NEO::patchPointer(ArrayRef<uint8_t>(crossThreadData.get(), crossThreadDataSize),
                              this->getImmutableData()->getDescriptor().payloadMappings.implicitArgs.printfSurfaceAddress,
                              static_cast<uintptr_t>(this->printfBuffer->getGpuAddressToPatch()));
        }
        if (pImplicitArgs) {
            pImplicitArgs->printfBufferPtr = printfBuffer->getGpuAddress();
        }
        this->devicePrintfKernelMutex = &(static_cast<DeviceImp *>(this->module->getDevice())->printfKernelMutex);
    }
}

void KernelImp::printPrintfOutput(bool hangDetected) {
    PrintfHandler::printOutput(kernelImmData, this->printfBuffer, module->getDevice(), hangDetected);
}

bool KernelImp::usesSyncBuffer() {
    return this->kernelImmData->getDescriptor().kernelAttributes.flags.usesSyncBuffer;
}

void KernelImp::patchSyncBuffer(NEO::GraphicsAllocation *gfxAllocation, size_t bufferOffset) {
    this->residencyContainer.push_back(gfxAllocation);
    NEO::patchPointer(ArrayRef<uint8_t>(crossThreadData.get(), crossThreadDataSize),
                      this->getImmutableData()->getDescriptor().payloadMappings.implicitArgs.syncBufferAddress,
                      static_cast<uintptr_t>(ptrOffset(gfxAllocation->getGpuAddressToPatch(), bufferOffset)));
}

void *KernelImp::patchBindlessSurfaceState(NEO::GraphicsAllocation *alloc, uint32_t bindless) {
    auto &gfxCoreHelper = this->module->getDevice()->getGfxCoreHelper();
    auto ssInHeap = alloc->getBindlessInfo();

    this->residencyContainer.push_back(ssInHeap.heapAllocation);
    auto patchLocation = ptrOffset(getCrossThreadData(), bindless);
    auto patchValue = gfxCoreHelper.getBindlessSurfaceExtendedMessageDescriptorValue(static_cast<uint32_t>(ssInHeap.surfaceStateOffset));
    patchWithRequiredSize(const_cast<uint8_t *>(patchLocation), sizeof(patchValue), patchValue);
    return ssInHeap.ssPtr;
}

void KernelImp::patchWorkgroupSizeInCrossThreadData(uint32_t x, uint32_t y, uint32_t z) {
    const NEO::KernelDescriptor &desc = kernelImmData->getDescriptor();
    auto dst = ArrayRef<uint8_t>(crossThreadData.get(), crossThreadDataSize);
    uint32_t workgroupSize[3] = {x, y, z};
    NEO::patchVecNonPointer(dst, desc.payloadMappings.dispatchTraits.localWorkSize, workgroupSize);
    NEO::patchVecNonPointer(dst, desc.payloadMappings.dispatchTraits.localWorkSize2, workgroupSize);
    NEO::patchVecNonPointer(dst, desc.payloadMappings.dispatchTraits.enqueuedLocalWorkSize, workgroupSize);
    if (pImplicitArgs) {
        pImplicitArgs->localSizeX = x;
        pImplicitArgs->localSizeY = y;
        pImplicitArgs->localSizeZ = z;
    }
}

ze_result_t KernelImp::setGlobalOffsetExp(uint32_t offsetX,
                                          uint32_t offsetY,
                                          uint32_t offsetZ) {
    this->globalOffsets[0] = offsetX;
    this->globalOffsets[1] = offsetY;
    this->globalOffsets[2] = offsetZ;

    return ZE_RESULT_SUCCESS;
}

void KernelImp::patchGlobalOffset() {
    const NEO::KernelDescriptor &desc = kernelImmData->getDescriptor();
    auto dst = ArrayRef<uint8_t>(crossThreadData.get(), crossThreadDataSize);
    NEO::patchVecNonPointer(dst, desc.payloadMappings.dispatchTraits.globalWorkOffset, this->globalOffsets);
    if (pImplicitArgs) {
        pImplicitArgs->globalOffsetX = globalOffsets[0];
        pImplicitArgs->globalOffsetY = globalOffsets[1];
        pImplicitArgs->globalOffsetZ = globalOffsets[2];
    }
}

Kernel *Kernel::create(uint32_t productFamily, Module *module,
                       const ze_kernel_desc_t *desc, ze_result_t *res) {
    UNRECOVERABLE_IF(productFamily >= IGFX_MAX_PRODUCT);
    KernelAllocatorFn allocator = kernelFactory[productFamily];
    auto kernel = static_cast<KernelImp *>(allocator(module));
    *res = kernel->initialize(desc);
    if (*res) {
        kernel->destroy();
        return nullptr;
    }
    return kernel;
}

bool KernelImp::hasIndirectAllocationsAllowed() const {
    return this->kernelHasIndirectAccess && (unifiedMemoryControls.indirectDeviceAllocationsAllowed ||
                                             unifiedMemoryControls.indirectHostAllocationsAllowed ||
                                             unifiedMemoryControls.indirectSharedAllocationsAllowed);
}

uint32_t KernelImp::getSlmTotalSize() const {
    return slmArgsTotalSize + getImmutableData()->getDescriptor().kernelAttributes.slmInlineSize;
}

ze_result_t KernelImp::setCacheConfig(ze_cache_config_flags_t flags) {
    cacheConfigFlags = flags;
    return ZE_RESULT_SUCCESS;
}

ze_result_t KernelImp::getProfileInfo(zet_profile_properties_t *pProfileProperties) {
    pProfileProperties->flags = module->getProfileFlags();
    pProfileProperties->numTokens = 0;
    return ZE_RESULT_SUCCESS;
}

NEO::GraphicsAllocation *KernelImp::getIsaAllocation() const {
    return getImmutableData()->getIsaGraphicsAllocation();
}

uint64_t KernelImp::getIsaOffsetInParentAllocation() const {
    return static_cast<uint64_t>(getImmutableData()->getIsaOffsetInParentAllocation());
}

ze_result_t KernelImp::setSchedulingHintExp(ze_scheduling_hint_exp_desc_t *pHint) {
    auto &threadArbitrationPolicy = const_cast<NEO::ThreadArbitrationPolicy &>(getKernelDescriptor().kernelAttributes.threadArbitrationPolicy);
    if (pHint->flags == ZE_SCHEDULING_HINT_EXP_FLAG_OLDEST_FIRST) {
        threadArbitrationPolicy = NEO::ThreadArbitrationPolicy::AgeBased;
    } else if (pHint->flags == ZE_SCHEDULING_HINT_EXP_FLAG_ROUND_ROBIN) {
        threadArbitrationPolicy = NEO::ThreadArbitrationPolicy::RoundRobin;
    } else {
        threadArbitrationPolicy = NEO::ThreadArbitrationPolicy::RoundRobinAfterDependency;
    }
    return ZE_RESULT_SUCCESS;
}

void KernelImp::setAssertBuffer() {
    if (!getKernelDescriptor().kernelAttributes.flags.usesAssert) {
        return;
    }

    auto assertHandler = this->module->getDevice()->getNEODevice()->getRootDeviceEnvironmentRef().getAssertHandler(this->module->getDevice()->getNEODevice());

    NEO::patchPointer(ArrayRef<uint8_t>(crossThreadData.get(), crossThreadDataSize),
                      this->getImmutableData()->getDescriptor().payloadMappings.implicitArgs.assertBufferAddress,
                      static_cast<uintptr_t>(assertHandler->getAssertBuffer()->getGpuAddressToPatch()));
    this->residencyContainer.push_back(assertHandler->getAssertBuffer());

    if (pImplicitArgs) {
        pImplicitArgs->assertBufferPtr = static_cast<uintptr_t>(assertHandler->getAssertBuffer()->getGpuAddressToPatch());
    }
}

void KernelImp::patchBindlessOffsetsInCrossThreadData(uint64_t bindlessSurfaceStateBaseOffset) const {

    auto &gfxCoreHelper = this->module->getDevice()->getGfxCoreHelper();
    auto surfaceStateSize = gfxCoreHelper.getRenderSurfaceStateSize();

    for (size_t argIndex = 0; argIndex < kernelImmData->getDescriptor().payloadMappings.explicitArgs.size(); argIndex++) {
        const auto &arg = kernelImmData->getDescriptor().payloadMappings.explicitArgs[argIndex];

        auto crossThreadOffset = NEO::undefined<NEO::CrossThreadDataOffset>;
        if (arg.type == NEO::ArgDescriptor::ArgTPointer) {
            crossThreadOffset = arg.as<NEO::ArgDescPointer>().bindless;
        } else if (arg.type == NEO::ArgDescriptor::ArgTImage) {
            crossThreadOffset = arg.as<NEO::ArgDescImage>().bindless;
        } else {
            continue;
        }

        if (NEO::isValidOffset(crossThreadOffset)) {
            auto patchLocation = ptrOffset(getCrossThreadData(), crossThreadOffset);
            auto index = getSurfaceStateIndexForBindlessOffset(crossThreadOffset);

            if (index < std::numeric_limits<uint32_t>::max() && !isBindlessOffsetSet[argIndex]) {
                auto surfaceStateOffset = static_cast<uint32_t>(bindlessSurfaceStateBaseOffset + index * surfaceStateSize);
                auto patchValue = gfxCoreHelper.getBindlessSurfaceExtendedMessageDescriptorValue(static_cast<uint32_t>(surfaceStateOffset));

                patchWithRequiredSize(const_cast<uint8_t *>(patchLocation), sizeof(patchValue), patchValue);
            }
        }
    }
}

uint32_t KernelImp::getSurfaceStateIndexForBindlessOffset(NEO::CrossThreadDataOffset bindlessOffset) const {
    const auto &iter = getKernelDescriptor().getBindlessOffsetToSurfaceState().find(bindlessOffset);
    if (iter != getKernelDescriptor().getBindlessOffsetToSurfaceState().end()) {
        return iter->second;
    }
    DEBUG_BREAK_IF(true);
    return std::numeric_limits<uint32_t>::max();
}

} // namespace L0
