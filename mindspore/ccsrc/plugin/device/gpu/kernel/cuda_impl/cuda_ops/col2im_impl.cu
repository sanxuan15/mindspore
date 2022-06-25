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

#include "plugin/device/gpu/kernel/cuda_impl/cuda_ops/col2im_impl.cuh"
#include <stdio.h>
#include <stdint.h>
#include <cuda_runtime.h>
#include <algorithm>
#include "include/cuda_fp16.h"
#include "plugin/device/gpu/kernel/cuda_impl/cuda_ops/complex.h"

template <typename T>
using Complex = mindspore::utils::Complex<T>;

template <typename T, typename S>
__global__ void Col2ImKernel(const T *input, T *output, const size_t num_kernels, const size_t per_batch_size,
                             const size_t per_channel_size, const size_t per_col_batch_size, const size_t out_height,
                             const size_t out_width, const size_t in_height, const size_t in_width,
                             const size_t kernel_height, const size_t kernel_width, const size_t pad_height,
                             const size_t pad_width, const size_t stride_height, const size_t stride_width,
                             const size_t dilation_height, const size_t dilation_width) {
  for (size_t i = blockIdx.x * blockDim.x + threadIdx.x; i < num_kernels; i += blockDim.x * gridDim.x) {
    S val = static_cast<S>(0);
    size_t w_id = i % out_height + pad_width;
    size_t h_id = i % per_batch_size / out_width % out_height + pad_height;
    size_t c_id = i % per_batch_size / per_channel_size;
    size_t n_col_offset = i / per_batch_size * per_col_batch_size;
    size_t kernel_expand_h = (kernel_height - 1) * dilation_height + 1;
    size_t kernel_expand_w = (kernel_width - 1) * dilation_width + 1;
    // range coordinates
    size_t out_height_start = h_id < kernel_expand_h ? 0 : (h_id - kernel_expand_h) / stride_height + 1;
    size_t out_width_start = w_id < kernel_expand_w ? 0 : (w_id - kernel_expand_w) / stride_width + 1;
    size_t out_height_end = min(h_id / stride_height + 1, in_height);
    size_t out_width_end = min(w_id / stride_width + 1, in_width);

    for (size_t height = out_height_start; height < out_height_end; ++height) {
      for (size_t width = out_width_start; width < out_width_end; ++width) {
        size_t kernel_h = (h_id - height * stride_height);
        size_t kernel_w = (w_id - width * stride_width);
        if (kernel_h % dilation_height == 0 && kernel_w % dilation_width == 0) {
          kernel_h /= dilation_height;
          kernel_w /= dilation_width;
          size_t data_index =
            n_col_offset +
            (((c_id * kernel_height + kernel_h) * kernel_width + kernel_w) * in_height + height) * in_width + width;
          val += (S)input[data_index];
        }
      }
    }
    output[i] = static_cast<T>(val);
  }
}

template <typename T, typename S>
void Col2Im(const T *input, const size_t batch_size, const size_t channels, const size_t out_height,
            const size_t out_width, const size_t in_height, const size_t in_width, const size_t kernel_height,
            const size_t kernel_width, const size_t pad_height, const size_t pad_width, const size_t stride_height,
            const size_t stride_width, const size_t dilation_height, const size_t dilation_width, T *output,
            cudaStream_t cuda_stream) {
  size_t per_channel_size = out_height * out_width;
  size_t per_batch_size = channels * per_channel_size;
  size_t num_kernels = batch_size * per_batch_size;
  size_t per_col_batch_size = channels * in_height * in_width * kernel_width * kernel_height;
  Col2ImKernel<T, S><<<GET_BLOCKS(num_kernels), GET_THREADS, 0, cuda_stream>>>(
    input, output, num_kernels, per_batch_size, per_channel_size, per_col_batch_size, out_height, out_width, in_height,
    in_width, kernel_height, kernel_width, pad_height, pad_width, stride_height, stride_width, dilation_height,
    dilation_width);
  return;
}

template CUDA_LIB_EXPORT void Col2Im<float, float>(
  const float *input, const size_t batch_size, const size_t channels, const size_t out_height, const size_t out_width,
  const size_t in_height, const size_t in_width, const size_t kernel_height, const size_t kernel_width,
  const size_t pad_height, const size_t pad_width, const size_t stride_height, const size_t stride_width,
  const size_t dilation_height, const size_t dilation_width, float *output, cudaStream_t cuda_stream);

template CUDA_LIB_EXPORT void Col2Im<half, float>(
  const half *input, const size_t batch_size, const size_t channels, const size_t out_height, const size_t out_width,
  const size_t in_height, const size_t in_width, const size_t kernel_height, const size_t kernel_width,
  const size_t pad_height, const size_t pad_width, const size_t stride_height, const size_t stride_width,
  const size_t dilation_height, const size_t dilation_width, half *output, cudaStream_t cuda_stream);

template CUDA_LIB_EXPORT void Col2Im<double, double>(
  const double *input, const size_t batch_size, const size_t channels, const size_t out_height, const size_t out_width,
  const size_t in_height, const size_t in_width, const size_t kernel_height, const size_t kernel_width,
  const size_t pad_height, const size_t pad_width, const size_t stride_height, const size_t stride_width,
  const size_t dilation_height, const size_t dilation_width, double *output, cudaStream_t cuda_stream);

template CUDA_LIB_EXPORT void Col2Im<Complex<float>, Complex<float>>(
  const Complex<float> *input, const size_t batch_size, const size_t channels, const size_t out_height,
  const size_t out_width, const size_t in_height, const size_t in_width, const size_t kernel_height,
  const size_t kernel_width, const size_t pad_height, const size_t pad_width, const size_t stride_height,
  const size_t stride_width, const size_t dilation_height, const size_t dilation_width, Complex<float> *output,
  cudaStream_t cuda_stream);

template CUDA_LIB_EXPORT void Col2Im<Complex<double>, Complex<double>>(
  const Complex<double> *input, const size_t batch_size, const size_t channels, const size_t out_height,
  const size_t out_width, const size_t in_height, const size_t in_width, const size_t kernel_height,
  const size_t kernel_width, const size_t pad_height, const size_t pad_width, const size_t stride_height,
  const size_t stride_width, const size_t dilation_height, const size_t dilation_width, Complex<double> *output,
  cudaStream_t cuda_stream);
