// SPDX-FileCopyrightText: © 2025 Tenstorrent USA, Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>
#include "ttnn/cpp/ttnn/kernel_lib/sfpu_helpers.hpp"

void kernel_main() {
    using namespace compute_kernel_lib;

    const uint32_t packed_scalar = get_arg_val<uint32_t>(0);
    const auto lambd = reinterpret_cast<const float*>(&packed_scalar);
    uint32_t per_core_block_cnt = get_compile_time_arg_val(0);
    uint32_t per_core_block_dim = get_compile_time_arg_val(1);

    constexpr uint32_t cb_input = static_cast<uint32_t>(tt::CBIndex::c_0);
    constexpr uint32_t cb_tmp0 = static_cast<uint32_t>(tt::CBIndex::c_1);
    constexpr uint32_t cb_output = static_cast<uint32_t>(tt::CBIndex::c_2);

    init_sfpu(cb_input, cb_output);

    // hardshrink(x) = x·1(x + λ < 0) + x·1(x − λ > 0)
    //
    // Chain 1 (→ cb_tmp0): ltz(λ + x) · x
    //   D0 = λ (FillScalar), D1 = x (Load, no pop — input reused in chain 2)
    //   D0 = D0 + D1 = λ + x  (SfpuAdd)
    //   D0 = ltz(D0)           (Ltz)
    //   D0 = D0 · D1 = ltz(λ+x)·x  (SfpuMul)
    //
    // Chain 2 (← cb_tmp0, → cb_output): gtz(x − λ) · x + chain1_result
    //   D1 = λ (FillScalar), D0 = x (Load WaitNoPop — idempotent, input still there)
    //   D0 = D0 − D1 = x − λ  (SfpuSub)
    //   D0 = gtz(D0)           (Gtz)
    //   D1 = x (Load NoWaitPop — reads input again, pops it)
    //   D0 = D0 · D1 = gtz(x−λ)·x  (SfpuMul)
    //   D1 = chain1_result (Load from cb_tmp0, WaitAndPop)
    //   D0 = D0 + D1 = hardshrink(x)  (SfpuAdd)

    for (uint32_t block_index = 0; block_index < per_core_block_cnt; block_index++) {
        auto chain1 = sfpu_chain(
            FillScalar<Dst::D0>{*lambd},
            Load<cb_input, Dst::D1, LoadPolicy::WaitNoPop>{},
            SfpuAdd<Dst::D0, Dst::D1, Dst::D0>{},
            Ltz<Dst::D0>{},
            SfpuMul<Dst::D0, Dst::D1, Dst::D0>{});

        sfpu_pipeline<SfpuOutputPolicy::PerTile, SfpuDataFormatReconfig::NONE, SfpuBatching::Disabled>(
            chain1, cb_tmp0, per_core_block_dim);

        auto chain2 = sfpu_chain(
            FillScalar<Dst::D1>{*lambd},
            Load<cb_input, Dst::D0, LoadPolicy::WaitNoPop>{},
            SfpuSub<Dst::D0, Dst::D1, Dst::D0>{},
            Gtz<Dst::D0>{},
            Load<cb_input, Dst::D1, LoadPolicy::NoWaitPop>{},
            SfpuMul<Dst::D0, Dst::D1, Dst::D0>{},
            Load<cb_tmp0, Dst::D1, LoadPolicy::WaitAndPop>{},
            SfpuAdd<Dst::D0, Dst::D1, Dst::D0>{});

        sfpu_pipeline<SfpuOutputPolicy::Bulk, SfpuDataFormatReconfig::NONE, SfpuBatching::Disabled>(
            chain2, cb_output, per_core_block_dim);
    }
}
