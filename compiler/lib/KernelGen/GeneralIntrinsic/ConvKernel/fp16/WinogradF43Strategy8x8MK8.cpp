/**
 * \file
 * compiler/lib/KernelGen/GeneralIntrinsic/ConvKernel/Winograd/WinogradF43Strategy8x8MK8.cpp
 *
 * This file is part of MegCC, a deep learning compiler developed by Megvii.
 *
 * \copyright Copyright (c) 2021-2022 Megvii Inc. All rights reserved.
 */

#include <string>
#include "GeneralIntrinsic/Activation.h"
#include "GeneralIntrinsic/ConvKernel/ConvKernel.h"
#include "Utils/StringTemplate.h"
#include "compiler/KernelGen/KernelGen.h"

using namespace megcc;
using namespace KernelGen;
using namespace GeneralIntrinsic;

std::string WinogradF43Strategy8x8MK8::WeightTrans(
        const std::vector<std::string>& strs) {
    auto inptr = strs[0];
    auto outptr = strs[1];
    auto OC = strs[2];
    auto IC = strs[3];
    std::string filter_process = R"(
    const uint32_t  PACK_C_SIZE= 8;
    const uint32_t KERNEL_SIZE = 3;
    const uint32_t alpha = 4 + 3 - 1; 
    size_t OCB = ${OC} /  PACK_C_SIZE;
    size_t ICB = ${IC} /  PACK_C_SIZE;

    for (size_t ocb = 0; ocb < OCB; ocb++) {
        for (size_t icb = 0; icb < ICB; icb++) {
            for (size_t ic_inner = 0; ic_inner <  PACK_C_SIZE; ic_inner++) {
                const gi_float16_t* fptr = ${filter} + (ocb * ICB + icb) * KERNEL_SIZE *
                      KERNEL_SIZE *  PACK_C_SIZE *  PACK_C_SIZE +
                      ic_inner *  PACK_C_SIZE;
                //! read 4OC 1IC filter
                GI_FLOAT16_t g00 = GiLoadFloat16(fptr + 0*  PACK_C_SIZE *  PACK_C_SIZE);
                GI_FLOAT16_t g01 = GiLoadFloat16(fptr + 1*  PACK_C_SIZE *  PACK_C_SIZE);
                GI_FLOAT16_t g02 = GiLoadFloat16(fptr + 2*  PACK_C_SIZE *  PACK_C_SIZE);
                GI_FLOAT16_t g10 = GiLoadFloat16(fptr + 3*  PACK_C_SIZE *  PACK_C_SIZE);
                GI_FLOAT16_t g11 = GiLoadFloat16(fptr + 4*  PACK_C_SIZE *  PACK_C_SIZE);
                GI_FLOAT16_t g12 = GiLoadFloat16(fptr + 5*  PACK_C_SIZE *  PACK_C_SIZE);
                GI_FLOAT16_t g20 = GiLoadFloat16(fptr + 6*  PACK_C_SIZE *  PACK_C_SIZE);
                GI_FLOAT16_t g21 = GiLoadFloat16(fptr + 7*  PACK_C_SIZE *  PACK_C_SIZE);
                GI_FLOAT16_t g22 = GiLoadFloat16(fptr + 8*  PACK_C_SIZE *  PACK_C_SIZE);

                //! twice matmul
                GI_FLOAT16_t tmp0, tmp1;
                ${FilterTransUnroll(3, midle, g, tmp0, tmp1)}
                ${FilterTransUnroll(6, ret, midle, tmp0, tmp1)}

                //! write to the dst
                gi_float16_t* dst = ${outptr};
                ${StoreRet2D(6, 6, ret)};
            }
        }
    })";
    auto FilterTransUnroll = [](const std::vector<std::string>& strs) {
        int times = std::stoi(strs[0]);
        std::string dst = strs[1];
        std::string src = strs[2];
        std::string tmp0 = strs[3];
        std::string tmp1 = strs[4];
        std::stringstream ss;
        for (int i = 0; i < times; i++) {
            ss << "GI_FLOAT16_t " << dst << i << "0 = GiMultiplyScalerFloat16(" << src
               << "0" << i << ", 0.25f);\n";
            ss << tmp0 << " = GiMultiplyScalerFloat16(GiAddFloat16(" << src << "0" << i
               << ", " << src << "2" << i << "), (-1.0/6));\n";
            ss << tmp1 << " = GiMultiplyScalerFloat16(" << src << "1" << i
               << ", (-1.0/6));\n";
            ss << "GI_FLOAT16_t " << dst << i << "1 = GiAddFloat16(" << tmp0 << ", "
               << tmp1 << ");\n";
            ss << "GI_FLOAT16_t " << dst << i << "2 = GiSubtractFloat16(" << tmp0
               << ", " << tmp1 << ");\n";
            ss << tmp0 << " = GiAddFloat16(GiMultiplyScalerFloat16(" << src << "0" << i
               << ", 1.0/24), GiMultiplyScalerFloat16(" << src << "2" << i
               << ", 1.0/6));\n";
            ss << tmp1 << " = GiMultiplyScalerFloat16(" << src << "1" << i
               << ", 1.0/12);\n";
            ss << "GI_FLOAT16_t " << dst << i << "3 = GiAddFloat16(" << tmp0 << ", "
               << tmp1 << ");\n";
            ss << "GI_FLOAT16_t " << dst << i << "4 = GiSubtractFloat16(" << tmp0
               << ", " << tmp1 << ");\n";
            ss << "GI_FLOAT16_t " << dst << i << "5 = " << src << "2" << i << ";\n";
        }
        return ss.str();
    };

    auto StoreRet2D = [](const std::vector<std::string>& strs) {
        int times_out = std::stoi(strs[0]);
        int times_inner = std::stoi(strs[1]);
        std::string src = strs[2];
        std::stringstream ss;
        for (int out = 0; out < times_out; out++) {
            for (int inner = 0; inner < times_inner; inner++) {
                ss << "GiStoreFloat16(dst + (" << out << " * alpha + " << inner
                   << ") * OCB * ICB * PACK_C_SIZE * PACK_C_SIZE + ocb * ICB * "
                      "PACK_C_SIZE *PACK_C_SIZE + icb* PACK_C_SIZE * "
                      "PACK_C_SIZE + "
                      "ic_inner*PACK_C_SIZE, "
                   << src << out << inner << ");\n";
            }
        }
        return ss.str();
    };
    std::stringstream ss;
    ss << StringTemplate::StringTemplateArgs()
                    .add("StoreRet2D", StoreRet2D)
                    .add("FilterTransUnroll", FilterTransUnroll)
                    .add("OC", OC)
                    .add("IC", IC)
                    .add("filter", inptr)
                    .add("outptr", outptr)
                    .render(filter_process);
    return ss.str();
}

