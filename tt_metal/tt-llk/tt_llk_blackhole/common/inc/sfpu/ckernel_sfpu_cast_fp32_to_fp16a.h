// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

#include "ckernel_addrmod.h"
#include "ckernel_ops.h"
#include "sfpi.h"

namespace ckernel
{
namespace sfpu
{

template <bool APPROXIMATION_MODE, int ITERATIONS>
inline void _cast_fp32_to_fp16a_(std::uint32_t dst_index_in, std::uint32_t dst_index_out, const int iterations)
{
    constexpr std::uint32_t SFP_DST_TILE_ROWS = 32;
#pragma GCC unroll 8
    for (int d = 0; d < iterations; d++)
    {
        sfpi::vFloat val                                 = sfpi::dst_reg[dst_index_in * SFP_DST_TILE_ROWS];
        sfpi::dst_reg[dst_index_out * SFP_DST_TILE_ROWS] = sfpi::reinterpret<sfpi::vFloat>(sfpi::float_to_fp16a(val, sfpi::RoundMode::NearestEven));
        sfpi::dst_reg++;
    }
}

} // namespace sfpu
} // namespace ckernel
