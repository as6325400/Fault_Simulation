#pragma once

#include <cuda_runtime.h>

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "algorithm/fault_simulator.hpp"
#include "core/circuit.hpp"
#include "io/pattern_loader.hpp"

namespace algorithm {

namespace gpu_detail {

struct DeviceGate {
    int output{0};
    int input_offset{0};
    int input_count{0};
    int op_kind{0};  // 0=and,1=or,2=xor,3=buf
    int invert{0};   // 1 means invert result
    int op_code{0};  // 4-bit truth table for 2-input gates
    uint32_t mask11{0};
    uint32_t mask10{0};
    uint32_t mask01{0};
    uint32_t mask00{0};
};

inline void cudaCheck(cudaError_t err, const char* expr, const char* file, int line) {
    if (err == cudaSuccess) {
        return;
    }
    std::string msg = std::string(expr) + " failed with " + cudaGetErrorString(err) + " at " +
                      file + ":" + std::to_string(line);
    throw std::runtime_error(msg);
}

#define GPU_CUDA_CHECK(expr) ::algorithm::gpu_detail::cudaCheck((expr), #expr, __FILE__, __LINE__)

template <int BITS>
__device__ uint32_t evalGate(const DeviceGate& gate,
                             const int* gate_inputs,
                             const uint32_t* values,
                             uint32_t mask) {
    auto input_at = [&](int idx) { return values[gate_inputs[gate.input_offset + idx]]; };

    if (gate.input_count == 2) {
        const uint32_t a = input_at(0);
        const uint32_t b = input_at(1);
        // maskXX already precomputed to 0xFFFFFFFF or 0.
        return ((a & b & gate.mask11) | (a & ~b & gate.mask10) | (~a & b & gate.mask01) |
                (~a & ~b & gate.mask00)) &
               mask;
    }

    return 1;

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
        v = (~v) & mask;
    } else {
        v &= mask;
    }
    return v;
}

__device__ __forceinline__ DeviceGate loadGate(const DeviceGate* gates, int idx) {
    DeviceGate g;
#if __CUDA_ARCH__ >= 350
    g.output = __ldg(&gates[idx].output);
    g.input_offset = __ldg(&gates[idx].input_offset);
    g.input_count = __ldg(&gates[idx].input_count);
    g.op_kind = __ldg(&gates[idx].op_kind);
    g.invert = __ldg(&gates[idx].invert);
    g.op_code = __ldg(&gates[idx].op_code);
    g.mask11 = __ldg(&gates[idx].mask11);
    g.mask10 = __ldg(&gates[idx].mask10);
    g.mask01 = __ldg(&gates[idx].mask01);
    g.mask00 = __ldg(&gates[idx].mask00);
#else
    g = gates[idx];
#endif
    return g;
}

template <int BITS>
__device__ uint32_t runFault(int fault_net,
                             uint32_t forced_value,
                             const DeviceGate* gates,
                             const int* gate_inputs,
                             const int* gate_order,
                             int gate_count,
                             uint32_t* values,
                             uint32_t mask,
                             const int* outputs,
                             int output_count,
                             const uint32_t* provided_values) {
    values[fault_net] = forced_value;
    for (int idx = 0; idx < gate_count; ++idx) {
        const int gate_idx = gate_order[idx];
        const DeviceGate& gate = gates[gate_idx];
        if (gate.output == fault_net) {
            values[gate.output] = forced_value;
            continue;
        }
        const uint32_t out = evalGate<BITS>(gate, gate_inputs, values, mask);
        values[gate.output] = out;
    }

    uint32_t eq = mask;
    for (int i = 0; i < output_count; ++i) {
        const uint32_t v = values[outputs[i]];
        eq &= ~(v ^ provided_values[i]) & mask;
    }
    return eq;
}

template <int BITS>
__global__ void simulateFaultKernel(const DeviceGate* __restrict__ gates,
                                    const int* __restrict__ gate_inputs,
                                    const int* __restrict__ gate_order,
                                    int gate_count,
                                    const int* __restrict__ level_index,
                                    const int* __restrict__ level_offsets,
                                    int level_count,
                                    const int* __restrict__ primary_inputs,
                                    int primary_input_count,
                                    const int* __restrict__ outputs,
                                    int output_count,
                                    const uint32_t* __restrict__ base_matrix,
                                    const uint32_t* __restrict__ batch_masks,
                                    const uint32_t* __restrict__ provided_matrix,
                                    uint32_t* __restrict__ stuck0_matrix,
                                    uint32_t* __restrict__ stuck1_matrix,
                                    uint32_t* __restrict__ values_matrix,
                                    int value_net_count,
                                    int fault_net_count,
                                    int batch_count,
                                    int use_shared_values,
                                    int logical_warps,
                                    int warps_per_block) {
    const int lane = static_cast<int>(threadIdx.x) & (warpSize - 1);
    const int warp_in_block = static_cast<int>(threadIdx.x) >> 5;
    const int logical_warp = static_cast<int>(blockIdx.x) * warps_per_block + warp_in_block;
    if (warp_in_block >= warps_per_block || logical_warp >= logical_warps) {
        return;
    }
    (void)gate_order;
    (void)gate_count;

    uint32_t* values0 = nullptr;
    uint32_t* values1 = nullptr;
    if (use_shared_values) {
        extern __shared__ uint32_t shared_values[];
        uint32_t* warp_base =
            shared_values + static_cast<std::size_t>(warp_in_block) * value_net_count * 2;
        values0 = warp_base;
        values1 = warp_base + value_net_count;
    } else {
        uint32_t* block_base =
            values_matrix + static_cast<std::size_t>(logical_warp) * value_net_count * 2;
        values0 = block_base;
        values1 = block_base + value_net_count;
    }

    for (int net = logical_warp; net < fault_net_count; net += gridDim.x * warps_per_block) {
        for (int batch = 0; batch < batch_count; ++batch) {
#if __CUDA_ARCH__ >= 350
            const uint32_t mask = __ldg(&batch_masks[batch]);
#else
            const uint32_t mask = batch_masks[batch];
#endif
            const uint32_t* base_values =
                base_matrix + static_cast<std::size_t>(batch) * value_net_count;
            const uint32_t* provided_values =
                provided_matrix ? provided_matrix + static_cast<std::size_t>(batch) * output_count
                                : nullptr;

            for (int idx = lane; idx < primary_input_count; idx += warpSize) {
                const int pi = primary_inputs[idx];
#if __CUDA_ARCH__ >= 350
                const uint32_t v = __ldg(&base_values[pi]);
#else
                const uint32_t v = base_values[pi];
#endif
                values0[pi] = v;
                values1[pi] = v;
            }

            if (lane == 0) {
                values0[net] = 0u;
                values1[net] = mask;
            }

            for (int level = 0; level < level_count; ++level) {
                const int start = level_offsets[level];
                const int end = level_offsets[level + 1];
                for (int idx = start + lane; idx < end; idx += warpSize) {
                    const int gate_idx = level_index[idx];
                    const DeviceGate gate = loadGate(gates, gate_idx);
                    if (gate.output == net) {
                        values0[gate.output] = 0u;
                        values1[gate.output] = mask;
                    } else {
                        const uint32_t out0 = evalGate<BITS>(gate, gate_inputs, values0, mask);
                        const uint32_t out1 = evalGate<BITS>(gate, gate_inputs, values1, mask);
                        values0[gate.output] = out0;
                        values1[gate.output] = out1;
                    }
                }
            }

            if (lane == 0) {
                uint32_t eq0 = mask;
                uint32_t eq1 = mask;
                if (provided_values) {
                    for (int i = 0; i < output_count; ++i) {
                        const int o = outputs[i];
                        const uint32_t pv =
#if __CUDA_ARCH__ >= 350
                            __ldg(&provided_values[i]);
#else
                            provided_values[i];
#endif
                        eq0 &= ~(values0[o] ^ pv) & mask;
                        eq1 &= ~(values1[o] ^ pv) & mask;
                    }
                }
                const std::size_t out_idx =
                    static_cast<std::size_t>(batch) * fault_net_count + net;
                stuck0_matrix[out_idx] = eq0;
                stuck1_matrix[out_idx] = eq1;
            }
        }
    }
}

}  // namespace gpu_detail

