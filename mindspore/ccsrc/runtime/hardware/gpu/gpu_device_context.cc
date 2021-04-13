/**
 * Copyright 2021 Huawei Technologies Co., Ltd
 *
 * Licensed under the Apache License, Version 2.0 (the "License"){}
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "runtime/hardware/gpu/gpu_device_context.h"
#include <dlfcn.h>
#include "runtime/device/gpu/kernel_info_setter.h"
#include "runtime/device/gpu/gpu_kernel_build.h"
#include "runtime/device/gpu/gpu_device_address.h"
#include "runtime/device/gpu/gpu_memory_manager.h"
#include "runtime/device/gpu/gpu_memory_allocator.h"
#include "runtime/device/gpu/gpu_stream_assign.h"
#include "runtime/device/gpu/distribution/collective_init.h"
#include "runtime/device/gpu/gpu_device_manager.h"
#include "runtime/device/gpu/gpu_buffer_mgr.h"
#include "backend/kernel_compiler/common_utils.h"
#include "runtime/device/gpu/gpu_common.h"
#include "runtime/hardware/gpu/optimizer.h"
#include "common/trans.h"
#include "utils/context/graph_kernel_flags.h"

namespace mindspore {
namespace device {
namespace gpu {
static thread_local bool cur_thread_device_inited{false};

bool GPUDeviceContext::Initialize() {
  if (initialized_ == true) {
    if (!BindDeviceToCurrentThread()) {
      return false;
    }
    GPUMemoryAllocator::GetInstance().CheckMaxDeviceMemory();
    return true;
  }

  // Set device id
  const void *collective_handle_ = CollectiveInitializer::instance().collective_handle();
  bool collective_inited = CollectiveInitializer::instance().collective_inited();
  if (collective_inited && collective_handle_ != nullptr) {
    auto get_local_rank_funcptr =
      reinterpret_cast<GetLocalRankId>(dlsym(const_cast<void *>(collective_handle_), "local_rank_id"));
    MS_EXCEPTION_IF_NULL(get_local_rank_funcptr);
    device_context_key_.device_id_ = IntToUint((*get_local_rank_funcptr)());
  }

  // Set device id and initialize device resource.
  bool ret = InitDevice();
  if (!ret) {
    MS_LOG(ERROR) << "GPU InitDevice failed.";
    return ret;
  }

  // Initialize memory pool.
  mem_manager_ = std::make_shared<GPUMemoryManager>();
  MS_EXCEPTION_IF_NULL(mem_manager_);
  mem_manager_->MallocDeviceMemory();

  // Initialize NCCL.
  if (collective_inited && collective_handle_ != nullptr) {
    auto init_nccl_comm_funcptr =
      reinterpret_cast<InitNCCLComm>(dlsym(const_cast<void *>(collective_handle_), "InitNCCLComm"));
    MS_EXCEPTION_IF_NULL(init_nccl_comm_funcptr);
    (*init_nccl_comm_funcptr)();
  }

  initialized_ = true;
  return ret;
}

bool GPUDeviceContext::InitDevice() {
  if (GPUDeviceManager::GetInstance().device_count() <= 0) {
    MS_LOG(ERROR) << "No GPU device found.";
    return false;
  }

  if (!GPUDeviceManager::GetInstance().is_device_id_init()) {
    if (!GPUDeviceManager::GetInstance().set_cur_device_id(device_context_key_.device_id_)) {
      MS_LOG(ERROR) << "Failed to set current device id: " << SizeToInt(device_context_key_.device_id_);
      return false;
    }
  }

  // Initialize device resource, such as stream, cudnn and cublas handle.
  GPUDeviceManager::GetInstance().InitDevice();
  auto stream = GPUDeviceManager::GetInstance().default_stream();
  if (stream == nullptr) {
    MS_LOG(ERROR) << "No default CUDA stream found.";
    return false;
  }
  streams_.push_back(stream);
  return true;
}

void GPUDeviceContext::Destroy() {
  // Release GPU buffer manager resource
  if (GpuBufferMgr::GetInstance().IsInit()) {
    if (!GpuBufferMgr::GetInstance().IsClosed() && !GpuBufferMgr::GetInstance().CloseNotify()) {
      MS_LOG(EXCEPTION) << "Could not close gpu data queue.";
    }
    CHECK_OP_RET_WITH_EXCEPT(GpuBufferMgr::GetInstance().Destroy(), "Could not destroy gpu data queue.");
  }

  // Release stream, cudnn and cublas handle, etc.
  GPUDeviceManager::GetInstance().ReleaseDevice();

  // Release device memory
  if (mem_manager_ != nullptr) {
    mem_manager_->FreeDeviceMemory();
    mem_manager_ = nullptr;
  }

  // Clean GPU cache kernels which is generated by AKG
  auto context_ptr = MsContext::GetInstance();
  MS_EXCEPTION_IF_NULL(context_ptr);
  if (!(context_ptr->get_param<bool>(MS_CTX_SAVE_GRAPHS_FLAG))) {
    kernel::KernelMeta *bin_map = kernel::KernelMeta::GetInstance();
    MS_EXCEPTION_IF_NULL(bin_map);
    bin_map->RemoveKernelCache();
  }
}

bool GPUDeviceContext::AllocateMemory(DeviceAddress *const &address, size_t size) const {
  MS_EXCEPTION_IF_NULL(address);
  if (!BindDeviceToCurrentThread()) {
    return false;
  }
  auto device_ptr = mem_manager_->MallocMemFromMemPool(size);
  if (!device_ptr) {
    return false;
  }
  address->ptr_ = device_ptr;
  address->size_ = size;
  address->from_mem_pool_ = true;
  return true;
}

void GPUDeviceContext::FreeMemory(DeviceAddress *const &address) const {
  MS_EXCEPTION_IF_NULL(address);
  MS_EXCEPTION_IF_NULL(address->ptr_);
  mem_manager_->FreeMemFromMemPool(address->ptr_);
  address->ptr_ = nullptr;
}

bool GPUDeviceContext::AllocateContinuousMemory(const std::vector<DeviceAddressPtr> &addr_list, size_t total_size,
                                                const std::vector<size_t> &size_list) const {
  if (!BindDeviceToCurrentThread()) {
    return false;
  }
  return mem_manager_->MallocContinuousMemFromMemPool(addr_list, total_size, size_list);
}

DeviceAddressPtr GPUDeviceContext::CreateDeviceAddress(void *device_ptr, size_t device_size, const string &format,
                                                       TypeId type_id) const {
  return std::make_shared<GPUDeviceAddress>(device_ptr, device_size, format, type_id);
}

void GPUDeviceContext::OptimizeGraph(const KernelGraphPtr &graph) const {
  MS_EXCEPTION_IF_NULL(graph);
  // Optimization pass which is irrelevant to device type or format.
  OptimizeGraphWithoutDeviceInfo(graph);

  SetOperatorInfo(graph->execution_order());

  // Optimization pass which is relevant to device type or format.
  OptimizeGraphWithDeviceInfo(graph);
}

void GPUDeviceContext::OptimizeGraphWithoutDeviceInfo(const KernelGraphPtr &graph) const {
  MS_EXCEPTION_IF_NULL(graph);
  // Operator fusion optimization.
  FuseOperators(graph);

  device::gpu::AssignGpuStream(graph);

  // Update Graph Dynamic Shape Attr.
  UpdateGraphDynamicShapeAttr(NOT_NULL(graph));
}

void GPUDeviceContext::OptimizeGraphWithDeviceInfo(const KernelGraphPtr &graph) const {
  // Graph optimization relevant to device data format
  auto optimizer = std::make_shared<opt::GraphOptimizer>();
  auto pm = std::make_shared<opt::PassManager>();
  pm->AddPass(std::make_shared<opt::BatchNormReluFusion>());
  pm->AddPass(std::make_shared<opt::BatchNormReluGradFusion>());
  pm->AddPass(std::make_shared<opt::BatchNormAddReluFusion>());
  pm->AddPass(std::make_shared<opt::PostBatchNormAddReluFusion>());
  pm->AddPass(std::make_shared<opt::BatchNormAddReluGradFusion>());
  pm->AddPass(std::make_shared<opt::InsertFormatTransformOp>());
  pm->AddPass(std::make_shared<opt::RemoveFormatTransformPair>());
  pm->AddPass(std::make_shared<opt::RemoveRedundantFormatTransform>());
  pm->AddPass(std::make_shared<opt::CudnnInplaceAggregate>());
  pm->AddPass(std::make_shared<opt::ReluV2Pass>());
  pm->AddPass(std::make_shared<opt::AddReluV2Fusion>());
  pm->AddPass(std::make_shared<opt::AddReluGradV2Fusion>());
  pm->AddPass(std::make_shared<opt::AllReduceFusion>());
  pm->AddPass(std::make_shared<opt::GetitemTuple>());
  pm->AddPass(std::make_shared<opt::ReducePrecisionFusion>("reduce_precision"));
  optimizer->AddPassManager(pm);
  (void)optimizer->Optimize(graph);
  graph->SetExecOrderByDefault();
}

void GPUDeviceContext::FuseOperators(const KernelGraphPtr &graph) const {
  auto optimizer = std::make_shared<opt::GraphOptimizer>();
  auto pm = std::make_shared<opt::PassManager>();
  pm->AddPass(std::make_shared<opt::AdamWeightDecayFusion>());
  pm->AddPass(std::make_shared<opt::AdamFusion>());
  pm->AddPass(std::make_shared<opt::ApplyMomentumWeightDecayScaleFusion>());
  pm->AddPass(std::make_shared<opt::ApplyMomentumScaleFusion>());
  pm->AddPass(std::make_shared<opt::ApplyMomentumWeightDecayFusion>());
  if (!context::GraphKernelFlags::GetInstance().IsEnableGraphKernel()) {
    pm->AddPass(std::make_shared<opt::CastAllFusion>("cast_all"));
  }
  pm->AddPass(std::make_shared<opt::CombineMomentumFusion>("combine_momentum"));
  pm->AddPass(std::make_shared<opt::ReplaceMomentumCastFusion>());
  pm->AddPass(std::make_shared<opt::ReplaceAddNFusion>());
  pm->AddPass(std::make_shared<opt::PrintReduceFusion>("print_reduce"));
  optimizer->AddPassManager(pm);
  (void)optimizer->Optimize(graph);
  graph->SetExecOrderByDefault();

  // Graph kernel fusion optimization
  if (context::GraphKernelFlags::GetInstance().IsEnableGraphKernel()) {
    opt::GraphKernelOptimize(graph);
    graph->SetExecOrderByDefault();
  }
}

void GPUDeviceContext::UpdateGraphDynamicShapeAttr(const NotNull<KernelGraphPtr> &graph) const {
  for (const auto &cnode : graph->execution_order()) {
    if (AnfAlgo::IsNodeDynamicShape(cnode)) {
      AnfAlgo::SetNodeAttr(kAttrIsDynamicShape, MakeValue(true), cnode);
      MS_LOG(INFO) << "Set Dynamic Shape Attr to Node:" << cnode->fullname_with_scope();
    }
  }
  graph->UpdateGraphDynamicAttr();
}

void GPUDeviceContext::OptimizeSingleOpGraph(const KernelGraphPtr &graph) const {
  MS_EXCEPTION_IF_NULL(graph);
  SetOperatorInfo(graph->execution_order());

  auto optimizer = std::make_shared<opt::GraphOptimizer>();
  auto pm = std::make_shared<opt::PassManager>();
  pm->AddPass(std::make_shared<opt::ReducePrecisionFusion>("reduce_precision"));
  optimizer->AddPassManager(pm);
  (void)optimizer->Optimize(graph);
  graph->SetExecOrderByDefault();
}

void GPUDeviceContext::SetOperatorInfo(const std::vector<CNodePtr> &nodes) const {
  for (const auto &node : nodes) {
    SetKernelInfo(node);
  }
}

void GPUDeviceContext::CreateKernel(const std::vector<CNodePtr> &nodes) const { CreateGPUKernel(nodes); }

bool GPUDeviceContext::LaunchKernel(KernelMod *kernel_mod, const std::vector<AddressPtr> &inputs,
                                    const std::vector<AddressPtr> &workspace,
                                    const std::vector<AddressPtr> &outputs) const {
  MS_EXCEPTION_IF_NULL(kernel_mod);
  if (!BindDeviceToCurrentThread()) {
    return false;
  }
  std::lock_guard<std::mutex> locker(launch_mutex_);
  return kernel_mod->Launch(inputs, workspace, outputs, streams_.front());
}

bool GPUDeviceContext::SyncStream(size_t stream_id) const {
  if (stream_id >= streams_.size()) {
    MS_LOG(EXCEPTION) << "The stream_id: " << stream_id << " is greater than stream array size: " << streams_.size();
  }
  return GPUDeviceManager::GetInstance().SyncStream(streams_[stream_id]);
}

bool GPUDeviceContext::BindDeviceToCurrentThread() const {
  if (cur_thread_device_inited) {
    return true;
  }

  if (!CudaDriver::SetDevice(UintToInt(device_context_key_.device_id_))) {
    MS_LOG(ERROR) << "Failed to set device id: " << device_context_key_.device_id_;
    return false;
  }

  cur_thread_device_inited = true;
  return true;
}

MS_REGISTER_DEVICE(kGPUDevice, GPUDeviceContext);
}  // namespace gpu
}  // namespace device
}  // namespace mindspore