std::string WinogradF43Strategy8x8MK8::InputFeatureTrans(
        const std::vector<std::string>& strs) {
    auto InputTransformF43NCHW88 = [](std::vector<std::string>) {
        std::stringstream ss;
        std::string kernel = R"(
        size_t ICB = IC_ /  PACK_C_SIZE;
        size_t icb = ic /  PACK_C_SIZE;

        #if defined(GI_TARGET_X86) || defined(GI_RVV_INTRINSICS)
            const gi_float16_t* v0 = input_parameters;
        #else
            GI_FLOAT16_t v0 = GiLoadFloat16(input_parameters);
        #endif
        int base_offset= ic * IH_ * IW_ + ih_start * IW_ * PACK_C_SIZE  + iw_start * PACK_C_SIZE;
        const gi_float16_t* input_ptr_ =source;

        GI_FLOAT16_t zero = GiZeroFloat16();
        GI_FLOAT16_t d00, d01, d02, d03, d04, d05;
        GI_FLOAT16_t d10, d11, d12, d13, d14, d15;
        GI_FLOAT16_t d20, d21, d22, d23, d24, d25;
        GI_FLOAT16_t d30, d31, d32, d33, d34, d35;

#define cb(i) GI_FLOAT16_t t##i;
        UNROLL_CALL_NOWRAPPER(6, cb);
#undef cb

        // load line 0 -> d00 ... d05
        int offset = base_offset;
        const gi_float16_t* line_ptr = input_ptr_+ offset;
        if (inner) {
#define cb(i) d0##i = GiLoadFloat16(line_ptr + i *  PACK_C_SIZE);
            UNROLL_CALL_NOWRAPPER(6, cb);
#undef cb
        } else {
            if (ih_valid[0] == 1) {
#define cb(i) d0##i = iw_valid[i] ==1 ? GiLoadFloat16(line_ptr + i *  PACK_C_SIZE) : zero;
                UNROLL_CALL_NOWRAPPER(6, cb);
#undef cb
            } else {
#define cb(i) d0##i = zero;
                UNROLL_CALL_NOWRAPPER(6, cb);
#undef cb
            }
        }

        // load line 4 -> d30 ... t35
        offset = base_offset + 4 * IW_ * PACK_C_SIZE;
        line_ptr = input_ptr_ + offset;
        if (inner) {
#define cb(i)                                        \
    d3##i = GiLoadFloat16(line_ptr + i *  PACK_C_SIZE); \
    t##i = MADD(d3##i, d0##i, v0, 0);
            UNROLL_CALL_NOWRAPPER(6, cb);
#undef cb
        } else {
            if (ih_valid[4] == 1 ) {
#define cb(i)                                                             \
    d3##i = iw_valid[i] == 1 ? GiLoadFloat16(line_ptr + i *  PACK_C_SIZE) : zero; \
    t##i = MADD(d3##i, d0##i, v0, 0);
                UNROLL_CALL_NOWRAPPER(6, cb);
#undef cb
            } else {
#define cb(i)     \
    d3##i = zero; \
    t##i = MADD(d3##i, d0##i, v0, 0);
                UNROLL_CALL_NOWRAPPER(6, cb);
#undef cb
            }
        }

        // load line 2 -> d20 ... d25
        offset = base_offset + 2 * IW_ * PACK_C_SIZE;
        line_ptr = input_ptr_ + offset;
        if (inner) {
#define cb(i)                                        \
    d2##i = GiLoadFloat16(line_ptr + i *  PACK_C_SIZE); \
    t##i = MSUB(t##i, d2##i, v0, 1);
            UNROLL_CALL_NOWRAPPER(6, cb);
#undef cb
        } else {
            if (ih_valid[2] == 1 ) {
#define cb(i)                                                             \
    d2##i = iw_valid[i] ==1 ? GiLoadFloat16(line_ptr + i *  PACK_C_SIZE) : zero; \
    t##i = MSUB(t##i, d2##i, v0, 1);
                UNROLL_CALL_NOWRAPPER(6, cb);
#undef cb
            } else {
#define cb(i)     \
    d2##i = zero; \
    t##i = MSUB(t##i, d2##i, v0, 1);
                UNROLL_CALL_NOWRAPPER(6, cb);
#undef cb
            }
        }

        // load line 3 -> d10 ... d15
        offset = base_offset + 3 * IW_ * PACK_C_SIZE;
        line_ptr = input_ptr_ + offset;
        if (inner) {
#define cb(i) d1##i = GiLoadFloat16(line_ptr + i *  PACK_C_SIZE);
            UNROLL_CALL_NOWRAPPER(6, cb);
#undef cb
        } else {
            if (ih_valid[3] ==1 ) {
#define cb(i) d1##i = iw_valid[i] ==1 ? GiLoadFloat16(line_ptr + i *  PACK_C_SIZE) : zero;
                UNROLL_CALL_NOWRAPPER(6, cb);
#undef cb
            } else {
#define cb(i) d1##i = zero;
                UNROLL_CALL_NOWRAPPER(6, cb);
#undef cb
            }
        }

        gi_float16_t* buf_ptr = dst + icb * nr_tiles_in_loop_ *  PACK_C_SIZE +
                         tile_idx *  PACK_C_SIZE;

        d00 = MADD(t4, t0, v0, 0);
        d00 = MSUB(d00, t2, v0, 1);
        GiStoreFloat16(buf_ptr, d00);
        d00 = MSUB(t3, t1, v0, 0);
        d01 = MSUB(t4, t2, v0, 0);
        d02 = GiAddFloat16(d00, d01);
        GiStoreFloat16(buf_ptr + 1 * ICB * nr_tiles_in_loop_ *  PACK_C_SIZE, d02);
        d02 = GiSubtractFloat16(d01, d00);
        GiStoreFloat16(buf_ptr + 2 * ICB * nr_tiles_in_loop_ *  PACK_C_SIZE, d02);
        d00 = GiSubtractFloat16(t3, t1);
        d01 = GiSubtractFloat16(t4, t2);
        d02 = MADD(d01, d00, v0, 2);
        GiStoreFloat16(buf_ptr + 3 * ICB * nr_tiles_in_loop_ *  PACK_C_SIZE, d02);
        d02 = MSUB(d01, d00, v0, 2);
        GiStoreFloat16(buf_ptr + 4 * ICB * nr_tiles_in_loop_ *  PACK_C_SIZE, d02);
        d01 = GiSubtractFloat16(t5, t3);
        d02 = MSUB(d01, d00, v0, 0);
        GiStoreFloat16(buf_ptr + 5 * ICB * nr_tiles_in_loop_ *  PACK_C_SIZE, d02);

// ln4 - ln2 -> t
#define cb(i) t##i = GiSubtractFloat16(d3##i, d2##i);
        UNROLL_CALL_NOWRAPPER(6, cb);
#undef cb

        // load line 1 -> d00 ... d05
        offset = base_offset + 1 * IW_ * PACK_C_SIZE;
        line_ptr = input_ptr_ + offset;
        if (inner) {
#define cb(i) d0##i = GiLoadFloat16(line_ptr + i *  PACK_C_SIZE);
            UNROLL_CALL_NOWRAPPER(6, cb);
#undef cb
        } else {
            if (ih_valid[1] ==1) {
#define cb(i) d0##i = iw_valid[i] ==1 ? GiLoadFloat16(line_ptr + i *  PACK_C_SIZE) : zero;
                UNROLL_CALL_NOWRAPPER(6, cb);
#undef cb
            } else {
#define cb(i) d0##i = zero;
                UNROLL_CALL_NOWRAPPER(6, cb);
#undef cb
            }
        }

// ln4 - 4 * ln2 -> ln4
#define cb(i) d3##i = MSUB(d3##i, d2##i, v0, 0);
        UNROLL_CALL_NOWRAPPER(6, cb);
#undef cb

// ln3 - 4 * ln1 -> ln2
#define cb(i) d2##i = MSUB(d1##i, d0##i, v0, 0);
        UNROLL_CALL_NOWRAPPER(6, cb);
#undef cb

// ln3 - ln1 -> ln3
#define cb(i) d1##i = GiSubtractFloat16(d1##i, d0##i);
        UNROLL_CALL_NOWRAPPER(6, cb);
#undef cb

// (ln4 - 4 * ln2)[ln4] + (ln3 - 4 * ln1)[ln2] -> ln1
#define cb(i) d0##i = GiAddFloat16(d3##i, d2##i);
        UNROLL_CALL_NOWRAPPER(6, cb);
#undef cb

// (ln4 - 4 * ln2)[ln4] - (ln3 - 4 * ln1)[ln2] -> ln2
#define cb(i) d2##i = GiSubtractFloat16(d3##i, d2##i);
        UNROLL_CALL_NOWRAPPER(6, cb);
#undef cb

        // ln4(d30 ... d35) is free until now
        buf_ptr = dst + 1 * Alpha * ICB * nr_tiles_in_loop_ *  PACK_C_SIZE +
                  icb * nr_tiles_in_loop_ *  PACK_C_SIZE + tile_idx *  PACK_C_SIZE;
        d30 = MADD(d04, d00, v0, 0);
        d30 = MSUB(d30, d02, v0, 1);
        GiStoreFloat16(buf_ptr, d30);
        d30 = MSUB(d03, d01, v0, 0);
        d32 = MSUB(d04, d02, v0, 0);
        d31 = GiAddFloat16(d30, d32);
        GiStoreFloat16(buf_ptr + ICB * nr_tiles_in_loop_ *  PACK_C_SIZE, d31);
        d31 = GiSubtractFloat16(d32, d30);
        GiStoreFloat16(buf_ptr + 2 * ICB * nr_tiles_in_loop_ *  PACK_C_SIZE, d31);
        d30 = GiSubtractFloat16(d03, d01);
        d31 = GiSubtractFloat16(d04, d02);
        d32 = MADD(d31, d30, v0, 2);
        GiStoreFloat16(buf_ptr + 3 * ICB * nr_tiles_in_loop_ *  PACK_C_SIZE, d32);
        d32 = MSUB(d31, d30, v0, 2);
        GiStoreFloat16(buf_ptr + 4 * ICB * nr_tiles_in_loop_ *  PACK_C_SIZE, d32);
        d31 = GiSubtractFloat16(d05, d03);
        d32 = MSUB(d31, d30, v0, 0);
        GiStoreFloat16(buf_ptr + 5 * ICB * nr_tiles_in_loop_ *  PACK_C_SIZE, d32);

        buf_ptr = dst + 2 * Alpha * ICB * nr_tiles_in_loop_ *  PACK_C_SIZE +
                  icb * nr_tiles_in_loop_ *  PACK_C_SIZE + tile_idx *  PACK_C_SIZE;
        d33 = MADD(d24, d20, v0, 0);
        d33 = MSUB(d33, d22, v0, 1);
        GiStoreFloat16(buf_ptr, d33);
        d33 = MSUB(d23, d21, v0, 0);
        d35 = MSUB(d24, d22, v0, 0);
        d34 = GiAddFloat16(d33, d35);
        GiStoreFloat16(buf_ptr + ICB * nr_tiles_in_loop_ *  PACK_C_SIZE, d34);
        d34 = GiSubtractFloat16(d35, d33);
        GiStoreFloat16(buf_ptr + 2 * ICB * nr_tiles_in_loop_ *  PACK_C_SIZE, d34);
        d33 = GiSubtractFloat16(d23, d21);
        d34 = GiSubtractFloat16(d24, d22);
        d35 = MADD(d34, d33, v0, 2);
        GiStoreFloat16(buf_ptr + 3 * ICB * nr_tiles_in_loop_ *  PACK_C_SIZE, d35);
        d35 = MSUB(d34, d33, v0, 2);
        GiStoreFloat16(buf_ptr + 4 * ICB * nr_tiles_in_loop_ *  PACK_C_SIZE, d35);
        d34 = GiSubtractFloat16(d25, d23);
        d35 = MSUB(d34, d33, v0, 0);
        GiStoreFloat16(buf_ptr + 5 * ICB * nr_tiles_in_loop_ *  PACK_C_SIZE, d35);

// (ln4 - ln2)[t] + (ln3 - ln1)[ln3] * 2 -> ln4
#define cb(i) d3##i = MADD(t##i, d1##i, v0, 2);
        UNROLL_CALL_NOWRAPPER(6, cb);
#undef cb

// (ln4 - ln2)[t] - (ln3 - ln1)[ln3] * 2 -> ln3
#define cb(i) d1##i = MSUB(t##i, d1##i, v0, 2);
        UNROLL_CALL_NOWRAPPER(6, cb);
#undef cb
        // t is free
        buf_ptr = dst + 3 * Alpha * ICB * nr_tiles_in_loop_ *  PACK_C_SIZE +
                  icb * nr_tiles_in_loop_ *  PACK_C_SIZE + tile_idx *  PACK_C_SIZE;
        t0 = MADD(d34, d30, v0, 0);
        t0 = MSUB(t0, d32, v0, 1);
        GiStoreFloat16(buf_ptr, t0);
        t0 = MSUB(d33, d31, v0, 0);
        t2 = MSUB(d34, d32, v0, 0);
        t1 = GiAddFloat16(t0, t2);
        GiStoreFloat16(buf_ptr + ICB * nr_tiles_in_loop_ *  PACK_C_SIZE, t1);
        t1 = GiSubtractFloat16(t2, t0);
        GiStoreFloat16(buf_ptr + 2 * ICB * nr_tiles_in_loop_ *  PACK_C_SIZE, t1);
        t0 = GiSubtractFloat16(d33, d31);
        t1 = GiSubtractFloat16(d34, d32);
        t2 = MADD(t1, t0, v0, 2);
        GiStoreFloat16(buf_ptr + 3 * ICB * nr_tiles_in_loop_ *  PACK_C_SIZE, t2);
        t2 = MSUB(t1, t0, v0, 2);
        GiStoreFloat16(buf_ptr + 4 * ICB * nr_tiles_in_loop_ *  PACK_C_SIZE, t2);
        t1 = GiSubtractFloat16(d35, d33);
        t2 = MSUB(t1, t0, v0, 0);
        GiStoreFloat16(buf_ptr + 5 * ICB * nr_tiles_in_loop_ *  PACK_C_SIZE, t2);

        buf_ptr = dst + 4 * Alpha * ICB * nr_tiles_in_loop_ *  PACK_C_SIZE +
                  icb * nr_tiles_in_loop_ *  PACK_C_SIZE + tile_idx *  PACK_C_SIZE;
        t3 = MADD(d14, d10, v0, 0);
        t3 = MSUB(t3, d12, v0, 1);
        GiStoreFloat16(buf_ptr, t3);
        t3 = MSUB(d13, d11, v0, 0);
        t5 = MSUB(d14, d12, v0, 0);
        t4 = GiAddFloat16(t3, t5);
        GiStoreFloat16(buf_ptr + ICB * nr_tiles_in_loop_ *  PACK_C_SIZE, t4);
        t4 = GiSubtractFloat16(t5, t3);
        GiStoreFloat16(buf_ptr + 2 * ICB * nr_tiles_in_loop_ *  PACK_C_SIZE, t4);
        t3 = GiSubtractFloat16(d13, d11);
        t4 = GiSubtractFloat16(d14, d12);
        t5 = MADD(t4, t3, v0, 2);
        GiStoreFloat16(buf_ptr + 3 * ICB * nr_tiles_in_loop_ *  PACK_C_SIZE, t5);
        t5 = MSUB(t4, t3, v0, 2);
        GiStoreFloat16(buf_ptr + 4 * ICB * nr_tiles_in_loop_ *  PACK_C_SIZE, t5);
        t4 = GiSubtractFloat16(d15, d13);
        t5 = MSUB(t4, t3, v0, 0);
        GiStoreFloat16(buf_ptr + 5 * ICB * nr_tiles_in_loop_ *  PACK_C_SIZE, t5);

        // load line 5 -> d30 ... d35
        offset = base_offset + 5 * IW_ * PACK_C_SIZE;
        line_ptr = input_ptr_ + offset;
        if (inner) {
#define cb(i) d3##i = GiLoadFloat16(line_ptr + i *  PACK_C_SIZE);
            UNROLL_CALL_NOWRAPPER(6, cb);
#undef cb
        } else {
            if (ih_valid[5] == 1) {
#define cb(i) d3##i = iw_valid[i] ==1 ? GiLoadFloat16(line_ptr + i *  PACK_C_SIZE) : zero;
                UNROLL_CALL_NOWRAPPER(6, cb);
#undef cb
            } else {
#define cb(i) d3##i = zero;
                UNROLL_CALL_NOWRAPPER(6, cb);
#undef cb
            }
        }

        // load line 1 -> d0 ... d5
        offset = base_offset + 1 * IW_ * PACK_C_SIZE;
        line_ptr = input_ptr_ + offset;
        if (inner) {
#define cb(i)                                        \
    d0##i = GiLoadFloat16(line_ptr + i *  PACK_C_SIZE); \
    d3##i = MADD(d3##i, d0##i, v0, 0);
            UNROLL_CALL_NOWRAPPER(6, cb);
#undef cb
        } else {
            if (ih_valid[1] ==1) {
#define cb(i)                                                             \
    d0##i = iw_valid[i] ==1 ? GiLoadFloat16(line_ptr + i *  PACK_C_SIZE) : zero; \
    d3##i = MADD(d3##i, d0##i, v0, 0);
                UNROLL_CALL_NOWRAPPER(6, cb);
#undef cb
            } else {
#define cb(i)     \
    d0##i = zero; \
    d3##i = MADD(d3##i, d0##i, v0, 0);
                UNROLL_CALL_NOWRAPPER(6, cb);
#undef cb
            }
        }

        // load line 3 -> d10 ... d15
        offset = base_offset + 3 * IW_ * PACK_C_SIZE;
        line_ptr = input_ptr_ + offset;
        if (inner) {
#define cb(i)                                        \
    d1##i = GiLoadFloat16(line_ptr + i *  PACK_C_SIZE); \
    d3##i = MSUB(d3##i, d1##i, v0, 1);
            UNROLL_CALL_NOWRAPPER(6, cb);
#undef cb
        } else {
            if (ih_valid[3] == 1 ) {
#define cb(i)                                                             \
    d1##i = iw_valid[i] ==1 ? GiLoadFloat16(line_ptr + i *  PACK_C_SIZE) : zero; \
    d3##i = MSUB(d3##i, d1##i, v0, 1);
                UNROLL_CALL_NOWRAPPER(6, cb);
#undef cb
            } else {
#define cb(i)     \
    d1##i = zero; \
    d3##i = MSUB(d3##i, d1##i, v0, 1);
                UNROLL_CALL_NOWRAPPER(6, cb);
#undef cb
            }
        }

        buf_ptr = dst + 5 * Alpha * ICB * nr_tiles_in_loop_ *  PACK_C_SIZE +
                  icb * nr_tiles_in_loop_ *  PACK_C_SIZE + tile_idx *  PACK_C_SIZE;
        t0 = MADD(d34, d30, v0, 0);
        t0 = MSUB(t0, d32, v0, 1);
        GiStoreFloat16(buf_ptr, t0);
        t0 = MSUB(d33, d31, v0, 0);
        t2 = MSUB(d34, d32, v0, 0);
        t1 = GiAddFloat16(t0, t2);
        GiStoreFloat16(buf_ptr + ICB * nr_tiles_in_loop_ *  PACK_C_SIZE, t1);
        t1 = GiSubtractFloat16(t2, t0);
        GiStoreFloat16(buf_ptr + 2 * ICB * nr_tiles_in_loop_ *  PACK_C_SIZE, t1);
        t0 = GiSubtractFloat16(d33, d31);
        t1 = GiSubtractFloat16(d34, d32);
        t2 = MADD(t1, t0, v0, 2);
        GiStoreFloat16(buf_ptr + 3 * ICB * nr_tiles_in_loop_ *  PACK_C_SIZE, t2);
        t2 = MSUB(t1, t0, v0, 2);
        GiStoreFloat16(buf_ptr + 4 * ICB * nr_tiles_in_loop_ *  PACK_C_SIZE, t2);
        t1 = GiSubtractFloat16(d35, d33);
        t2 = MSUB(t1, t0, v0, 0);
        GiStoreFloat16(buf_ptr + 5 * ICB * nr_tiles_in_loop_ *  PACK_C_SIZE, t2);

)";
        return kernel;
    };
    std::string input_process = R"(
    const uint32_t OUTPUT_BLOCK_SIZE = 4;
    const uint32_t KS = 3;

    gi_float16_t* dst = ${transform_input_ptr};
    const gi_float16_t* source = ${inptr};
    uint32_t IH_ = ${IH};
    uint32_t IW_ = ${IW};
    uint32_t IC_ = ${IC};
    uint32_t PH_ = ${PH};
    uint32_t PW_ = ${PW};
    uint32_t nr_tiles_in_loop_ = ${nr_tiles_in_loop};
    uint32_t tile_id_ = ${tile_id};

    const gi_float16_t input_parameters[8] = {4.0, 5.0, 2.0, 0.0, 0.0, 0.0, 0.0, 0.0};

     #if defined(GI_TARGET_X86) || defined(GI_RVV_INTRINSICS)
    //! x86 and rvv GiSimdFmaLaneFloat16 API is slowly, as an alternate, use
    //! GiMultiplyAddScalarFloat16
    #define MADD(a, b, c, d) GiMultiplyAddScalarFloat16(a, b, *(c + d))
    #define MSUB(a, b, c, d) GiMultiplySubScalarFloat16(a, b, *(c + d))
    #else
    #define MADD(a, b, c, d) GiSimdFmaLaneFloat16(a, b, c, d)
    #define MSUB(a, b, c, d) GiFmsqLaneQFloat16(a, b, c, d)
    #endif

    uint32_t OW = IW_ + 2 * PW_ - KS + 1;
    uint32_t tiles_w = (OW + OUTPUT_BLOCK_SIZE -1)/ OUTPUT_BLOCK_SIZE;
    int ih_valid[6]={0,0,0,0,0,0};
    int iw_valid[6]={0,0,0,0,0,0};

    for (uint32_t ic = 0; ic < IC_; ic += PACK_C_SIZE) {
        uint32_t tile_start_id = tile_id_;
        for(uint32_t tile_idx = 0; tile_idx < nr_tiles_in_loop_; tile_idx++) {
            uint32_t index = tile_start_id + tile_idx;
            uint32_t nh = index / tiles_w;
            uint32_t nw = index % tiles_w;

            int ih_start = nh * OUTPUT_BLOCK_SIZE - PH_;
            int iw_start = nw * OUTPUT_BLOCK_SIZE - PW_;
            int inner = (ih_start >= 0 && iw_start >= 0 &&
                        ih_start + Alpha <= (int)IH_ &&
                        iw_start + Alpha <= (int)IW_)?1:0;
            if(!inner){
                 for (int iho = 0; iho < Alpha; ++iho) {
                    ih_valid[iho] =
                            (iho + ih_start >= 0 &&
                             iho + ih_start < (int)IH_) ? 1:0;
                }
                for (int iwo = 0; iwo < Alpha; ++iwo) {
                    iw_valid[iwo] =
                            (iwo + iw_start >= 0 &&
                             iwo + iw_start < (int)(IW_))?1:0;
                }
            }
            ${InputTransformF43NCHW88()}
        }
    })";
    std::stringstream ss;
    ss << StringTemplate::StringTemplateArgs()
                    .add("inptr", strs[0])
                    .add("transform_input_ptr", strs[1])
                    .add("IH", strs[2])
                    .add("IW", strs[3])
                    .add("IC", strs[4])
                    .add("PH", strs[5])
                    .add("PW", strs[6])
                    .add("tile_id", strs[7])
                    .add("nr_tiles_in_loop", strs[8])
                    .add("InputTransformF43NCHW88", InputTransformF43NCHW88)
                    .render(input_process);
    return ss.str();
}