template <int BITS>
class BatchGpuFaultSimulator : public FaultSimulator {
public:
    BatchGpuFaultSimulator(const core::Circuit& circuit, const std::vector<io::PatternRow>& rows)
        : FaultSimulator(circuit, rows), fault_net_count_(circuit_.netCount()) {
        if (fault_net_count_ > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            throw std::runtime_error("Net count exceeds GPU simulator limits");
        }
        prepareWorkingGates();
        if (working_net_count_ > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            throw std::runtime_error("Transformed net count exceeds GPU simulator limits");
        }

        net_to_gate_.assign(working_net_count_, -1);
        for (std::size_t i = 0; i < working_gates_.size(); ++i) {
            net_to_gate_[working_gates_[i].output] = static_cast<int>(i);
        }

        output_index_by_net_.assign(fault_net_count_, -1);
        outputs_.reserve(circuit_.primaryOutputs().size());
        for (std::size_t i = 0; i < circuit_.primaryOutputs().size(); ++i) {
            const auto net = circuit_.primaryOutputs()[i];
            outputs_.push_back(static_cast<int>(net));
            output_index_by_net_[net] = static_cast<int>(i);
        }
        primary_inputs_.reserve(circuit_.primaryInputs().size());
        for (auto net : circuit_.primaryInputs()) {
            primary_inputs_.push_back(static_cast<int>(net));
        }

        pattern_batches_ =
            (rows_.size() + static_cast<std::size_t>(BITS) - 1) / static_cast<std::size_t>(BITS);
        buildTopology();
        buildLevels();
        uploadStaticData();
        ensureWorkBuffers();
    }

