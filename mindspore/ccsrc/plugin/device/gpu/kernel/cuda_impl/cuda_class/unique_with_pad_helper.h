/**
 * Copyright 2022 Huawei Technologies Co., Ltd
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
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

#ifndef MINDSPORE_CCSRC_PLUGIN_DEVICE_GPU_KERNEL_CUDA_IMPL_CUDA_CLASS_UNIQUE_WITH_PAD_HELPER_H_
#define MINDSPORE_CCSRC_PLUGIN_DEVICE_GPU_KERNEL_CUDA_IMPL_CUDA_CLASS_UNIQUE_WITH_PAD_HELPER_H_
#include <string>
#include <vector>
#include "plugin/device/gpu/kernel/cuda_impl/cuda_class/helper_base.h"
#include "plugin/device/gpu/kernel/cuda_impl/cuda_ops/unique_with_pad_impl.cuh"

namespace mindspore {
namespace cukernel {
template <typename T, typename S>
class UniqueWithPadHelperGpuKernel : public GpuKernelHelperBase {
 public:
  explicit UniqueWithPadHelperGpuKernel(const std::string &kernel_name, const uint32_t &device_id)
      : GpuKernelHelperBase(kernel_name, device_id) {
    num_elements_ = 1;
  }
  virtual ~UniqueWithPadHelperGpuKernel() = default;
  int CalMemSize(const std::vector<std::vector<int64_t>> &input_shapes,
                 const std::vector<std::vector<int64_t>> &output_shapes) override {
    ResetResource();
    constexpr size_t INPUT_NUM = 2;
    int flag = CalShapesSizeInBytes<T>(input_shapes, INPUT_NUM, kernel_name_, "input_shapes", &input_size_list_);
    if (flag == -1) {
      return flag;
    }
    num_elements_ = input_size_list_[0] / sizeof(T);
    size_t workspace_size = num_elements_ * sizeof(S);
    work_size_list_.emplace_back(workspace_size);
    work_size_list_.emplace_back(workspace_size);
    output_size_list_.emplace_back(input_size_list_[0]);
    output_size_list_.emplace_back(num_elements_ * sizeof(S));
    return 0;
  }

  int Process(const std::vector<void *> &input_ptrs, const std::vector<void *> &output_ptrs,
              const std::vector<void *> &work_ptrs, void *cuda_stream) override {
    T *t_input_ptr = nullptr;
    T *t_pad_num_ptr = nullptr;
    S *s_input_index = nullptr;
    S *s_sorted_index = nullptr;
    T *t_output_ptr = nullptr;
    S *s_output_index = nullptr;
    int flag = GetDeviceAddress<T>(input_ptrs, 0, kernel_name_, &t_input_ptr);
    if (flag != 0) {
      return flag;
    }

    flag = GetDeviceAddress<T>(input_ptrs, 1, kernel_name_, &t_pad_num_ptr);
    if (flag != 0) {
      return flag;
    }

    flag = GetDeviceAddress<S>(work_ptrs, 0, kernel_name_, &s_input_index);
    if (flag != 0) {
      return flag;
    }

    flag = GetDeviceAddress<S>(work_ptrs, 1, kernel_name_, &s_sorted_index);
    if (flag != 0) {
      return flag;
    }
    flag = GetDeviceAddress<T>(output_ptrs, 0, kernel_name_, &t_output_ptr);
    if (flag != 0) {
      return flag;
    }

    flag = GetDeviceAddress<S>(output_ptrs, 1, kernel_name_, &s_output_index);
    if (flag != 0) {
      return flag;
    }

    CalUniqueWithPad(t_input_ptr, num_elements_, s_input_index, s_sorted_index, t_output_ptr, s_output_index,
                     reinterpret_cast<cudaStream_t>(cuda_stream), t_pad_num_ptr);
    return 0;
  }

 private:
  int num_elements_;
};
}  // namespace cukernel
}  // namespace mindspore
#endif  // MINDSPORE_CCSRC_PLUGIN_DEVICE_GPU_KERNEL_CUDA_IMPL_CUDA_CLASS_UNIQUE_WITH_PAD_HELPER_H_
