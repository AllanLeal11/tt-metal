// SPDX-FileCopyrightText: © 2026 Tenstorrent USA, Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>

#include "api/compute/bcast.h"
#include "api/compute/reduce.h"

#include "api/compute/eltwise_unary/negative.h"
#include "api/compute/pack.h"
#include "api/compute/reconfig_data_format.h"
#include "api/compute/tile_move_copy.h"
#include "experimental/circular_buffer.h"

void kernel_main() {
    uint32_t Ht = get_compile_time_arg_val(0);
    uint32_t Wt = get_compile_time_arg_val(1);
    uint32_t NC = get_compile_time_arg_val(2);

    constexpr uint32_t cb_input = tt::CBIndex::c_0;
    constexpr uint32_t cb_scaler = tt::CBIndex::c_2;
    constexpr uint32_t cb_output = tt::CBIndex::c_3;
    constexpr uint32_t cb_acc = tt::CBIndex::c_4;
    constexpr uint32_t cb_ineg = tt::CBIndex::c_5;

    experimental::CircularBuffer cb_input_obj(cb_input);
    experimental::CircularBuffer cb_scaler_obj(cb_scaler);
    experimental::CircularBuffer cb_output_obj(cb_output);
    experimental::CircularBuffer cb_acc_obj(cb_acc);
    experimental::CircularBuffer cb_ineg_obj(cb_ineg);

    constexpr int dst_acc = 0;

    compute_kernel_hw_startup(cb_input, cb_scaler, cb_output);
    pack_reconfig_data_format(cb_output);

    cb_scaler_obj.wait_front(1);
    for (uint32_t nc = 0; nc < NC; nc++) {
        constexpr int onetile = 1;
        for (uint32_t ht = 0; ht < Ht; ++ht) {
            for (uint32_t wt = 0; wt < Wt; ++wt) {
                cb_input_obj.wait_front(onetile);
                tile_regs_acquire();
                if (wt == 0 && (nc > 0 || ht > 0)) {
                    reconfig_data_format_srca(cb_input);
                }
                copy_tile_init(cb_input);
                copy_tile(cb_input, 0, dst_acc);
                negative_tile_init();
                negative_tile(dst_acc);
                tile_regs_wait();
                cb_input_obj.pop_front(onetile);
                cb_ineg_obj.reserve_back(onetile);
                tile_regs_commit();
                pack_reconfig_data_format(cb_ineg);
                pack_tile(dst_acc, cb_ineg);
                tile_regs_release();
                cb_ineg_obj.push_back(onetile);

                tile_regs_acquire();
                if (wt > 0) {
                    cb_acc_obj.wait_front(onetile);
                    copy_tile_init(cb_acc);
                    copy_tile(cb_acc, 0, dst_acc);
                }

                cb_ineg_obj.wait_front(onetile);
                reduce_init(cb_ineg, cb_scaler, cb_acc);
                reduce_tile(cb_ineg, cb_scaler, 0, 0, dst_acc);
                reduce_uninit(cb_ineg);
                tile_regs_wait();
                cb_ineg_obj.pop_front(onetile);
                if (wt > 0) {
                    cb_acc_obj.pop_front(onetile);
                }
                cb_acc_obj.reserve_back(onetile);
                tile_regs_commit();
                pack_reconfig_data_format(cb_acc);
                pack_tile(dst_acc, cb_acc);
                tile_regs_release();
                cb_acc_obj.push_back(onetile);
            }

            // Post-process: negate accumulator, then scale by user scalar.
            // Do NOT use mul_binary_tile with a tile copied from cb_scaler: generate_reduce_scaler only
            // fills the slots the reduce unpack expects; the rest of the tile is zero, so an elementwise
            // multiply zeros most of the tile (typically one 16-row face). mul_tiles_bcast_scalar applies
            // the scaler in the correct broadcast form (same pattern as welford_reduce_w.cpp).
            cb_acc_obj.wait_front(onetile);
            tile_regs_acquire();
            reconfig_data_format_srca(cb_acc);
            copy_tile_init(cb_acc);
            copy_tile(cb_acc, 0, dst_acc);
            negative_tile_init();
            negative_tile(dst_acc);
            tile_regs_commit();
            cb_acc_obj.pop_front(onetile);

            cb_acc_obj.reserve_back(onetile);
            tile_regs_wait();
            pack_reconfig_data_format(cb_acc);
            pack_tile(dst_acc, cb_acc);
            tile_regs_release();
            cb_acc_obj.push_back(onetile);

            cb_acc_obj.wait_front(onetile);
            tile_regs_acquire();
            reconfig_data_format_srca(cb_acc);
            mul_tiles_bcast_scalar_init_short(cb_acc, cb_scaler);
            mul_tiles_bcast_scalar(cb_acc, cb_scaler, 0, 0, dst_acc);
            tile_regs_commit();
            cb_acc_obj.pop_front(onetile);

            cb_output_obj.reserve_back(onetile);
            tile_regs_wait();
            pack_reconfig_data_format(cb_output);
            pack_tile(dst_acc, cb_output);
            tile_regs_release();
            cb_output_obj.push_back(onetile);
        }
    }
}
