// DGC device kernel implementations
// by Yuechao Pan
// for NVIDIA

#pragma once

#include "device_intrinsics.cuh"

namespace horovod {
namespace dgc {

template <typename T, typename SizeT>
__global__
void sample_kernel(
  T           *elements,
  SizeT        num_elements,
  T           *samples,
  SizeT        num_samples,
  curandState *rand_states)
{
  const SizeT STRIDE = (SizeT)gridDim.x * blockDim.x;
  const SizeT thread_id = (SizeT)blockDim.x * blockIdx.x + threadIdx.x;

  SizeT i = thread_id;
  while (i < num_samples)
  {
    SizeT pos = curand_uniform(rand_states + thread_id) * num_elements;
    if (pos >= num_elements)
      pos -= num_elements;
    //if (i < 10)
    //  printf("Selecting elements[%d] = %f for pos %d\n",
    //    pos, elements[pos], i);
    samples[i] = abs(elements[pos]);

    i += STRIDE;
  }
}

template <typename T, typename IndexT, typename SizeT, typename CounterT>
__global__
void select_kernel(
  T      *elements,
  SizeT   num_elements,
  int     global_num_gpus,
  float  *threshold_,
  SizeT   target_num,
  T      *selected_elements,
  IndexT *selected_indices,
  CounterT *selected_count)
{
  const SizeT STRIDE = (SizeT)gridDim.x * blockDim.x;
  SizeT block_input_start = (SizeT)blockDim.x * blockIdx.x;
  __shared__ SizeT s_block_output_count, s_block_output_start;
  float threshold = threshold_[0];

  if (threadIdx.x == 0)
  {
    s_block_output_count = 0;
    if (blockIdx.x == 0)
      printf("threadhold = %f\n", threshold);
  }
  __syncthreads();

  while (block_input_start < num_elements)
  {
    SizeT thread_input  = block_input_start + threadIdx.x;
    SizeT thread_output = 0;
    bool thread_to_select = false;
    T element = 0;
    if (thread_input < num_elements)
    {
      element = elements[thread_input];
      if (!(abs(element) < threshold))
      {
        thread_to_select = true;
        thread_output = atomicAdd(&s_block_output_count, (SizeT)1);
      }
    }
    __syncthreads();
    // TODO: if double atomicAdd is performance bottleneck,
    //       change to block scan
    if (threadIdx.x == 0 && s_block_output_count != 0)
    {
      s_block_output_start = atomicAdd(selected_count, (CounterT)s_block_output_count);
      s_block_output_count = 0;
    }
    __syncthreads();
    thread_output += s_block_output_start;
    if (thread_to_select && thread_output < target_num)
    {
      selected_elements[thread_output] = element / global_num_gpus;
      selected_indices [thread_output] = thread_input;
    }

    block_input_start += STRIDE;
  }
}

template <typename T, typename IndexT, typename SizeT, typename CounterT>
__global__
void select_kernel2(
  T      *elements,
  SizeT   num_elements,
  int     global_num_gpus,
  float  *threshold_,
  SizeT   target_num,
  T      *selected_elements,
  IndexT *selected_indices,
  CounterT *selected_count)
{
  static const int num_local_slots = 4;
  const SizeT STRIDE = (SizeT)gridDim.x * blockDim.x;
  __shared__ bool s_to_continue;
  __shared__ int s_block_output_count;
  __shared__ SizeT s_block_output_start;
  const T threshold = threshold_[0];
  T      thread_elements[num_local_slots];
  IndexT thread_indices [num_local_slots];

  if (threadIdx.x == 0)
  {
    s_to_continue = true;
    s_block_output_count = 0;
    if (blockIdx.x == 0)
      printf("threadhold = %f, #elements = %lld\n", threshold,
        (long long)num_elements);
  }
  __syncthreads();

  SizeT thread_pos = (SizeT)blockDim.x * blockIdx.x + threadIdx.x;
  int thread_num_output = 0;
  while (s_to_continue)
  {
    while (thread_pos < num_elements &&
           thread_num_output < num_local_slots)
    {
      T element = elements[thread_pos];
      //T element = 0;
      if ((abs(element) > threshold))
      {
        thread_elements[thread_num_output] = element;
        thread_indices [thread_num_output] = thread_pos;
        thread_num_output ++;
      }
      thread_pos += STRIDE;
    }

    int thread_output_start = 0;
    if (thread_num_output != 0)
      atomicAdd(&s_block_output_count, thread_num_output);
    __syncthreads();

    if (threadIdx.x == 0)
    {
      if (s_block_output_count != 0)
      {
        s_block_output_start =
          atomicAdd(selected_count, (CounterT)s_block_output_count);
        s_block_output_count = 0;
        if (s_block_output_start >= target_num)
          s_to_continue = false;
      } else {
        s_to_continue = false;
      }
    }
    __syncthreads();

    IndexT output_pos = s_block_output_start + thread_output_start;
    for (int i = 0; i < thread_num_output; i++)
    {
      if (output_pos >= target_num)
        break;
      selected_elements[output_pos] = thread_elements[i] / global_num_gpus;
      selected_indices [output_pos] = thread_indices [i];
      output_pos ++;
    }
    thread_num_output = 0;

    //if (thread_pos < num_elements)
    //  s_to_continue = true;
    //__syncthreads();
  }
}

template <typename T, typename IndexT, typename SizeT, typename CounterT>
__global__
void pad_kernel(
  T     *selected_elements,
  IndexT *selected_indices,
  SizeT  target_num,
  CounterT *selected_count)
{
  const SizeT STRIDE = (SizeT)gridDim.x * blockDim.x;
  SizeT i = selected_count[0] + (SizeT)blockDim.x * blockIdx.x + threadIdx.x;

  if (blockIdx.x == 0 && threadIdx.x == 0)
    printf("#selected = %ld, target = %ld\n", (long long)i, (long long)target_num);

  while (i < target_num)
  {
    selected_elements[i] = PreDefinedValues<T    >::InvalidValue;
    selected_indices [i] = PreDefinedValues<SizeT>::InvalidValue;
    i += STRIDE;
  }
}

template <typename SizeT, typename OpT>
__global__
void loop_kernel(
  SizeT loop_size,
  OpT   op)
{
  const SizeT STRIDE = (SizeT)gridDim.x * blockDim.x;
  SizeT i = (SizeT)blockDim.x * blockIdx.x + threadIdx.x;

  while (i < loop_size)
  {
    op(i);
    i += STRIDE;
  }
}

} // end of namespace dgc
} // end of namespace horovod