    ~BatchGpuFaultSimulator() override {
        if (d_gates_) {
            GPU_CUDA_CHECK(cudaFree(d_gates_));
        }
        if (d_gate_inputs_) {
            GPU_CUDA_CHECK(cudaFree(d_gate_inputs_));
        }
        if (d_gate_order_) {
            GPU_CUDA_CHECK(cudaFree(d_gate_order_));
        }
        if (d_outputs_) {
            GPU_CUDA_CHECK(cudaFree(d_outputs_));
        }
        if (d_primary_inputs_) {
            GPU_CUDA_CHECK(cudaFree(d_primary_inputs_));
        }
        if (d_level_index_) {
            GPU_CUDA_CHECK(cudaFree(d_level_index_));
        }
        if (d_level_offsets_) {
            GPU_CUDA_CHECK(cudaFree(d_level_offsets_));
        }
        if (d_base_matrix_) {
            GPU_CUDA_CHECK(cudaFree(d_base_matrix_));
        }
        if (d_provided_matrix_) {
            GPU_CUDA_CHECK(cudaFree(d_provided_matrix_));
        }
        if (d_batch_masks_) {
            GPU_CUDA_CHECK(cudaFree(d_batch_masks_));
        }
        if (d_stuck0_matrix_) {
            GPU_CUDA_CHECK(cudaFree(d_stuck0_matrix_));
        }
        if (d_stuck1_matrix_) {
            GPU_CUDA_CHECK(cudaFree(d_stuck1_matrix_));
        }
        if (d_values_matrix_) {
            GPU_CUDA_CHECK(cudaFree(d_values_matrix_));
        }
    }

    void start() override {
        const std::size_t out_count = outputs_.size();
        const std::size_t batch_count = pattern_batches_;
        if (batch_count == 0 || fault_net_count_ == 0) {
            return;
        }

        std::vector<uint32_t> base_matrix(batch_count * working_net_count_, 0u);
        std::vector<uint32_t> provided_matrix(batch_count * out_count, 0u);
        std::vector<uint32_t> batch_masks(batch_count, 0u);
        std::vector<uint32_t> host_stuck0(batch_count * fault_net_count_, 0u);
        std::vector<uint32_t> host_stuck1(batch_count * fault_net_count_, 0u);

        for (std::size_t batch = 0; batch < batch_count; ++batch) {
            const std::size_t base = batch * static_cast<std::size_t>(BITS);
            const std::size_t chunk_size = std::min<std::size_t>(BITS, rows_.size() - base);
            const uint32_t mask = (chunk_size >= 32) ? 0xFFFFFFFFu
                                                     : ((uint32_t{1} << chunk_size) - 1u);
            batch_masks[batch] = mask;

            for (std::size_t offset = 0; offset < chunk_size; ++offset) {
                const auto& row = rows_[base + offset];
                const uint32_t bit = uint32_t{1} << offset;
                for (const auto& entry : row.pattern.assignments) {
                    if (entry.value) {
                        base_matrix[batch * working_net_count_ + entry.net] |= bit;
                    }
                }
                if (out_count > 0) {
                    for (const auto& kv : row.provided_outputs) {
                        const int idx = output_index_by_net_[kv.first];
                        if (idx >= 0 && kv.second) {
                            provided_matrix[batch * out_count +
                                            static_cast<std::size_t>(idx)] |= bit;
                        }
                    }
                }
            }
        }

        GPU_CUDA_CHECK(cudaMemcpy(d_base_matrix_, base_matrix.data(),
                                  base_matrix.size() * sizeof(uint32_t),
                                  cudaMemcpyHostToDevice));
        if (!provided_matrix.empty()) {
            GPU_CUDA_CHECK(cudaMemcpy(d_provided_matrix_, provided_matrix.data(),
                                      provided_matrix.size() * sizeof(uint32_t),
                                      cudaMemcpyHostToDevice));
        }
        GPU_CUDA_CHECK(cudaMemcpy(d_batch_masks_, batch_masks.data(),
                                  batch_masks.size() * sizeof(uint32_t),
                                  cudaMemcpyHostToDevice));

        const std::size_t logical_warps =
            std::min<std::size_t>(workspace_blocks_, fault_net_count_);
        int shared_limit = 0;
        int device = 0;
        int warp_width = 0;
        GPU_CUDA_CHECK(cudaGetDevice(&device));
        GPU_CUDA_CHECK(
            cudaDeviceGetAttribute(&shared_limit, cudaDevAttrMaxSharedMemoryPerBlock, device));
        GPU_CUDA_CHECK(cudaDeviceGetAttribute(&warp_width, cudaDevAttrWarpSize, device));
        if (warp_width <= 0) {
            warp_width = 32;
        }
        const std::size_t shared_needed = working_net_count_ * 2 * sizeof(uint32_t);
        // Only use shared when it fits comfortably to keep occupancy (half of the limit).
        const bool use_shared =
            shared_needed > 0 && shared_needed <= static_cast<std::size_t>(shared_limit / 2);
        const int warps_per_block = use_shared ? 1 : 4;
        const std::size_t blocks =
            (logical_warps + static_cast<std::size_t>(warps_per_block) - 1) /
            static_cast<std::size_t>(warps_per_block);
        dim3 block(static_cast<unsigned>(warps_per_block * warp_width));
        dim3 grid(static_cast<unsigned>(blocks));
        const std::size_t shared_bytes = use_shared ? shared_needed : 0;
#ifdef GPU_FORCE_BINARY_FANIN
        std::cout << describeGateInputStats() << '\n';
        std::cout << describeMemoryTraffic(use_shared, warps_per_block) << '\n';
#endif
        gpu_detail::simulateFaultKernel<BITS><<<grid, block, shared_bytes>>>(
            d_gates_, d_gate_inputs_, d_gate_order_, static_cast<int>(gates_.size()),
            d_level_index_, d_level_offsets_, static_cast<int>(level_offsets_.size() - 1),
            d_primary_inputs_, static_cast<int>(primary_inputs_.size()),
            d_outputs_, static_cast<int>(outputs_.size()), d_base_matrix_, d_batch_masks_,
            out_count ? d_provided_matrix_ : nullptr, d_stuck0_matrix_, d_stuck1_matrix_,
            d_values_matrix_, static_cast<int>(working_net_count_),
            static_cast<int>(fault_net_count_), static_cast<int>(batch_count),
            use_shared ? 1 : 0, static_cast<int>(logical_warps), warps_per_block);
        GPU_CUDA_CHECK(cudaGetLastError());
        GPU_CUDA_CHECK(cudaDeviceSynchronize());

        GPU_CUDA_CHECK(cudaMemcpy(host_stuck0.data(), d_stuck0_matrix_,
                                  host_stuck0.size() * sizeof(uint32_t),
                                  cudaMemcpyDeviceToHost));
        GPU_CUDA_CHECK(cudaMemcpy(host_stuck1.data(), d_stuck1_matrix_,
                                  host_stuck1.size() * sizeof(uint32_t),
                                  cudaMemcpyDeviceToHost));

        for (std::size_t batch = 0; batch < batch_count; ++batch) {
            const std::size_t base = batch * static_cast<std::size_t>(BITS);
            const std::size_t chunk_size = std::min<std::size_t>(BITS, rows_.size() - base);
            const uint32_t* batch_stuck0 = host_stuck0.data() + batch * fault_net_count_;
            const uint32_t* batch_stuck1 = host_stuck1.data() + batch * fault_net_count_;
            for (std::size_t net = 0; net < fault_net_count_; ++net) {
                const uint32_t eq0 = batch_stuck0[net];
                const uint32_t eq1 = batch_stuck1[net];
                for (std::size_t offset = 0; offset < chunk_size; ++offset) {
                    const bool bit0 = ((eq0 >> offset) & 1u) != 0u;
                    const bool bit1 = ((eq1 >> offset) & 1u) != 0u;
                    answers.set(base + offset, net, true, bit0);
                    answers.set(base + offset, net, false, bit1);
                }
            }
        }
    }

