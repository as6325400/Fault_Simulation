#pragma once

#include "algorithm/batch_gpu_fault_common.cuh"

// Define GPU_FAULT_BRANCHY_EVAL32 to force the simple evalGate<32> specialization and bypass the
// multiplexer-based implementation in batch_gpu_fault_common.cuh.
#ifdef GPU_FAULT_BRANCHY_EVAL32
namespace algorithm {
namespace gpu_detail {

template <>
__device__ __forceinline__ uint32_t evalGate<32>(const DeviceGate& gate,
                                                 const int* gate_inputs,
                                                 const uint32_t* values,
                                                 uint32_t mask) {
    auto input_at = [&](int idx) { return values[gate_inputs[gate.input_offset + idx]]; };

    uint32_t v = 0;
    if (gate.op_kind == 0) {  // AND/NAND
        v = mask;
        for (int i = 0; i < gate.input_count; ++i) {
            v &= input_at(i);
        }
    } else if (gate.op_kind == 1) {  // OR/NOR
        v = 0;
        for (int i = 0; i < gate.input_count; ++i) {
            v |= input_at(i);
        }
    } else if (gate.op_kind == 2) {  // XOR/XNOR
        v = 0;
        for (int i = 0; i < gate.input_count; ++i) {
            v ^= input_at(i);
        }
    } else {  // BUF/NOT
        if (gate.input_count != 1) {
            return 0;
        }
        v = input_at(0);
    }

    if (gate.invert) {
        v = ~v;
    }
    return v & mask;
}

}  // namespace gpu_detail
}  // namespace algorithm
#endif  // GPU_FAULT_BRANCHY_EVAL32
