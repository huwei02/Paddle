/* Copyright (c) 2016 PaddlePaddle Authors. All Rights Reserved.
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at
    http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License. */

#pragma once

#if defined(PADDLE_WITH_CUDA) || defined(PADDLE_WITH_HIP)

#include <stddef.h>

#include <array>
#include <string>
#include <vector>

#include "paddle/fluid/platform/device/gpu/gpu_types.h"

namespace paddle {
namespace platform {
//! Get the version of dnn
int DnnVersion();

//! Get the total number of GPU devices in system.
int GetGPUDeviceCount();

//! Get the compute capability of the ith GPU (format: major * 10 + minor)
int GetGPUComputeCapability(int id);

//! Get the runtime version of the ith GPU
int GetGPURuntimeVersion(int id);

//! Get the driver version of the ith GPU
int GetGPUDriverVersion(int id);

//! Wheter the current device support TensorCore
bool TensorCoreAvailable();

//! Get the MultiProcessors of the ith GPU.
int GetGPUMultiProcessors(int id);

//! Get the MaxThreads of each MultiProcessor of the ith GPU.
int GetGPUMaxThreadsPerMultiProcessor(int id);

//! Get the MaxThreads of each block of the ith GPU.
int GetGPUMaxThreadsPerBlock(int id);

//! Get the current GPU device id in system.
int GetCurrentDeviceId();

//! Get the maximum GridDim size for GPU buddy allocator.
std::array<int, 3> GetGpuMaxGridDimSize(int);

//! Get a list of device ids from environment variable or use all.
std::vector<int> GetSelectedDevices();

//! Get the properties of the ith GPU device.
const gpuDeviceProp &GetDeviceProperties(int id);

//! Set the GPU device id for next execution.
void SetDeviceId(int device_id);

//! Get the memory usage of current GPU device.
void GpuMemoryUsage(size_t *available, size_t *total);

//! Get the available memory to allocate, which is the size of available gpu
//! minus reserving.
size_t GpuAvailableMemToAlloc();

//! Get the maximum allocation size of current GPU device.
size_t GpuMaxAllocSize();

//! Get the initial allocation size of current GPU device.
size_t GpuInitAllocSize();

//! Get the re-allocation size of current GPU device.
size_t GpuReallocSize();

//! Get the minimum chunk size for GPU buddy allocator.
size_t GpuMinChunkSize();

//! Get the maximum chunk size for GPU buddy allocator.
size_t GpuMaxChunkSize();

//! Copy memory from address src to dst asynchronously.
void GpuMemcpyAsync(void *dst,
                    const void *src,
                    size_t count,
                    gpuMemcpyKind kind,
                    gpuStream_t stream);

//! Copy memory from address src to dst synchronously.
void GpuMemcpySync(void *dst,
                   const void *src,
                   size_t count,
                   gpuMemcpyKind kind);

//! Copy memory from one device to another device asynchronously.
void GpuMemcpyPeerAsync(void *dst,
                        int dst_device,
                        const void *src,
                        int src_device,
                        size_t count,
                        gpuStream_t stream);

//! Copy memory from one device to another device synchronously.
void GpuMemcpyPeerSync(
    void *dst, int dst_device, const void *src, int src_device, size_t count);

//! Set memory dst with value count size asynchronously
void GpuMemsetAsync(void *dst, int value, size_t count, gpuStream_t stream);

//! Blocks until stream has completed all operations.
void GpuStreamSync(gpuStream_t stream);

void GpuDestroyStream(gpuStream_t stream);

// ! Blocks until device has completed all operations.
void GpuDeviceSync();

//! CudaMalloc with recorded info
gpuError_t RecordedGpuMalloc(void **ptr,
                             size_t size,
                             int dev_id,
                             bool malloc_managed_memory = false);

//! CudaFree with recorded info
void RecordedGpuFree(void *p, size_t size, int dev_id);

gpuError_t GpuGetLastError();

#ifdef PADDLE_WITH_CUDA
#if CUDA_VERSION >= 10020
//! cuMemCreate with recorded info
CUresult RecordedGpuMemCreate(CUmemGenericAllocationHandle *handle,
                              size_t size,
                              const CUmemAllocationProp *prop,
                              unsigned long long flags,
                              int dev_id);  // NOLINT

//! cuMemRelease with recorded info
CUresult RecordedGpuMemRelease(CUmemGenericAllocationHandle handle,
                               size_t size,
                               int dev_id);
#endif
#endif

//! Get available and total gpu memory with considering limitation
bool RecordedGpuMemGetInfo(size_t *avail,
                           size_t *total,
                           size_t *actual_avail,
                           size_t *actual_total,
                           int dev_id);

//! Get recorded cudaMalloc size. If record is disabled, return 0.
uint64_t RecordedGpuMallocSize(int dev_id);

uint64_t RecordedGpuLimitSize(int dev_id);

bool IsGpuMallocRecorded(int dev_id);

//! Empty idle cached memory held by the allocator.
void EmptyCache(void);

bool IsGPUManagedMemorySupported(int dev_id);

bool IsGPUManagedMemoryOversubscriptionSupported(int dev_id);

//! Get the primitive pointer return from cudaMalloc, just implemented with
//! testing, do not use for release
void *GetGpuBasePtr(void *ptr, int dev_id);
//! is support flash attn
bool IsSupportFlashAttn(int dev_id);

}  // namespace platform
}  // namespace paddle

#endif
