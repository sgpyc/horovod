// DGC definations
// by Yuechao Pan
// for NVIDIA

#pragma once

#include <map>
#include <list>
#include <math.h>
#include <mpi.h>
#include <nccl.h>
#include <curand_kernel.h>

namespace horovod {
namespace dgc {

template <typename T>
struct PreDefinedValues{};

template <>
struct PreDefinedValues<float>
{
  static const ncclDataType_t NCCLDataType = ncclFloat32;
  MPI_Datatype getMpiDataType() {return MPI_FLOAT;}
  constexpr static const float InvalidValue = NAN;
};

template <>
struct PreDefinedValues<double>
{
  static const ncclDataType_t NCCLDataType = ncclFloat64;
  MPI_Datatype getMpiDataType() {return MPI_DOUBLE;}
  constexpr static const double InvalidValue = NAN;
};

template <>
struct PreDefinedValues<int32_t>
{
  static const ncclDataType_t NCCLDataType = ncclInt32;
  static MPI_Datatype getMpiDataType() {return MPI_INT;}
  static const int32_t AllZeros = (int32_t)0;
  static const int32_t AllOnes  = ~AllZeros;
  static const int32_t InvalidValue = AllOnes;
};

template <>
struct PreDefinedValues<uint32_t>
{
  static const ncclDataType_t NCCLDataType = ncclUint32;
  static MPI_Datatype getMpiDataType() {return MPI_UNSIGNED;}
  static const uint32_t AllZeros = (uint32_t)0;
  static const uint32_t AllOnes  = ~AllZeros;
  static const uint32_t InvalidValue = AllOnes;
};

template <>
struct PreDefinedValues<int64_t>
{
  static const ncclDataType_t NCCLDataType = ncclInt64;
  static MPI_Datatype getMpiDataType() {return MPI_LONG_LONG;}
  static const int64_t AllZeros = (int64_t)0;
  static const int64_t AllOnes  = ~AllZeros;
  static const int64_t InvalidValue = AllOnes;
};

template <>
struct PreDefinedValues<uint64_t>
{
  static const ncclDataType_t NCCLDataType = ncclUint64;
  static MPI_Datatype getMpiDataType() {return MPI_UNSIGNED_LONG_LONG;}
  static const uint64_t AllZeros = (uint64_t)0;
  static const uint64_t AllOnes  = ~AllZeros;
  static const uint64_t InvalidValue = AllOnes;
};

template <typename T>
__device__ __host__ __forceinline__
bool isValid(const T &val)
{
    return (val != PreDefinedValues<T>::InvalidValue);
}

template <>
__device__ __host__ __forceinline__
bool isValid(const float &val)
{
    return (!isnan(val));
}

template <>
__device__ __host__ __forceinline__
bool isValid(const double &val)
{
    return (!isnan(val));
}

template <>
__device__ __host__ __forceinline__
bool isValid(const long double &val)
{
    return (!isnan(val));
}

// Configuration for DGC
struct DgcConfig {
  // The number of warmup epoches for DGC.
  // DGC communication will use a gradient sparsity, which starts from
  // init_sparsity in the first epoch, and exponentially increases to
  // final_sparsity after warmup epoches.
  double warmup_epochs = 5.0;

  // Each epoch has (num_examples_per_epoch / (global_num_gpus * batch_size_per_gpu)
  // steps
  int num_examples_per_epoch = 1000000;
  int batch_size_per_gpu = 32;

  // Initial gradient sparsity for DGC.
  double init_sparsity = 0.75;

  // Final gradient sparsity for DGC, after the warmup epoches.
  double final_sparsity = 0.999;

  // Sampling rate for top-k selection in DGC
  double sampling_rate = 0.01;

  // dgc rand seed
  unsigned int rand_seed = 2800;

  // dgc grid and block sizes
  int grid_size = 32;
  int block_size = 512;

  // stream DGC works on
  cudaStream_t stream = 0;

  // number of GPUs in all nodes
  int global_num_gpus = 1;

  // global GPU rank
  int global_gpu_rank = 0;

  // number of nodes
  int global_num_nodes = 1;

  // node rank
  int global_node_rank = 0;

  // number of GPUs in local node
  int local_num_gpus = 1;

  // local GPU rank
  int local_gpu_rank = 0;

  // NCCL communication handle
  ncclComm_t nccl_comm;
  MPI_Comm mpi_comm;
  MPI_Comm cross_comm;
  MPI_Comm local_comm;

  // whether DgcConfig has been configured
  bool configured = false;

  // the minimum number of elements to trigger sampling
  uint64_t min_sampling_num = 4000;

  // the minimum number of selected elements per layer
  uint64_t min_gradients_comm_per_layer = 10;

  // the minimum number of selected samples per layer
  uint64_t min_selected_samples_per_layer = 5;

  // Momentum
  float momentum = 0.9;

  // Whether to use local gradient clipping
  bool local_gradient_clipping = true;

  // Gradient clipping threshold
  float clipping_threshold = 6.0;

  // Whether to use allReduce instead of allGather for gradient communication
  bool use_allReduce = true;

