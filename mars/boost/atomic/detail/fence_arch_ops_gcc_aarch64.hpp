/*
 * Distributed under the Boost Software License, Version 1.0.
 * (See accompanying file LICENSE_1_0.txt or copy at
 * http://www.boost.org/LICENSE_1_0.txt)
 *
 * Copyright (c) 2020 Andrey Semashev
 */
/*!
 * \file   atomic/detail/fence_arch_ops_gcc_aarch64.hpp
 *
 * This header contains implementation of the \c fence_arch_operations struct.
 */

#ifndef BOOST_ATOMIC_DETAIL_FENCE_ARCH_OPS_GCC_AARCH64_HPP_INCLUDED_
#define BOOST_ATOMIC_DETAIL_FENCE_ARCH_OPS_GCC_AARCH64_HPP_INCLUDED_

#include <boost/memory_order.hpp>
#include <boost/atomic/detail/config.hpp>
#include <boost/atomic/detail/header.hpp>

#ifdef BOOST_HAS_PRAGMA_ONCE
#pragma once
#endif

namespace mars_boost {} namespace boost = mars_boost; namespace mars_boost {
namespace atomics {
namespace detail {

//! Fence operations for AArch64
struct fence_arch_operations_gcc_aarch64
{
    static BOOST_FORCEINLINE void thread_fence(memory_order order) BOOST_NOEXCEPT
    {
        if (order != memory_order_relaxed)
        {
            if (order == memory_order_consume || order == memory_order_acquire)
                __asm__ __volatile__ ("dmb ishld\n\t" ::: "memory");
            else
                __asm__ __volatile__ ("dmb ish\n\t" ::: "memory");
        }
    }

    static BOOST_FORCEINLINE void signal_fence(memory_order order) BOOST_NOEXCEPT
    {
        if (order != memory_order_relaxed)
            __asm__ __volatile__ ("" ::: "memory");
    }
};

typedef fence_arch_operations_gcc_aarch64 fence_arch_operations;

} // namespace detail
} // namespace atomics
} // namespace mars_boost

#include <boost/atomic/detail/footer.hpp>

#endif // BOOST_ATOMIC_DETAIL_FENCE_ARCH_OPS_GCC_AARCH64_HPP_INCLUDED_
