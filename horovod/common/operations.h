// Copyright 2018 Uber Technologies, Inc. All Rights Reserved.
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
// =============================================================================

#ifndef HOROVOD_OPERATIONS_H
#define HOROVOD_OPERATIONS_H

#include <functional>
#include <Python.h>

#include "common.h"

namespace horovod {
namespace common {

// Activity names, see Horovod Timeline for more details.
#define INIT_FUSION_BUFFER "INIT_FUSION_BUFFER"
#define WAIT_FOR_DATA "WAIT_FOR_DATA"
#define WAIT_FOR_OTHER_TENSOR_DATA "WAIT_FOR_OTHER_TENSOR_DATA"
#define ALLOCATE_OUTPUT "ALLOCATE_OUTPUT"
#define MPI_ALLGATHER "MPI_ALLGATHER"
#define INIT_NCCL "INIT_NCCL"
#define QUEUE "QUEUE"
#define MEMCPY_IN_FUSION_BUFFER "MEMCPY_IN_FUSION_BUFFER"
#define NCCL_REDUCE "NCCL_REDUCE"
#define MEMCPY_IN_HOST_BUFFER "MEMCPY_IN_HOST_BUFFER"
#define MPI_ALLREDUCE "MPI_ALLREDUCE"
#define MEMCPY_OUT_HOST_BUFFER "MEMCPY_OUT_HOST_BUFFER"
#define NCCL_BCAST "NCCL_BCAST"
#define NCCL_ALLREDUCE "NCCL_ALLREDUCE"
#define MEMCPY_OUT_FUSION_BUFFER "MEMCPY_OUT_FUSION_BUFFER"
#define MPI_BCAST "MPI_BCAST"

// A callback to call after the MPI communication completes. Since the
// allreduce and allgather ops are asynchronous, this callback is what resumes
// computation after the reduction is completed.
using StatusCallback = std::function<void(const Status&)>;

// Check that Horovod is initialized.
Status CheckInitialized();

extern "C" {

// C interfact to pass parameters into horovod_global
void horovod_set(std::string key, std::string value);

// C interface to initialize Horovod.
void horovod_init();

// C interfact to pass parameters into horovod_global
void horovod_read_config();

// C interface to get index of current Horovod process.
// Returns -1 if Horovod is not initialized.
int horovod_rank();

// C interface to get index of current Horovod process in the node it is on.
// Returns -1 if Horovod is not initialized.
int horovod_local_rank();

// C interface to return number of Horovod processes.
// Returns -1 if Horovod is not initialized.
int horovod_size();

// C interface to return number of Horovod processes in the node it is on.
// Returns -1 if Horovod is not initialized.
int horovod_local_size();

// C interface to return flag indicating whether MPI multi-threading is
// supported. Returns -1 if Horovod is not initialized.
int horovod_mpi_threads_supported();
}

Status EnqueueTensorAllreduce(std::shared_ptr<OpContext> context,
                              std::shared_ptr<Tensor> tensor,
                              std::shared_ptr<Tensor> output,
                              std::shared_ptr<ReadyEvent> ready_event,
                              const std::string name, const int device,
                              StatusCallback callback);

Status EnqueueTensorAllgather(std::shared_ptr<OpContext> context,
                              std::shared_ptr<Tensor> tensor,
                              std::shared_ptr<ReadyEvent> ready_event,
                              const std::string name, const int device,
                              StatusCallback callback);

Status EnqueueTensorBroadcast(std::shared_ptr<OpContext> context,
                              std::shared_ptr<Tensor> tensor,
                              std::shared_ptr<Tensor> output, int root_rank,
                              std::shared_ptr<ReadyEvent> ready_event,
                              const std::string name, const int device,
                              StatusCallback callback);

} // namespace common
} // namespace horovod

#endif // HOROVOD_OPERATIONS_H
