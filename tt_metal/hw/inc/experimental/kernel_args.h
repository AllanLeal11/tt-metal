// SPDX-FileCopyrightText: © 2026 Tenstorrent USA, Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Metal 2.0 kernel argument accessors.
//
// This header defines the device-side machinery for named kernel arguments:
//   - RtaArg<T> / CrtaArg<T> / CtaVal<T> accessor structs
//   - get_arg() overloads, one per accessor kind
//
// The accessor constants themselves (e.g., `args::input_ptr`) are emitted per-kernel into
// kernel_args_generated.h. That generated header is included before the user kernel source,
// so kernel code can write:
//
//   auto in  = get_arg(args::input_ptr);   // RTA
//   auto cnt = get_arg(args::tile_count);  // CRTA
//   auto bsz = get_arg(args::block_size);  // CTA (compile-time constant)
//
// The kernel source is identical regardless of whether an arg is dispatched via RTA, CRTA,
// or CTA — moving an arg between kinds only requires a host-side schema change. The
// get_arg() call is resolved via ADL on the accessor type's `experimental` namespace.
//
// NOTE: Phase 1 supports uint32_t args only. Phase 2 will generalize to arbitrary POD types
// via memcpy-based serialization.

#include "api/dataflow/dataflow_api.h"

namespace experimental {

// byte_offset is measured from the start of the *named* section of the dispatch buffer.
// Varargs live after the named section; see get_vararg() in kernel_args_generated.h.
template <typename T>
struct RtaArg {
    uint32_t byte_offset;
};

template <typename T>
struct CrtaArg {
    uint32_t byte_offset;
};

template <typename T>
struct CtaVal {
    T value;
};

template <typename T>
FORCE_INLINE T get_arg(RtaArg<T> arg) {
    static_assert(sizeof(T) == 4, "Phase 1 supports only 4-byte args");
    return *((tt_l1_ptr T*)(get_arg_addr(arg.byte_offset / sizeof(uint32_t))));
}

template <typename T>
FORCE_INLINE T get_arg(CrtaArg<T> arg) {
    static_assert(sizeof(T) == 4, "Phase 1 supports only 4-byte args");
    return *((tt_l1_ptr T*)(get_common_arg_addr(arg.byte_offset / sizeof(uint32_t))));
}

template <typename T>
FORCE_INLINE constexpr T get_arg(CtaVal<T> arg) {
    return arg.value;
}

}  // namespace experimental