    std::string describeGateInputStats() const {
        const std::size_t original_total = circuit_.gates().size();
        const std::size_t transformed_total = working_gates_.size();
        auto render = [](const std::vector<std::size_t>& hist, std::size_t total) {
            std::ostringstream oss;
            bool first = true;
            for (std::size_t fanin = 0; fanin < hist.size(); ++fanin) {
                if (hist[fanin] == 0) {
                    continue;
                }
                if (!first) {
                    oss << ", ";
                }
                oss << fanin << "in=" << hist[fanin];
                if (total > 0) {
                    const double pct =
                        (static_cast<double>(hist[fanin]) * 100.0) / static_cast<double>(total);
                    oss << " (" << std::fixed << std::setprecision(1) << pct << "%)";
                }
                first = false;
            }
            return oss.str();
        };

        const std::size_t gate_delta =
            (transformed_total >= original_total) ? (transformed_total - original_total)
                                                  : (original_total - transformed_total);
        const std::size_t net_delta =
            (working_net_count_ >= fault_net_count_) ? (working_net_count_ - fault_net_count_)
                                                     : (fault_net_count_ - working_net_count_);
        const int depth_delta =
            (working_max_level_ >= original_max_level_) ? (working_max_level_ - original_max_level_)
                                                        : (original_max_level_ - working_max_level_);
        const std::size_t warps = std::min<std::size_t>(workspace_blocks_, fault_net_count_);
        const std::size_t value_bytes = working_net_count_ * warps * 2 * sizeof(uint32_t);
        const std::size_t base_bytes = working_net_count_ * pattern_batches_ * sizeof(uint32_t);
        const std::size_t stuck_bytes = fault_net_count_ * pattern_batches_ * sizeof(uint32_t);
        const std::size_t shared_bytes_needed = working_net_count_ * 2 * sizeof(uint32_t);

        std::ostringstream oss;
        oss << "original gates=" << original_total << " ["
            << render(original_fanin_hist_, original_total) << "], depth=" << original_max_level_
            << ", nets=" << fault_net_count_ << "; ";
        oss << "transformed gates=" << transformed_total << " ["
            << render(working_fanin_hist_, transformed_total) << "], depth=" << working_max_level_
            << ", nets=" << working_net_count_;
        oss << " (gate_delta=" << (transformed_total >= original_total ? "+" : "-") << gate_delta
            << ", net_delta=" << (working_net_count_ >= fault_net_count_ ? "+" : "-") << net_delta
            << ", depth_delta=" << (working_max_level_ >= original_max_level_ ? "+" : "-")
            << depth_delta << ")";
        oss << "; buffers: value_bytes=" << value_bytes << " ("
            << warps << " warps), base_bytes=" << base_bytes
            << ", stuck_bytes=" << stuck_bytes
            << ", shared_bytes_if_used=" << shared_bytes_needed;
        return oss.str();
    }