std::string WinogradF43Strategy8x8MK8::DependMatmulSymbol() {
    return Fp16MatmulM8N8MK8Kernel().GetKernelSymbol(NULL);
}

std::string WinogradF43Strategy8x8MK8::OutputFeatureTrans(
        const std::vector<std::string>& strs, TContext* ctx) {
    std::string ouput_trans = R"(
    gi_float16_t* transform_output_ptr_ = ${transform_output_ptr};
    const gi_float16_t output_parameters[8] = {1.0, 2.0, 4.0, 8.0, 0.0, 0.0, 0.0, 0.0};
    gi_float16_t* outptr_ = ${outptr};
    const gi_float16_t* bias = ${bias_ptr};
    
    uint32_t OH_ = ${OH};
    uint32_t OW_ = ${OW};
    uint32_t OC_ = ${OC};

    uint32_t tile_id_ = ${tile_id};
    uint32_t nr_tiles_in_loop_ = ${nr_tiles_in_loop};
     #if defined(GI_TARGET_X86) || defined(GI_RVV_INTRINSICS)
            const gi_float16_t* v0 = output_parameters;
        #else
            GI_FLOAT16_t v0 = GiLoadFloat16(output_parameters);
        #endif
    uint32_t tiles_w_ = (OW_ + OutputBlockSize -1) / OutputBlockSize;
    for (uint32_t oc = 0; oc < OC_; oc += PACK_C_SIZE) {
        for(uint32_t tile_idx = 0; tile_idx < nr_tiles_in_loop_; tile_idx++) {
            uint32_t index = tile_id_ + tile_idx;
            uint32_t nh = index / tiles_w_;
            uint32_t nw = index % tiles_w_;
            uint32_t oh_start = nh * OutputBlockSize;
            uint32_t ow_start = nw * OutputBlockSize;

            size_t num_valid_oh =(OH_ - oh_start) < 4 ?(OH_ - oh_start) : 4;
            size_t num_valid_ow = (OW_ - ow_start) < 4 ?(OW_ - ow_start) : 4;

            //! AT * m * A
            size_t OCB = (OC_) /  PACK_C_SIZE;
            size_t ocb = oc /  PACK_C_SIZE;
            size_t col_step = OCB * nr_tiles_in_loop_ * PACK_C_SIZE;
            size_t row_step = Alpha * col_step;

            GI_FLOAT16_t vbias = GiZeroFloat16();
            GI_FLOAT16_t v00, v01, v02, v03, v04, v05;
            GI_FLOAT16_t v10, v11, v12, v13, v14, v15;
            GI_FLOAT16_t v20, v21, v22, v23, v24, v25;
            GI_FLOAT16_t v30, v31, v32, v33, v34, v35;
            GI_FLOAT16_t v40, v41, v42, v43, v44, v45;

            if(num_valid_ow == num_valid_oh && num_valid_ow ==4){
                const gi_float16_t* buf_base =
                        transform_output_ptr_ + ocb * nr_tiles_in_loop_ * PACK_C_SIZE +
                        tile_idx * PACK_C_SIZE;
                const gi_float16_t* buf_ptr = NULL;

                // load line 1 -> v10 ... v15
                buf_ptr = buf_base + row_step;
        #define cb(i) v1##i = GiLoadFloat16(buf_ptr + i * col_step);
                UNROLL_CALL_NOWRAPPER(6, cb);
        #undef cb

                // load line 2 -> v20 ... v25
                buf_ptr = buf_base + 2 * row_step;
        #define cb(i)                                      \
            v2##i = GiLoadFloat16(buf_ptr + i * col_step); \
            v0##i = GiAddFloat16(v1##i, v2##i);                    \
            v1##i = GiSubtractFloat16(v1##i, v2##i);
                UNROLL_CALL_NOWRAPPER(6, cb);
        #undef cb

                // load line 3 -> v30 ... v35
                buf_ptr = buf_base + 3 * row_step;
        #define cb(i) v3##i = GiLoadFloat16(buf_ptr + i * col_step);
                UNROLL_CALL_NOWRAPPER(6, cb);
        #undef cb

                // load line 4 -> v40 ... v45
                buf_ptr = buf_base + 4 * row_step;
        #define cb(i)                                      \
            v4##i = GiLoadFloat16(buf_ptr + i * col_step); \
            v2##i = GiAddFloat16(v3##i, v4##i);                    \
            v3##i = GiSubtractFloat16(v3##i, v4##i);                    \
            v4##i = MADD(v0##i, v2##i, v0, 2);             \
            v2##i = GiAddFloat16(v2##i, v0##i);
                UNROLL_CALL_NOWRAPPER(6, cb);
        #undef cb
                ${nonline_gen_init()}
                gi_float16_t* output_base = outptr_ + oc * OH_ * OW_ + oh_start * OW_ * PACK_C_SIZE +
                                    ow_start * PACK_C_SIZE;
                gi_float16_t* output_ptr_ = output_base + 2 * OW_ * PACK_C_SIZE;
                if (bias) {
                   vbias = GiLoadFloat16(bias + oc);
                }
                v00 = GiAddFloat16(v41, v42);
                v01 = GiAddFloat16(v43, v44);
                v02 = GiAddFloat16(v40, v00);
                v02 = GiAddFloat16(v02, v01);

                v02 = GiAddFloat16(v02, vbias);
                ${nonline_gen_func(v02, v02)};
                GiStoreFloat16(output_ptr_, v02);

                v03 = GiSubtractFloat16(v41, v42);
                v04 = GiSubtractFloat16(v43, v44);
                v05 = MADD(v03, v04, v0, 1);

                v05 = GiAddFloat16(v05, vbias);
                ${nonline_gen_func(v05, v05)};
                GiStoreFloat16(output_ptr_ + PACK_C_SIZE, v05);

                v02 = MADD(v00, v01, v0, 2);

                v02 = GiAddFloat16(v02, vbias);
                ${nonline_gen_func(v02, v02)};
                GiStoreFloat16(output_ptr_ + 2 * PACK_C_SIZE, v02);

                v05 = MADD(v03, v04, v0, 3);
                v05 = GiAddFloat16(v05, v45);

                v05 = GiAddFloat16(v05, vbias);
                ${nonline_gen_func(v05, v05)};
                GiStoreFloat16(output_ptr_ + 3 * PACK_C_SIZE, v05);

                buf_ptr = buf_base;
        #define cb(i)                                      \
            v4##i = GiLoadFloat16(buf_ptr + i * col_step); \
            v4##i = GiAddFloat16(v4##i, v2##i);
                UNROLL_CALL_NOWRAPPER(6, cb);
        #undef cb

                output_ptr_ = output_base;

                v00 = GiAddFloat16(v41, v42);
                v01 = GiAddFloat16(v43, v44);
                v02 = GiAddFloat16(v40, v00);
                v02 = GiAddFloat16(v02, v01);

                v02 = GiAddFloat16(v02, vbias);
                ${nonline_gen_func(v02, v02)};
                GiStoreFloat16(output_ptr_, v02);

                v03 = GiSubtractFloat16(v41, v42);
                v04 = GiSubtractFloat16(v43, v44);
                v05 = MADD(v03, v04, v0, 1);

                v05 = GiAddFloat16(v05, vbias);
                ${nonline_gen_func(v05, v05)};
                GiStoreFloat16(output_ptr_ + PACK_C_SIZE, v05);

                v02 = MADD(v00, v01, v0, 2);

                v02 = GiAddFloat16(v02, vbias);
                ${nonline_gen_func(v02,v02)};
                GiStoreFloat16(output_ptr_ + 2 * PACK_C_SIZE, v02);

                v05 = MADD(v03, v04, v0, 3);
                v05 = GiAddFloat16(v05, v45);

                v05 = GiAddFloat16(v05, vbias);
                ${nonline_gen_func(v05,v05)};
                GiStoreFloat16(output_ptr_ + 3 * PACK_C_SIZE, v05);

        #define cb(i) v4##i = MADD(v1##i, v3##i, v0, 1);
                UNROLL_CALL_NOWRAPPER(6, cb);
        #undef cb

                output_ptr_ = output_base + OW_ * PACK_C_SIZE;

                v00 = GiAddFloat16(v41, v42);
                v01 = GiAddFloat16(v43, v44);
                v02 = GiAddFloat16(v40, v00);
                v02 = GiAddFloat16(v02, v01);

                v02 = GiAddFloat16(v02, vbias);
                ${nonline_gen_func(v02, v02)};
                GiStoreFloat16(output_ptr_, v02);

                v03 = GiSubtractFloat16(v41, v42);
                v04 = GiSubtractFloat16(v43, v44);
                v05 = MADD(v03, v04, v0, 1);

                v05 = GiAddFloat16(v05, vbias);
                ${nonline_gen_func(v05, v05)};
                GiStoreFloat16(output_ptr_ + PACK_C_SIZE, v05);

                v02 = MADD(v00, v01, v0, 2);

                v02 = GiAddFloat16(v02, vbias);
                ${nonline_gen_func(v02, v02)};
                GiStoreFloat16(output_ptr_ + 2 * PACK_C_SIZE, v02);

                v05 = MADD(v03, v04, v0, 3);
                v05 = GiAddFloat16(v05, v45);

                v05 = GiAddFloat16(v05, vbias);
                ${nonline_gen_func(v05, v05)};
                GiStoreFloat16(output_ptr_ + 3 * PACK_C_SIZE, v05);

                buf_ptr = buf_base + 5 * row_step;
        #define cb(i)                                      \
            v2##i = GiLoadFloat16(buf_ptr + i * col_step); \
            v1##i = MADD(v1##i, v3##i, v0, 3);             \
            v2##i = GiAddFloat16(v1##i, v2##i);
                UNROLL_CALL_NOWRAPPER(6, cb);
        #undef cb

                output_ptr_ = output_base + 3 * OW_ * PACK_C_SIZE;

                v00 = GiAddFloat16(v21, v22);
                v01 = GiAddFloat16(v23, v24);
                v02 = GiAddFloat16(v20, v00);
                v02 = GiAddFloat16(v02, v01);

                v02 = GiAddFloat16(v02, vbias);
                ${nonline_gen_func(v02, v02)};
                GiStoreFloat16(output_ptr_, v02);

                v03 = GiSubtractFloat16(v21, v22);
                v04 = GiSubtractFloat16(v23, v24);
                v05 = MADD(v03, v04, v0, 1);

                v05 = GiAddFloat16(v05, vbias);
                ${nonline_gen_func(v05, v05)};
                GiStoreFloat16(output_ptr_ + PACK_C_SIZE, v05);

                v02 = MADD(v00, v01, v0, 2);

                v02 = GiAddFloat16(v02, vbias);
                ${nonline_gen_func(v02, v02)};
                GiStoreFloat16(output_ptr_ + 2 * PACK_C_SIZE, v02);

                v05 = MADD(v03, v04, v0, 3);
                v05 = GiAddFloat16(v05, v25);

                v05 = GiAddFloat16(v05, vbias);
                ${nonline_gen_func(v05, v05)};
                GiStoreFloat16(output_ptr_ + 3 * PACK_C_SIZE, v05);
        }else{

        const gi_float16_t* buf_base =
                transform_output_ptr_ + ocb * nr_tiles_in_loop_ * PACK_C_SIZE +
                tile_idx * PACK_C_SIZE;
        const gi_float16_t* buf_ptr = NULL;

        // load line 1 -> v10 ... v15
        buf_ptr = buf_base + row_step;
#define cb(i) v1##i = GiLoadFloat16(buf_ptr + i * col_step);
        UNROLL_CALL_NOWRAPPER(6, cb);
#undef cb

        // load line 2 -> v20 ... v25
        buf_ptr = buf_base + 2 * row_step;
#define cb(i)                                      \
    v2##i = GiLoadFloat16(buf_ptr + i * col_step); \
    v0##i = GiAddFloat16(v1##i, v2##i);                    \
    v1##i = GiSubtractFloat16(v1##i, v2##i);
        UNROLL_CALL_NOWRAPPER(6, cb);
#undef cb

        // load line 3 -> v30 ... v35
        buf_ptr = buf_base + 3 * row_step;
#define cb(i) v3##i = GiLoadFloat16(buf_ptr + i * col_step);
        UNROLL_CALL_NOWRAPPER(6, cb);
#undef cb

        // load line 4 -> v40 ... v45
        buf_ptr = buf_base + 4 * row_step;
#define cb(i)                                      \
    v4##i = GiLoadFloat16(buf_ptr + i * col_step); \
    v2##i = GiAddFloat16(v3##i, v4##i);                    \
    v3##i = GiSubtractFloat16(v3##i, v4##i);                    \
    v4##i = MADD(v0##i, v2##i, v0, 2);             \
    v2##i = GiAddFloat16(v2##i, v0##i);
        UNROLL_CALL_NOWRAPPER(6, cb);
#undef cb

        // result line 2, v40 ... v45 -> v02 ... v05
        // v40 ... v45 is free.
        v00 = GiAddFloat16(v41, v42);
        v01 = GiAddFloat16(v43, v44);
        v02 = GiAddFloat16(v40, v00);
        v02 = GiAddFloat16(v02, v01);

        v04 = MADD(v00, v01, v0, 2);

        v00 = GiSubtractFloat16(v41, v42);
        v01 = GiSubtractFloat16(v43, v44);
        v03 = MADD(v00, v01, v0, 1);

        v05 = MADD(v00, v01, v0, 3);
        v05 = GiAddFloat16(v05, v45);

        buf_ptr = buf_base;
#define cb(i)                                      \
    v4##i = GiLoadFloat16(buf_ptr + i * col_step); \
    v4##i = GiAddFloat16(v4##i, v2##i);
        UNROLL_CALL_NOWRAPPER(6, cb);
#undef cb

        // result line 0
        // v40 ... v45 -> v22 ... v25
        v20 = GiAddFloat16(v41, v42);
        v21 = GiAddFloat16(v43, v44);
        v22 = GiAddFloat16(v40, v20);
        v22 = GiAddFloat16(v22, v21);

        v24 = MADD(v20, v21, v0, 2);

        v20 = GiSubtractFloat16(v41, v42);
        v21 = GiSubtractFloat16(v43, v44);
        v23 = MADD(v20, v21, v0, 1);

        v25 = MADD(v20, v21, v0, 3);
        v25 = GiAddFloat16(v25, v45);

#define cb(i)                          \
    v4##i = MADD(v1##i, v3##i, v0, 1); \
    v3##i = MADD(v1##i, v3##i, v0, 3);
        UNROLL_CALL_NOWRAPPER(6, cb);
#undef cb

        // result line 1
        // v40 ... v45 -> v12 ... v15
        v10 = GiAddFloat16(v41, v42);
        v11 = GiAddFloat16(v43, v44);
        v12 = GiAddFloat16(v40, v10);
        v12 = GiAddFloat16(v12, v11);

        v14 = MADD(v10, v11, v0, 2);

        v10 = GiSubtractFloat16(v41, v42);
        v11 = GiSubtractFloat16(v43, v44);
        v13 = MADD(v10, v11, v0, 1);

        v15 = MADD(v10, v11, v0, 3);
        v15 = GiAddFloat16(v15, v45);

        buf_ptr = buf_base + 5 * row_step;
#define cb(i)                                      \
    v4##i = GiLoadFloat16(buf_ptr + i * col_step); \
    v4##i = GiAddFloat16(v3##i, v4##i);
        UNROLL_CALL_NOWRAPPER(6, cb);
#undef cb

        // result line 3
        // v40 ... v45 -> v32 ... v35
        v30 = GiAddFloat16(v41, v42);
        v31 = GiAddFloat16(v43, v44);
        v32 = GiAddFloat16(v40, v30);
        v32 = GiAddFloat16(v32, v31);

        v34 = MADD(v30, v31, v0, 2);

        v30 = GiSubtractFloat16(v41, v42);
        v31 = GiSubtractFloat16(v43, v44);
        v33 = MADD(v30, v31, v0, 1);

        v35 = MADD(v30, v31, v0, 3);
        v35 = GiAddFloat16(v35, v45);

        gi_float16_t* output_base = outptr_ + oc * OH_ * OW_ + oh_start * OW_ *  PACK_C_SIZE +
                             ow_start *  PACK_C_SIZE;
        gi_float16_t* output_ptr_ = NULL;

        ${nonline_gen_init()}
        if (bias) {
            vbias = GiLoadFloat16(bias + oc);
        }
# define BIAS_LINE(j, k) \
    v##j##k = GiAddFloat16(v##j##k, vbias);

#define BIAS(m) \
    BIAS_LINE(m, 5) \
    BIAS_LINE(m, 4) \
    BIAS_LINE(m, 3) \
    BIAS_LINE(m, 2)

// add_bias
if(bias){
    BIAS(0)
    BIAS(1)
    BIAS(2)
    BIAS(3)
}
#undef BIAS_LINE
#undef BIAS

// activate

${nonline_gen_func(v35, vbias)};v35=vbias;
${nonline_gen_func(v34, vbias)};v34=vbias;
${nonline_gen_func(v33, vbias)};v33=vbias;
${nonline_gen_func(v32, vbias)};v32=vbias;

${nonline_gen_func(v25, vbias)};v25=vbias;
${nonline_gen_func(v24, vbias)};v24=vbias;
${nonline_gen_func(v23, vbias)};v23=vbias;
${nonline_gen_func(v22, vbias)};v22=vbias;

${nonline_gen_func(v15, vbias)};v15=vbias;
${nonline_gen_func(v14, vbias)};v14=vbias;
${nonline_gen_func(v13, vbias)};v13=vbias;
${nonline_gen_func(v12, vbias)};v12=vbias;

${nonline_gen_func(v05, vbias)};v05=vbias;
${nonline_gen_func(v04, vbias)};v04=vbias;
${nonline_gen_func(v03, vbias)};v03=vbias;
${nonline_gen_func(v02, vbias)};v02=vbias;

// store
# define STORE_LINE(i, j, k) \
if(num_valid_ow >i){ \
    GiStoreFloat16(output_ptr_ + i *  PACK_C_SIZE, v##j##k); \
}
#define STORE(m, l) \
if(num_valid_oh >m){ \
    output_ptr_ = output_base + m * OW_ *  PACK_C_SIZE; \
    STORE_LINE(3, l, 5) \
    STORE_LINE(2, l, 4) \
    STORE_LINE(1, l, 3) \
    STORE_LINE(0, l, 2) \
}
    STORE(3, 3)
    STORE(2, 0)
    STORE(1, 1)
    STORE(0, 2)
    }

#undef MSUB
#undef MADD
        }
    })";
    std::string nonline_mode =
            ctx->haveAttr("nonlineMode") ? ctx->getAttrStr("nonlineMode") : "IDENTITY";
    auto nonline_gen = create_activation_gener_instrinsic(nonline_mode, "f16");
    auto nonline_gen_func = [&](std::vector<std::string> str) -> std::string {
        return nonline_gen->GenIntrinsicFloat(str[0], str[1]);
    };
    auto nonline_gen_init = [&]() -> std::string {
        return nonline_gen->GenIntrinsicInitFloat();
    };

    std::stringstream ss;
    ss << StringTemplate::StringTemplateArgs()
                    .add("nonline_gen_func", nonline_gen_func)
                    .add("nonline_gen_init", nonline_gen_init)
                    .add("transform_output_ptr", strs[0])
                    .add("outptr", strs[1])
                    .add("bias_ptr", strs[2])
                    .add("OH", strs[3])
                    .add("OW", strs[4])
                    .add("OC", strs[5])
                    .add("tile_id", strs[6])
                    .add("nr_tiles_in_loop", strs[7])
                    .render(ouput_trans);
    return ss.str();
}

// vim: syntax=cpp.doxygen