  // Whether to use hierarchical_allreduce
  bool use_hierarchical_allreduce = true;

  // NCCL communicator for cross node communication, only GPU0
  ncclComm_t nccl_cross_comm;
  ncclComm_t nccl_local_comm;
  bool cross_comm_inited = false;

  // function to set indivual configuration
  void Set(std::string key, std::string value);
};

// Token for GradientAllReduce call, mainly for host arrays,
// to avoid CPU-side data being overwrote before moving to GPU,
// if another call happens before GPU operations of pervious calls
// are executed.
struct DgcToken {
  // Gradient layer and sample layer starts
  uint32_t* h_layer_starts = NULL;
  uint32_t  h_layer_starts_allocated = 0;
  uint32_t* h_samp_starts = NULL;
  uint32_t  h_samp_starts_allocated = 0;

  cudaEvent_t dgc_finish;
};

// Running state, including memory allocation of DGC
struct DgcState {

  // States for curand, one for each GPU thread
  curandState *rand_states     = NULL;

  // Verlocity
  char     *verlocity          = NULL;
  uint64_t  verlocity_allocated = 0;

  // Past verlocity
  char     *pervious_verlocity = NULL;
  uint64_t  pervious_verlocity_allocated = 0;

  // Accumulated verlociy
  char     *accumulated_verlocity = NULL;
  uint64_t  accumulated_verlocity_allocated = 0;

  char     *pervious_accumulated_verlocity = NULL;
  uint64_t  pervious_accumulated_verlocity_allocated = 0;

  // Sample counter
  //uint64_t *samp_counter       = NULL;

  // Sample data, raw data in chars; need to convert type before using
  char     *samp_data          = NULL;
  uint64_t  samp_allocated     = 0;

  // Gradient selection threshold
  float    *thresholds = NULL;
  uint64_t  thresholds_allocated = 0;

  // Counter for gradient selection
  uint64_t *send_counter       = NULL;

  // Number of allocated elements for selected data
  uint64_t  send_allocated     = 0;

  // Memory for selected gradient and indices
  char     *send_data          = NULL;
  uint32_t *send_indices       = NULL;

  // Number of allocated elements for recv data
  uint64_t  recv_allocated     = 0;

  // Memory for recved gradient and indices
  char     *recv_data          = NULL;
  uint32_t *recv_indices       = NULL;

  // Number of allocated elements for global gradients
  uint64_t  global_allocated   = 0;

  // Global gradients
  char     *global_gradients   = NULL;

  // layer offset address book
  std::map<std::string, size_t  > layer_offset_bytes;

  // Per-tensor step counter
  std::map<std::string, uint64_t> step_counters;

  // Step number
  uint64_t step = 0;

  // Epoch number
  double epoch = 0;

  // Counter for adding new tensor to the end of memory space
  size_t offset_byte_counter = 0;

  // Temp storage
  char* temp_storage = NULL;
  size_t temp_storage_bytes = 0;

  // Maximum gradient
  float* max_gradient = NULL;

  // Gradient and sample starts for each layer
  uint32_t* layer_starts = NULL;
  uint32_t  layer_starts_allocated = 0;
  uint32_t* samp_starts = NULL;
  uint32_t  samp_starts_allocated = 0;

  // Gradient selection mask for allReduce communication
  uint32_t* send_masks = NULL;
  uint32_t* recv_masks = NULL;
  uint32_t* h_send_masks = NULL;
  uint32_t* h_recv_masks = NULL;
  uint64_t  mask_allocated = 0;

  uint32_t* mask_counters = NULL;
  uint64_t  mask_counters_allocated = 0;
  uint32_t* mask_offsets  = NULL;
  uint64_t  mask_offsets_allocated = 0;

  uint32_t* h_num_gradients_to_communicate = NULL;

  // Tokens
  std::list<DgcToken> free_tokens;
  std::list<DgcToken> busy_tokens;
};

// Entry warper function
cudaError_t GradientAllReduce(
  ncclDataType_t  gradient_type, // type of gradient
  void           *input_gradients, // GPU pointer to the input graients
  void           *output_gradients,// GPU pointer to the output gradients
  //uint64_t        num_gradients, // number of gradients
  //std::vector<std::tuple<uint64_t, uint64_t, size_t> >
  //               &offset_map,   // <start, length, offset> mappings for
                                // continous chunks of gradients
  std::vector<std::pair<std::string, uint64_t> > &layers,
                                // <name, #elements> of layers
  DgcConfig      &config,       // DGC configuration
  DgcState       &state);       // DGC running states

cudaError_t ClipGradient(
  ncclDataType_t  gradient_type, // type of gradient
  void           *gradients,     // GPU pointer to the gradients
  //uint64_t       *layer_offsets, // gradient layer offsets, on host
  std::vector<std::pair<std::string, uint64_t> > &layers,
                                 // <name, #elements> of layers
  //int             num_layers,    // The number of layers in the gradients
  DgcConfig      &config,        // DGC configuration
  DgcState       &state);        // DGC running states

} // end of namespace dgc
} // end of namespace horovod