    std::string describeMemoryTraffic(bool use_shared, int warps_per_block) const {
        const std::size_t per_fault_batch_loads = working_total_inputs_ * 2;   // values0/values1
        const std::size_t per_fault_batch_stores = working_gates_.size() * 2;  // values0/values1
        const unsigned long long total_loads =
            static_cast<unsigned long long>(per_fault_batch_loads) *
            static_cast<unsigned long long>(fault_net_count_) *
            static_cast<unsigned long long>(pattern_batches_);
        const unsigned long long total_stores =
            static_cast<unsigned long long>(per_fault_batch_stores) *
            static_cast<unsigned long long>(fault_net_count_) *
            static_cast<unsigned long long>(pattern_batches_);

        std::ostringstream oss;
        oss << "value_traffic ("
            << (use_shared ? "shared" : "global")
            << ", warps_per_block=" << warps_per_block << "): per(fault,batch) loads="
            << per_fault_batch_loads << " stores=" << per_fault_batch_stores
            << "; total loads=" << total_loads << " stores=" << total_stores;
        return oss.str();
    }

private:
    void prepareWorkingGates() {
        working_net_count_ = fault_net_count_;
        original_fanin_hist_ = faninHistogram(circuit_.gates());
        original_max_level_ = computeMaxLevel(circuit_.gates(), fault_net_count_);
        original_total_inputs_ = countTotalInputs(circuit_.gates());
#ifdef GPU_FORCE_BINARY_FANIN
        working_gates_ = binarizeGates(circuit_.gates(), working_net_count_);
#else
        working_gates_.assign(circuit_.gates().begin(), circuit_.gates().end());
#endif
        working_fanin_hist_ = faninHistogram(working_gates_);
        working_total_inputs_ = countTotalInputs(working_gates_);
    }

    std::vector<core::Gate> binarizeGates(const std::vector<core::Gate>& source,
                                          std::size_t& net_count) const {
        std::vector<core::Gate> result;
        result.reserve(source.size());
        std::size_t next_net = net_count;
        auto nextNetId = [&]() -> std::size_t {
            if (next_net == std::numeric_limits<std::size_t>::max()) {
                throw std::runtime_error("Net id overflow during gate binarization");
            }
            return next_net++;
        };
        auto emitChain = [&](const core::Gate& g, core::GateType mid_type,
                             core::GateType last_type) {
            core::NetId acc = g.inputs[0];
            for (std::size_t i = 1; i < g.inputs.size(); ++i) {
                core::Gate new_gate;
                new_gate.type = (i + 1 == g.inputs.size()) ? last_type : mid_type;
                new_gate.name = g.name;
                new_gate.output = (i + 1 == g.inputs.size()) ? g.output : nextNetId();
                new_gate.inputs = {acc, g.inputs[i]};
                result.push_back(std::move(new_gate));
                acc = result.back().output;
            }
        };

        for (const auto& g : source) {
            const std::size_t fanin = g.inputs.size();
            if (fanin == 0) {
                result.push_back(g);
                continue;
            }
            if (fanin == 1) {
                if (g.type != core::GateType::Buf && g.type != core::GateType::Not) {
                    throw std::runtime_error("Unsupported single-input gate type for binary fanin mode");
                }
                core::Gate dup = g;
                dup.inputs = {g.inputs[0], g.inputs[0]};
                result.push_back(std::move(dup));
                continue;
            }
            if (fanin == 2) {
                result.push_back(g);
                continue;
            }
            switch (g.type) {
                case core::GateType::And:
                    emitChain(g, core::GateType::And, core::GateType::And);
                    break;
                case core::GateType::Nand:
                    emitChain(g, core::GateType::And, core::GateType::Nand);
                    break;
                case core::GateType::Or:
                    emitChain(g, core::GateType::Or, core::GateType::Or);
                    break;
                case core::GateType::Nor:
                    emitChain(g, core::GateType::Or, core::GateType::Nor);
                    break;
                case core::GateType::Xor:
                    emitChain(g, core::GateType::Xor, core::GateType::Xor);
                    break;
                case core::GateType::Xnor:
                    emitChain(g, core::GateType::Xor, core::GateType::Xnor);
                    break;
                case core::GateType::Buf:
                case core::GateType::Not:
                    throw std::runtime_error("Unexpected multi-input BUF/NOT gate in GPU simulator");
                case core::GateType::Unknown:
                default:
                    throw std::runtime_error("Unsupported gate type for GPU simulator");
            }
        }
        net_count = next_net;
        return result;
    }

