/**
 * \file
 * compiler/lib/Dialect/Kernel/Transforms/migrate/static_mem_alloc/arith_helper.h
 *
 * This file is part of MegCC, a deep learning compiler developed by Megvii.
 *
 * \copyright Copyright (c) 2021-2022 Megvii Inc. All rights reserved.
 */

#pragma once

#include <cmath>
#include <limits>
#include <type_traits>

namespace mlir {
namespace Kernel {
namespace migrate {

/*!
 * \brief div with rounding up; only positive numbers work
 */
template <typename T>
inline constexpr T divup(T a, T b) {
    return (a - 1) / b + 1;
}

/*!
 * \brief update dest if val is greater than it
 */
template <typename T>
inline bool update_max(T& dest, const T& val) {
    if (dest < val) {
        dest = val;
        return true;
    }
    return false;
}

/*!
 * \brief update dest if val is less than it
 */
template <typename T>
inline bool update_min(T& dest, const T& val) {
    if (val < dest) {
        dest = val;
        return true;
    }
    return false;
}

/*!
 * \brief align *val* to be multiples of *align*
 * \param align required alignment, which must be power of 2
 */
template <typename T>
static inline T get_aligned_power2(T val, T align) {
    auto d = val & (align - 1);
    val += (align - d) & (align - 1);
    return val;
}

/*!
 * \brief check float equal within given ULP(unit in the last place)
 */
template <class T>
static inline typename std::enable_if<!std::numeric_limits<T>::is_integer, bool>::type
almost_equal(T x, T y, int unit_last_place = 1) {
    return

            std::abs(x - y) < (std::numeric_limits<T>::epsilon() * std::abs(x + y) *
                               unit_last_place) ||
            std::abs(x - y) < std::numeric_limits<T>::min();
}

}  // namespace migrate
}  // namespace Kernel
}  // namespace mlir

// vim: syntax=cpp.doxygen foldmethod=marker foldmarker=f{{{,f}}}
