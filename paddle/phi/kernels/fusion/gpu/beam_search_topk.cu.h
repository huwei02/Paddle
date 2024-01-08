// Copyright (c) 2022 PaddlePaddle Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

namespace phi {
namespace fusion {

#define WRAP_SIZE 32
#define FLT_MAX 1e38

inline static int GetDesiredBlockDim(int dim) {
  if (dim > 128) {
    return 256;
  } else if (dim > 64) {
    return 128;
  } else if (dim > 32) {
    return 64;
  } else {
    return 32;
  }
}

#define FIXED_BLOCK_DIM_BASE(dim, ...) \
  case (dim): {                        \
    constexpr auto kBlockDim = (dim);  \
    __VA_ARGS__;                       \
  } break

#define FIXED_BLOCK_DIM(...)                \
  FIXED_BLOCK_DIM_BASE(256, ##__VA_ARGS__); \
  FIXED_BLOCK_DIM_BASE(128, ##__VA_ARGS__); \
  FIXED_BLOCK_DIM_BASE(64, ##__VA_ARGS__);  \
  FIXED_BLOCK_DIM_BASE(32, ##__VA_ARGS__)

template <typename T>
struct Pair {
  __device__ __forceinline__ Pair() {}
  __device__ __forceinline__ Pair(T value, int id) : v(value), id(id) {}

  __device__ __forceinline__ void set(T value, int id) {
    v = value;
    this->id = id;
  }

  __device__ __forceinline__ void operator=(const Pair<T>& in) {
    v = in.v;
    id = in.id;
  }

  __device__ __forceinline__ bool operator<(const T value) const {
    return (v < value);
  }

  __device__ __forceinline__ bool operator>(const T value) const {
    return (v > value);
  }
  __device__ __forceinline__ bool operator<(const Pair<T>& in) const {
    return (v < in.v) || ((v == in.v) && (id > in.id));
  }

  __device__ __forceinline__ bool operator>(const Pair<T>& in) const {
    return (v > in.v) || ((v == in.v) && (id < in.id));
  }

  T v;
  int id;
};

template <typename T>
__device__ __forceinline__ void AddTo(Pair<T> topk[],
                                      const Pair<T>& p,
                                      int beam_size) {
  for (int k = beam_size - 2; k >= 0; k--) {
    if (topk[k] < p) {
      topk[k + 1] = topk[k];
    } else {
      topk[k + 1] = p;
      return;
    }
  }
  topk[0] = p;
}

template <typename T, int BlockSize>
__device__ __forceinline__ void GetTopK(Pair<T> topk[],
                                        const T* src,
                                        int idx,
                                        int dim,
                                        int beam_size,
                                        const bool stop_flag,
                                        const int end_id) {
  Pair<T> tmp;
  while (idx < dim) {
    if (stop_flag) {
      tmp.set(idx == end_id ? 0 : -static_cast<T>(FLT_MAX), idx);
    } else {
      tmp.set(src[idx], idx);
    }
    if (topk[beam_size - 1] < tmp) {
      AddTo<T>(topk, tmp, beam_size);
    }
    idx += BlockSize;
  }
}

template <typename T, int BlockSize>
__device__ __forceinline__ void GetTopK(Pair<T> topk[],
                                        const T* src,
                                        int idx,
                                        int dim,
                                        const Pair<T>& max,
                                        int beam_size,
                                        const bool stop_flag,
                                        const int end_id) {
  Pair<T> tmp;
  while (idx < dim) {
    if (stop_flag) {
      tmp.set(idx == end_id ? 0 : -static_cast<T>(FLT_MAX), idx);
    } else {
      tmp.set(src[idx], idx);
    }
    if (topk[beam_size - 1] < tmp && tmp < max) {
      AddTo<T>(topk, tmp, beam_size);
    }

    idx += BlockSize;
  }
}

template <typename T, int MaxLength, int BlockSize>
__device__ __forceinline__ void ThreadGetTopK(Pair<T> topk[],
                                              int* beam,
                                              int beam_size,
                                              const T* src,
                                              bool* firstStep,
                                              bool* is_empty,
                                              Pair<T>* max,
                                              int dim,
                                              const int tid,
                                              const bool stop_flag,
                                              const int end_id) {
  if (*beam > 0) {
    int length = (*beam) < beam_size ? *beam : beam_size;
    if (*firstStep) {
      *firstStep = false;
      GetTopK<T, BlockSize>(topk, src, tid, dim, length, stop_flag, end_id);
    } else {
      for (int k = 0; k < MaxLength; k++) {
        if (k < MaxLength - (*beam)) {
          topk[k] = topk[k + *beam];
        } else {
          topk[k].set(-static_cast<T>(FLT_MAX), -1);
        }
      }
      if (!(*is_empty)) {
        GetTopK<T, BlockSize>(
            topk + MaxLength - *beam, src, tid, dim, *max, length, stop_flag, end_id);
      }
    }

    *max = topk[MaxLength - 1];
    if (max->id == -1) {
      *is_empty = true;
    }
    *beam = 0;
  }
}

template <typename T, int MaxLength, int BlockSize>
__device__ __forceinline__ void BlockReduce(Pair<T>* sh_topk,
                                            int* maxid,
                                            Pair<T> topk[],
                                            T** topVal,
                                            int** topIds,
                                            int* beam,
                                            int* k,
                                            const int tid,
                                            const int warp,
                                            const T cum_log_prob,
                                            const T length_penalty) {
  while (true) {
    __syncthreads();
    if (tid < BlockSize / 2) {
      if (sh_topk[tid] < sh_topk[tid + BlockSize / 2]) {
        maxid[tid] = tid + BlockSize / 2;
      } else {
        maxid[tid] = tid;
      }
    }
    __syncthreads();
    for (int stride = BlockSize / 4; stride > 0; stride = stride / 2) {
      if (tid < stride) {
        if (sh_topk[maxid[tid]] < sh_topk[maxid[tid + stride]]) {
          maxid[tid] = maxid[tid + stride];
        }
      }
      __syncthreads();
    }
    __syncthreads();

    if (tid == 0) {
      **topVal = sh_topk[maxid[0]].v / length_penalty + cum_log_prob;
      **topIds = sh_topk[maxid[0]].id;
      (*topVal)++;
      (*topIds)++;
    }
    if (tid == maxid[0]) (*beam)++;
    if (--(*k) == 0) break;
    __syncthreads();

    if (tid == maxid[0]) {
      if (*beam < MaxLength) {
        sh_topk[tid] = topk[*beam];
      }
    }

    if (*beam == MaxLength) {
      break;
    }
  }
}

template <typename T, int MaxLength, int BlockSize>
__global__ void BeamSearchTopK(T* output,
                             int output_stride,
                             int* indices,
                             const T* src,
                             int lds,
                             int dim,
                             int k,
                             int grid_dim,
                             int num,
                             const T *cum_log_probs, // batch * beam
                             const bool *stop_flags, // batch * beam
                             const int *step_ids,
                             const int *end_ids,
                             const T length_penalty) {
  __shared__ Pair<T> sh_topk[BlockSize];
  const int tid = threadIdx.x;
  const int warp = threadIdx.x / WRAP_SIZE;

  const int bid = blockIdx.x;
  for (int i = bid; i < num; i += grid_dim) {
    int top_num = k;
    __shared__ int maxid[BlockSize / 2];
    T* out = output + i * output_stride;
    int* inds = indices + i * k;
    Pair<T> topk[MaxLength];
    int beam = MaxLength;
    Pair<T> max;
    bool is_empty = false;
    bool firststep = true;

    T cum_log_prob = cum_log_probs[i];
    T current_penalty = 1;
    const bool stop_flag = stop_flags[i];
    const int end_id = end_ids[0];

    if (length_penalty == 0.0) {
        // do nothing
    } else {
        // new_prob = (prob + cum_log_prob * previous_penalty) / current_penalty;
        T previous_penalty = static_cast<T>(powf(step_ids[i], length_penalty));
        current_penalty = static_cast<T>(powf(step_ids[i] + 1, length_penalty));
        cum_log_prob = cum_log_prob * previous_penalty / current_penalty;
    }

    #pragma unroll 1
    for (int j = 0; j < MaxLength; j++) {
      topk[j].set(-static_cast<T>(FLT_MAX), -1);
    }

    while (top_num) {
      ThreadGetTopK<T, MaxLength, BlockSize>(topk,
                                             &beam,
                                             k,
                                             src + i * lds,
                                             &firststep,
                                             &is_empty,
                                             &max,
                                             dim,
                                             tid,
                                             stop_flag,
                                             end_id);

      sh_topk[tid] = topk[0];
      BlockReduce<T, MaxLength, BlockSize>(sh_topk,
                                           maxid,
                                           topk,
                                           &out,
                                           &inds,
                                           &beam,
                                           &top_num,
                                           tid,
                                           warp,
                                           cum_log_prob,
                                           current_penalty);
    }
  }
}

#undef FLT_MAX
}  // namespace fusion
}  // namespace phi