    static std::vector<std::size_t> faninHistogram(const std::vector<core::Gate>& gates) {
        std::size_t max_fanin = 0;
        for (const auto& g : gates) {
            max_fanin = std::max<std::size_t>(max_fanin, g.inputs.size());
        }
        std::vector<std::size_t> hist(max_fanin + 1, 0);
        for (const auto& g : gates) {
            ++hist[g.inputs.size()];
        }
        return hist;
    }

    static std::size_t countTotalInputs(const std::vector<core::Gate>& gates) {
        std::size_t sum = 0;
        for (const auto& g : gates) {
            sum += g.inputs.size();
        }
        return sum;
    }

    static int computeMaxLevel(const std::vector<core::Gate>& gates, std::size_t net_count) {
        std::vector<int> net_to_gate(net_count, -1);
        for (std::size_t i = 0; i < gates.size(); ++i) {
            net_to_gate[gates[i].output] = static_cast<int>(i);
        }
        std::vector<int> indegree(gates.size(), 0);
        std::vector<std::vector<int>> adj(gates.size());
        for (std::size_t i = 0; i < gates.size(); ++i) {
            for (auto net : gates[i].inputs) {
                const int driver = net_to_gate[net];
                if (driver >= 0) {
                    adj[static_cast<std::size_t>(driver)].push_back(static_cast<int>(i));
                    ++indegree[i];
                }
            }
        }
        std::queue<int> q;
        for (std::size_t i = 0; i < indegree.size(); ++i) {
            if (indegree[i] == 0) {
                q.push(static_cast<int>(i));
            }
        }
        std::vector<int> level(gates.size(), 1);
        std::size_t visited = 0;
        int max_level = 0;
        while (!q.empty()) {
            const int g = q.front();
            q.pop();
            ++visited;
            const int cur_level = level[static_cast<std::size_t>(g)];
            max_level = std::max(max_level, cur_level);
            for (int dst : adj[static_cast<std::size_t>(g)]) {
                level[static_cast<std::size_t>(dst)] =
                    std::max(level[static_cast<std::size_t>(dst)], cur_level + 1);
                --indegree[static_cast<std::size_t>(dst)];
                if (indegree[static_cast<std::size_t>(dst)] == 0) {
                    q.push(dst);
                }
            }
        }
        if (visited != gates.size()) {
            throw std::runtime_error("Topological order failed in depth calculation");
        }
        return max_level;
    }

    void buildTopology() {
        const auto& source_gates = working_gates_;
        gates_.clear();
        gate_inputs_.clear();
        gates_.reserve(source_gates.size());
        auto gateOpcode = [](core::GateType type) {
            switch (type) {
                case core::GateType::And:
                    return 0b1000;
                case core::GateType::Nand:
                    return 0b0111;
                case core::GateType::Or:
                    return 0b1110;
                case core::GateType::Nor:
                    return 0b0001;
                case core::GateType::Xor:
                    return 0b0110;
                case core::GateType::Xnor:
                    return 0b1001;
                case core::GateType::Buf:
                    return 0b1100;
                case core::GateType::Not:
                    return 0b0011;
                case core::GateType::Unknown:
                default:
                    return 0;
            }
        };
        auto fillMasks = [](gpu_detail::DeviceGate& g, int opcode) {
            const uint32_t t3 = static_cast<uint32_t>(-((opcode >> 3) & 1));
            const uint32_t t2 = static_cast<uint32_t>(-((opcode >> 2) & 1));
            const uint32_t t1 = static_cast<uint32_t>(-((opcode >> 1) & 1));
            const uint32_t t0 = static_cast<uint32_t>(-((opcode >> 0) & 1));
            g.mask11 = t3;
            g.mask10 = t2;
            g.mask01 = t1;
            g.mask00 = t0;
        };
        for (const auto& gate : source_gates) {
            gpu_detail::DeviceGate g;
            g.output = static_cast<int>(gate.output);
            g.input_offset = static_cast<int>(gate_inputs_.size());
            g.input_count = static_cast<int>(gate.inputs.size());
            g.op_code = gateOpcode(gate.type);
            if (g.input_count == 2) {
                fillMasks(g, g.op_code);
            }
            switch (gate.type) {
                case core::GateType::And:
                    g.op_kind = 0;
                    g.invert = 0;
                    break;
                case core::GateType::Nand:
                    g.op_kind = 0;
                    g.invert = 1;
                    break;
                case core::GateType::Or:
                    g.op_kind = 1;
                    g.invert = 0;
                    break;
                case core::GateType::Nor:
                    g.op_kind = 1;
                    g.invert = 1;
                    break;
                case core::GateType::Xor:
                    g.op_kind = 2;
                    g.invert = 0;
                    break;
                case core::GateType::Xnor:
                    g.op_kind = 2;
                    g.invert = 1;
                    break;
                case core::GateType::Buf:
                    g.op_kind = 3;
                    g.invert = 0;
                    break;
                case core::GateType::Not:
                    g.op_kind = 3;
                    g.invert = 1;
                    break;
                case core::GateType::Unknown:
                default:
                    throw std::runtime_error("Unsupported gate type for GPU simulator");
            }
            for (auto net : gate.inputs) {
                gate_inputs_.push_back(static_cast<int>(net));
            }
            gates_.push_back(g);
        }

        std::vector<int> indegree(gates_.size(), 0);
        std::vector<std::vector<int>> adj(gates_.size());
        for (std::size_t i = 0; i < source_gates.size(); ++i) {
            for (auto net : source_gates[i].inputs) {
                const int driver = net_to_gate_[net];
                if (driver >= 0) {
                    adj[static_cast<std::size_t>(driver)].push_back(static_cast<int>(i));
                    ++indegree[i];
                }
            }
        }

        std::queue<int> q;
        for (std::size_t i = 0; i < indegree.size(); ++i) {
            if (indegree[i] == 0) {
                q.push(static_cast<int>(i));
            }
        }
        gate_order_.clear();
        gate_order_.reserve(gates_.size());
        while (!q.empty()) {
            const int g = q.front();
            q.pop();
            gate_order_.push_back(g);
            for (int dst : adj[static_cast<std::size_t>(g)]) {
                --indegree[static_cast<std::size_t>(dst)];
                if (indegree[static_cast<std::size_t>(dst)] == 0) {
                    q.push(dst);
                }
            }
        }

        if (gate_order_.size() != gates_.size()) {
            throw std::runtime_error("Topological order failed for GPU simulator (possible cycle)");
        }
    }

