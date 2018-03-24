#include <torch/torch.h>
#include <ATen/TensorUtils.h>
#include <tuple>
#include <iostream>
#include "ctc.h"

std::tuple<at::Tensor, at::Tensor> ctc(at::Tensor activations,
				       at::Tensor input_lengths,
				       at::Tensor labels,
				       at::Tensor label_lengths,
				       int blank_label = 0)
{
    auto is_cuda = activations.type().is_cuda();
   
    auto activations_arg = at::TensorArg(activations, "activations", 0);
    checkScalarType("activations", activations_arg, at::kFloat);
    checkContiguous("activations", activations_arg);

    auto input_lengths_arg = at::TensorArg(input_lengths, "input_lengths", 1);
    checkScalarType("input_lengths", input_lengths_arg, at::kInt);
    checkContiguous("input_lengths", input_lengths_arg);

    auto labels_arg = at::TensorArg(labels, "labels", 2);
    checkScalarType("labels", labels_arg, at::kInt);
    checkContiguous("labels", labels_arg);

    auto label_lengths_arg = at::TensorArg(label_lengths, "label_lengths", 3);
    checkScalarType("label_lengths", label_lengths_arg, at::kInt);
    checkContiguous("label_lengths", label_lengths_arg);

    // check dimensions?
    const auto batch_size = input_lengths.size(0);
    const auto alphabet_size = activations.size(2);
   
    ctcOptions options{};
    options.blank_label = blank_label;
    if (! is_cuda) {
        options.loc = CTC_CPU;
        options.num_threads = 0; // will use default number of threads

        #if defined(CTC_DISABLE_OMP) || defined(APPLE)
        // have to use at least one
        options.num_threads = std::max(options.num_threads, (unsigned int) 1);
        #endif
    }
    else {
        options.loc = CTC_GPU;
        options.stream = at::globalContext().getCurrentCUDAStream();
    }
   
   
/*
ctcStatus_t get_workspace_size(const int* const label_lengths,
                               const int* const input_lengths,
                               int alphabet_size, int minibatch,
                               ctcOptions info,
                               size_t* size_bytes);

*/

    size_t workspace_size;
    ctcStatus_t status;
    status = get_workspace_size(label_lengths.data<int>(), input_lengths.data<int>(),
				(int) alphabet_size, batch_size,
				options, &workspace_size);

    if (status != CTC_STATUS_SUCCESS) {
       at::runtime_error(ctcGetStatusString(status));
    }
   
    at::Tensor workspace = activations.type().toScalarType(at::kByte).tensor(workspace_size);

    at::Tensor costs = activations.type().toScalarType(at::kFloat).tensor(batch_size);
    at::Tensor gradients = activations.type().toScalarType(at::kFloat).tensor(activations.sizes());

    status = compute_ctc_loss(activations.data<float>(), gradients.data<float>(),
			      labels.data<int>(), label_lengths.data<int>(),
			      input_lengths.data<int>(), alphabet_size,
			      batch_size, costs.data<float>(),
			      workspace.data_ptr(), options);

    if (status != CTC_STATUS_SUCCESS) {
       at::runtime_error(ctcGetStatusString(status));
    }
    return std::make_tuple(costs, gradients);

   /*
ctcStatus_t compute_ctc_loss(const float* const activations,
                             float* gradients,
                             const int* const flat_labels,
                             const int* const label_lengths,
                             const int* const input_lengths,
                             int alphabet_size,
                             int minibatch,
                             float *costs,
                             void *workspace,
                             ctcOptions options);
*/


}


PYBIND11_MODULE(TORCH_EXTENSION_NAME, m) {
  m.def("ctc", &ctc, "CTC");
}