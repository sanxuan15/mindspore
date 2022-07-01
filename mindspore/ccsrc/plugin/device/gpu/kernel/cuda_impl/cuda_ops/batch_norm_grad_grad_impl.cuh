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

#ifndef MINDSPORE_CCSRC_PLUGIN_DEVICE_GPU_KERNEL_CUDA_IMPL_CUDA_OPS_BATCH_NORM_GRAD_GRAD_IMPL_CUH_
#define MINDSPORE_CCSRC_PLUGIN_DEVICE_GPU_KERNEL_CUDA_IMPL_CUDA_OPS_BATCH_NORM_GRAD_GRAD_IMPL_CUH_
#include "plugin/device/gpu/kernel/cuda_impl/cuda_ops/cuda_common.h"

typedef struct {
  size_t n;
  size_t c;
  size_t h;
  size_t w;
} ShapeInfo;

enum DataFormat {
  NCHW = 0,
  NHWC = 1
};

template <typename T>
CUDA_LIB_EXPORT void BatchNormGradGradTraining(const T *dy, const T *x, const float *scale, const float *mean,
                                               const float *variance, const T *dout_dx, const float *dout_dscale,
                                               const float *dout_dbias, T *dx, T *ddy, float *dscale, float *inv_std,
                                               float *x_hat, float *mean_dy, float *mean_dout_dx,
                                               float *mean_dy_mul_x_hat, float *mean_dout_dx_mul_x_hat,
                                               float *mean_dy_mul_dout_dx, const ShapeInfo &shape_info,
                                               DataFormat format, float epsilon, uint32_t device_id,
                                               cudaStream_t stream);
template <typename T>
CUDA_LIB_EXPORT void BatchNormGradGradInference(const T *dy, const T *x, const float *scale, const float *mean,
                                                const float *variance, const T *dout_dx, const float *dout_dscale,
                                                const float *dout_dbias, T *dx, T *ddy, float *dscale, float *inv_std,
                                                float *tmp, const ShapeInfo &shape_info, DataFormat format,
                                                float epsilon, uint32_t device_id, cudaStream_t stream);
#endif  // MINDSPORE_CCSRC_PLUGIN_DEVICE_GPU_KERNEL_CUDA_IMPL_CUDA_OPS_BATCH_NORM_GRAD_GRAD_IMPL_CUH_