    void buildLevels() {
        level_offsets_.clear();
        level_offsets_.push_back(0);
        std::vector<int> level_of_gate(gates_.size(), 0);
        const auto& source_gates = working_gates_;
        for (std::size_t i = 0; i < source_gates.size(); ++i) {
            int max_parent = 0;
            for (auto net : source_gates[i].inputs) {
                const int parent_gate = net_to_gate_[net];
                if (parent_gate >= 0) {
                    max_parent = std::max(max_parent, level_of_gate[static_cast<std::size_t>(parent_gate)]);
                }
            }
            level_of_gate[i] = max_parent + 1;
        }

        int max_level = 0;
        for (int lv : level_of_gate) {
            max_level = std::max(max_level, lv);
        }
        working_max_level_ = max_level;
        std::vector<int> level_counts(static_cast<std::size_t>(max_level + 1), 0);
        for (int lv : level_of_gate) {
            ++level_counts[static_cast<std::size_t>(lv)];
        }
        for (int c : level_counts) {
            level_offsets_.push_back(level_offsets_.back() + c);
        }
        level_index_.assign(gates_.size(), 0);
        std::vector<int> cursor = level_offsets_;
        for (std::size_t i = 0; i < level_of_gate.size(); ++i) {
            const int lv = level_of_gate[i];
            level_index_[static_cast<std::size_t>(cursor[static_cast<std::size_t>(lv)]++)] = static_cast<int>(i);
        }
    }

    void uploadStaticData() {
        if (!gates_.empty()) {
            GPU_CUDA_CHECK(cudaMalloc(&d_gates_, gates_.size() * sizeof(gpu_detail::DeviceGate)));
            GPU_CUDA_CHECK(cudaMemcpy(d_gates_, gates_.data(),
                                      gates_.size() * sizeof(gpu_detail::DeviceGate),
                                      cudaMemcpyHostToDevice));
        }
        if (!gate_inputs_.empty()) {
            GPU_CUDA_CHECK(cudaMalloc(&d_gate_inputs_, gate_inputs_.size() * sizeof(int)));
            GPU_CUDA_CHECK(cudaMemcpy(d_gate_inputs_, gate_inputs_.data(),
                                      gate_inputs_.size() * sizeof(int),
                                      cudaMemcpyHostToDevice));
        }
        if (!gate_order_.empty()) {
            GPU_CUDA_CHECK(cudaMalloc(&d_gate_order_, gate_order_.size() * sizeof(int)));
            GPU_CUDA_CHECK(cudaMemcpy(d_gate_order_, gate_order_.data(),
                                      gate_order_.size() * sizeof(int),
                                      cudaMemcpyHostToDevice));
        }
        if (!outputs_.empty()) {
            GPU_CUDA_CHECK(cudaMalloc(&d_outputs_, outputs_.size() * sizeof(int)));
            GPU_CUDA_CHECK(cudaMemcpy(d_outputs_, outputs_.data(),
                                      outputs_.size() * sizeof(int), cudaMemcpyHostToDevice));
        }
        if (!primary_inputs_.empty()) {
            GPU_CUDA_CHECK(cudaMalloc(&d_primary_inputs_, primary_inputs_.size() * sizeof(int)));
            GPU_CUDA_CHECK(cudaMemcpy(d_primary_inputs_, primary_inputs_.data(),
                                      primary_inputs_.size() * sizeof(int),
                                      cudaMemcpyHostToDevice));
        }
        if (!level_index_.empty()) {
            GPU_CUDA_CHECK(cudaMalloc(&d_level_index_, level_index_.size() * sizeof(int)));
            GPU_CUDA_CHECK(cudaMemcpy(d_level_index_, level_index_.data(),
                                      level_index_.size() * sizeof(int),
                                      cudaMemcpyHostToDevice));
        }
        if (!level_offsets_.empty()) {
            GPU_CUDA_CHECK(cudaMalloc(&d_level_offsets_, level_offsets_.size() * sizeof(int)));
            GPU_CUDA_CHECK(cudaMemcpy(d_level_offsets_, level_offsets_.data(),
                                      level_offsets_.size() * sizeof(int),
                                      cudaMemcpyHostToDevice));
        }
    }

    void ensureWorkBuffers() {
        const std::size_t out_count = outputs_.size();
        if (fault_net_count_ == 0) {
            return;
        }
        const std::size_t warps = std::min<std::size_t>(workspace_blocks_, fault_net_count_);
        const std::size_t values_elems = working_net_count_ * warps * 2;
        if (warps == 0 ||
            (working_net_count_ > 0 && values_elems / working_net_count_ != warps * 2)) {
            throw std::runtime_error("Value buffer size overflow for GPU simulator");
        }
        if (values_elems > 0 && !d_values_matrix_) {
            GPU_CUDA_CHECK(cudaMalloc(&d_values_matrix_, values_elems * sizeof(uint32_t)));
        }

        if (pattern_batches_ == 0) {
            return;
        }

        const std::size_t base_span = working_net_count_ * pattern_batches_;
        const std::size_t stuck_span = fault_net_count_ * pattern_batches_;
        if (working_net_count_ > 0 && base_span / working_net_count_ != pattern_batches_) {
            throw std::runtime_error("Batch buffer size overflow for GPU simulator");
        }
        if (fault_net_count_ > 0 && stuck_span / fault_net_count_ != pattern_batches_) {
            throw std::runtime_error("Stuck buffer size overflow for GPU simulator");
        }

        if (!d_base_matrix_) {
            GPU_CUDA_CHECK(cudaMalloc(&d_base_matrix_, base_span * sizeof(uint32_t)));
        }
        if (out_count > 0 && !d_provided_matrix_) {
            GPU_CUDA_CHECK(
                cudaMalloc(&d_provided_matrix_, out_count * pattern_batches_ * sizeof(uint32_t)));
        }
        if (!d_batch_masks_) {
            GPU_CUDA_CHECK(cudaMalloc(&d_batch_masks_, pattern_batches_ * sizeof(uint32_t)));
        }
        if (!d_stuck0_matrix_) {
            GPU_CUDA_CHECK(cudaMalloc(&d_stuck0_matrix_, stuck_span * sizeof(uint32_t)));
        }
        if (!d_stuck1_matrix_) {
            GPU_CUDA_CHECK(cudaMalloc(&d_stuck1_matrix_, stuck_span * sizeof(uint32_t)));
        }
    }

    const std::size_t fault_net_count_;
    std::size_t working_net_count_{0};
    std::vector<core::Gate> working_gates_;
    std::vector<std::size_t> original_fanin_hist_;
    std::vector<std::size_t> working_fanin_hist_;
    int original_max_level_{0};
    int working_max_level_{0};
    std::size_t original_total_inputs_{0};
    std::size_t working_total_inputs_{0};

    std::vector<int> net_to_gate_;
    std::vector<int> output_index_by_net_;
    std::vector<gpu_detail::DeviceGate> gates_;
    std::vector<int> gate_inputs_;
    std::vector<int> gate_order_;
    std::vector<int> primary_inputs_;
    std::vector<int> outputs_;
    std::vector<int> level_index_;
    std::vector<int> level_offsets_;

    gpu_detail::DeviceGate* d_gates_{nullptr};
    int* d_gate_inputs_{nullptr};
    int* d_gate_order_{nullptr};
    int* d_primary_inputs_{nullptr};
    int* d_outputs_{nullptr};
    int* d_level_index_{nullptr};
    int* d_level_offsets_{nullptr};
    uint32_t* d_base_matrix_{nullptr};
    uint32_t* d_provided_matrix_{nullptr};
    uint32_t* d_batch_masks_{nullptr};
    uint32_t* d_stuck0_matrix_{nullptr};
    uint32_t* d_stuck1_matrix_{nullptr};
    uint32_t* d_values_matrix_{nullptr};

    std::size_t workspace_blocks_{2048};  // cap on logical warps issued for the kernel
    std::size_t pattern_batches_{0};
};

using Batch1GpuFaultSimulator = BatchGpuFaultSimulator<1>;
using Batch32GpuFaultSimulator = BatchGpuFaultSimulator<32>;

extern template class BatchGpuFaultSimulator<1>;
extern template class BatchGpuFaultSimulator<32>;

}  // namespace algorithm
