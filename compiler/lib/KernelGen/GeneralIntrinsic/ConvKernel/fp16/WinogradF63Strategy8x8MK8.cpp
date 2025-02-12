/**
 * \file
 * compiler/lib/KernelGen/GeneralIntrinsic/ConvKernel/Winograd/WinogradF63Strategy8x8MK8.cpp
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

std::string WinogradF63Strategy8x8MK8::WeightTrans(
        const std::vector<std::string>& strs) {
    auto inptr = strs[0];
    auto outptr = strs[1];
    auto OC = strs[2];
    auto IC = strs[3];
    std::string filter_process = R"(
    const uint32_t  PACK_C_SIZE= 8;
    const uint32_t KERNEL_SIZE = 3;
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
                ${FilterTransUnroll(8, ret, midle, tmp0, tmp1)}

                //! write to the dst
                gi_float16_t* dst = ${outptr};
                ${StoreRet2D(8, 8, ret)};
            }
        }
       
    }
    )";
    auto FilterTransUnroll = [](const std::vector<std::string>& strs) {
        int times = std::stoi(strs[0]);
        std::string dst = strs[1];
        std::string src = strs[2];
        std::string tmp0 = strs[3];
        std::string tmp1 = strs[4];
        std::stringstream ss;
        for (int i = 0; i < times; i++) {
            ss << "GI_FLOAT16_t " << dst << i << "0 = " << src << "0" << i << ";\n";
            ss << tmp0 << " = GiMultiplyScalerFloat16(GiAddFloat16(" << src << "0" << i
               << ", " << src << "2" << i << "), (-2.0f/9));\n";
            ss << tmp1 << " = GiMultiplyScalerFloat16(" << src << "1" << i
               << ", (-2.0f/9));\n";
            ss << "GI_FLOAT16_t " << dst << i << "1 = GiAddFloat16(" << tmp0 << ", "
               << tmp1 << ");\n";
            ss << "GI_FLOAT16_t " << dst << i << "2 = GiSubtractFloat16(" << tmp0
               << ", " << tmp1 << ");\n";
            ss << tmp0 << " = GiAddFloat16(GiMultiplyScalerFloat16(" << src << "0" << i
               << ", 1.0f/90), GiMultiplyScalerFloat16(" << src << "2" << i
               << ", 2.0f/45));\n";
            ss << tmp1 << " = GiMultiplyScalerFloat16(" << src << "1" << i
               << ", 2.0f/90);\n";
            ss << "GI_FLOAT16_t " << dst << i << "3 = GiAddFloat16(" << tmp0 << ", "
               << tmp1 << ");\n";
            ss << "GI_FLOAT16_t " << dst << i << "4 = GiSubtractFloat16(" << tmp0
               << ", " << tmp1 << ");\n";
            ss << tmp0 << " = GiAddFloat16(GiMultiplyScalerFloat16(" << src << "0" << i
               << ", 0.7111111f), GiMultiplyScalerFloat16(" << src << "2" << i
               << ", 0.1777778f));\n";
            ss << tmp1 << " = GiMultiplyScalerFloat16(" << src << "1" << i
               << ", 0.3555556f);\n";
            ss << "GI_FLOAT16_t " << dst << i << "5 = GiAddFloat16(" << tmp0 << ", "
               << tmp1 << ");\n";
            ss << "GI_FLOAT16_t " << dst << i << "6 = GiSubtractFloat16(" << tmp0
               << ", " << tmp1 << ");\n";
            ss << "GI_FLOAT16_t " << dst << i << "7 = " << src << "2" << i << ";\n";
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
                ss << "GiStoreFloat16(dst + (" << out << " * Alpha + " << inner
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

std::string WinogradF63Strategy8x8MK8::InputFeatureTrans(
        const std::vector<std::string>& strs) {
    auto InputPrepareF63NCHW88 = [](std::vector<std::string>) {
        std::stringstream ss;
        std::string kernel = R"(
        size_t IW8 = IW_ * PACK_C_SIZE;
        size_t iw8_start = iw_start * PACK_C_SIZE;
        size_t icb = ic / PACK_C_SIZE;
        if (!(inner && ic + PACK_C_SIZE < IC_)) {
            memset(patchT, 0, sizeof(gi_float16_t) * PACK_C_SIZE * Alpha * Alpha);
        }
        if (inner) {
            const gi_float16_t* input_ptr_ =
                    source + icb * IH_ * IW8 + ih_start * IW8 + iw8_start;
            for (size_t ih = 0; ih < Alpha; ih++) {
#define cb(i) GI_FLOAT16_t v##i = GiLoadFloat16(input_ptr_ + PACK_C_SIZE * i);
                UNROLL_CALL_NOWRAPPER(8, cb);
#undef cb

#define cb(i) GiStoreFloat16(patchT + ih * PACK_C_SIZE * Alpha + i * PACK_C_SIZE, v##i);
                UNROLL_CALL_NOWRAPPER(8, cb);
#undef cb
                input_ptr_ += IW8;
            }
        } else {
            int ih0_act = ih_start >0 ? ih_start:0,
                ih1_act = (ih_start + Alpha)< IH_?(ih_start + Alpha):IH_,
                iw0_act = iw_start > 0 ? iw_start : 0,
                iw1_act =(iw_start + Alpha)< IW_?(iw_start + Alpha):IW_;
            const gi_float16_t* input_ptr_ = source + icb * IH_ * IW8;
            // partial copy
            for (int ih = ih0_act; ih < ih1_act; ++ih) {
                for (int iw = iw0_act; iw < iw1_act; ++iw) {
                    size_t iho = ih - ih_start, iwo = iw - iw_start;
                    GI_FLOAT16_t src = GiLoadFloat16(input_ptr_ + ih * IW8 + iw * PACK_C_SIZE);
                    GiStoreFloat16(
                            patchT + iho * PACK_C_SIZE * Alpha + iwo * PACK_C_SIZE, src);
                }
            }
        }


)";
        return kernel;
    };
    auto InputTransformF63NCHW88 = [](std::vector<std::string>) {
        std::stringstream ss;
        std::string kernel = R"(
         // BT * d * B

        size_t ICB = IC_ / PACK_C_SIZE;

        GI_FLOAT16_t d0, d1, d2, d3, d4, d5, d6, d7;
#if defined(GI_TARGET_X86) || defined(GI_RVV_INTRINSICS)
        const gi_float16_t* v0 = input_parameters + 0 * PACK_C_SIZE;
        const gi_float16_t* v1 = input_parameters + 1 * PACK_C_SIZE;
#else
        GI_FLOAT16_t v0 = GiLoadFloat16(input_parameters + 0 * PACK_C_SIZE);
        GI_FLOAT16_t v1 = GiLoadFloat16(input_parameters + 1 * PACK_C_SIZE);
#endif

        //! B
        //!     1     0     0     0     0    0    0     0
        //!     0     1    -1   0.5  -0.5    2   -2    -1
        //! -5.25     1     1  0.25  0.25    4    4     0
        //!     0 -4.25  4.25  -2.5   2.5 -2.5  2.5  5.25
        //!  5.25 -4.25 -4.25 -1.25 -1.25   -5   -5     0
        //!     0     1    -1     2    -2  0.5 -0.5 -5.25
        //!    -1     1     1     1     1    1    1     0
        //!     0     0     0     0     0    0    0     1

#define cb(i)                                                                     \
    d1 = GiLoadFloat16(patchT + i * Alpha * PACK_C_SIZE + 1 * PACK_C_SIZE);           \
    d2 = GiLoadFloat16(patchT + i * Alpha * PACK_C_SIZE + 2 * PACK_C_SIZE);           \
    d3 = GiLoadFloat16(patchT + i * Alpha * PACK_C_SIZE + 3 * PACK_C_SIZE);           \
    d4 = GiLoadFloat16(patchT + i * Alpha * PACK_C_SIZE + 4 * PACK_C_SIZE);           \
    d5 = GiLoadFloat16(patchT + i * Alpha * PACK_C_SIZE + 5 * PACK_C_SIZE);           \
    d6 = GiLoadFloat16(patchT + i * Alpha * PACK_C_SIZE + 6 * PACK_C_SIZE);           \
    GI_FLOAT16_t t##i##0 = GiLoadFloat16(patchT + i * Alpha * PACK_C_SIZE + 0 * PACK_C_SIZE); \
    GI_FLOAT16_t t##i##7 = GiLoadFloat16(patchT + i * Alpha * PACK_C_SIZE + 7 * PACK_C_SIZE); \
    GI_FLOAT16_t t##i##1 = d6;                                                            \
    GI_FLOAT16_t t##i##2 = d6;                                                            \
    GI_FLOAT16_t t##i##3 = d6;                                                            \
    GI_FLOAT16_t t##i##4 = d6;                                                            \
    GI_FLOAT16_t t##i##5 = d6;                                                            \
    GI_FLOAT16_t t##i##6 = d6;                                                            \
    t##i##0 = GiSubtractFloat16(t##i##0, d6);                                                \
    t##i##1 = GiAddFloat16(t##i##1, d1);                                                \
    t##i##2 = GiSubtractFloat16(t##i##2, d1);                                                \
    t##i##3 = MADD(t##i##3, d1, v0, 2);                                           \
    t##i##4 = MSUB(t##i##4, d1, v0, 2);                                           \
    t##i##5 = MADD(t##i##5, d1, v0, 6);                                           \
    t##i##6 = MSUB(t##i##6, d1, v0, 6);                                           \
    t##i##7 = GiSubtractFloat16(t##i##7, d1);                                                \
    t##i##0 = MSUB(t##i##0, d2, v0, 0);                                           \
    t##i##1 = GiAddFloat16(t##i##1, d2);                                                \
    t##i##2 = GiAddFloat16(t##i##2, d2);                                                \
    t##i##3 = MADD(t##i##3, d2, v0, 3);                                           \
    t##i##4 = MADD(t##i##4, d2, v0, 3);                                           \
    t##i##5 = MADD(t##i##5, d2, v0, 7);                                           \
    t##i##6 = MADD(t##i##6, d2, v0, 7);                                           \
    t##i##1 = MSUB(t##i##1, d3, v0, 1);                                           \
    t##i##2 = MADD(t##i##2, d3, v0, 1);                                           \
    t##i##3 = MSUB(t##i##3, d3, v0, 4);                                           \
    t##i##4 = MADD(t##i##4, d3, v0, 4);                                           \
    t##i##5 = MSUB(t##i##5, d3, v0, 4);                                           \
    t##i##6 = MADD(t##i##6, d3, v0, 4);                                           \
    t##i##7 = MADD(t##i##7, d3, v0, 0);                                           \
    t##i##0 = MADD(t##i##0, d4, v0, 0);                                           \
    t##i##1 = MSUB(t##i##1, d4, v0, 1);                                           \
    t##i##2 = MSUB(t##i##2, d4, v0, 1);                                           \
    t##i##3 = MSUB(t##i##3, d4, v0, 5);                                           \
    t##i##4 = MSUB(t##i##4, d4, v0, 5);                                           \
    t##i##5 = MSUB(t##i##5, d4, v1, 0);                                           \
    t##i##6 = MSUB(t##i##6, d4, v1, 0);                                           \
    t##i##1 = GiAddFloat16(t##i##1, d5);                                                \
    t##i##2 = GiSubtractFloat16(t##i##2, d5);                                                \
    t##i##3 = MADD(t##i##3, d5, v0, 6);                                           \
    t##i##4 = MSUB(t##i##4, d5, v0, 6);                                           \
    t##i##5 = MADD(t##i##5, d5, v0, 2);                                           \
    t##i##6 = MSUB(t##i##6, d5, v0, 2);                                           \
    t##i##7 = MSUB(t##i##7, d5, v0, 0);
        UNROLL_CALL_RAW(8, cb);
#undef cb

#define cb(i)                                                                  \
    d0 = t0##i;                                                                \
    d1 = t6##i;                                                                \
    d2 = t6##i;                                                                \
    d3 = t6##i;                                                                \
    d4 = t6##i;                                                                \
    d5 = t6##i;                                                                \
    d6 = t6##i;                                                                \
    d7 = t7##i;                                                                \
    d0 = GiSubtractFloat16(d0, t6##i);                                                    \
    d1 = GiAddFloat16(d1, t1##i);                                                    \
    d2 = GiSubtractFloat16(d2, t1##i);                                                    \
    d3 = MADD(d3, t1##i, v0, 2);                                               \
    d4 = MSUB(d4, t1##i, v0, 2);                                               \
    d5 = MADD(d5, t1##i, v0, 6);                                               \
    d6 = MSUB(d6, t1##i, v0, 6);                                               \
    d7 = GiSubtractFloat16(d7, t1##i);                                                    \
    d0 = MSUB(d0, t2##i, v0, 0);                                               \
    d1 = GiAddFloat16(d1, t2##i);                                                    \
    d2 = GiAddFloat16(d2, t2##i);                                                    \
    d3 = MADD(d3, t2##i, v0, 3);                                               \
    d4 = MADD(d4, t2##i, v0, 3);                                               \
    d5 = MADD(d5, t2##i, v0, 7);                                               \
    d6 = MADD(d6, t2##i, v0, 7);                                               \
    d1 = MSUB(d1, t3##i, v0, 1);                                               \
    d2 = MADD(d2, t3##i, v0, 1);                                               \
    d3 = MSUB(d3, t3##i, v0, 4);                                               \
    d4 = MADD(d4, t3##i, v0, 4);                                               \
    d5 = MSUB(d5, t3##i, v0, 4);                                               \
    d6 = MADD(d6, t3##i, v0, 4);                                               \
    d7 = MADD(d7, t3##i, v0, 0);                                               \
    d0 = MADD(d0, t4##i, v0, 0);                                               \
    d1 = MSUB(d1, t4##i, v0, 1);                                               \
    d2 = MSUB(d2, t4##i, v0, 1);                                               \
    d3 = MSUB(d3, t4##i, v0, 5);                                               \
    d4 = MSUB(d4, t4##i, v0, 5);                                               \
    d5 = MSUB(d5, t4##i, v1, 0);                                               \
    d6 = MSUB(d6, t4##i, v1, 0);                                               \
    d1 = GiAddFloat16(d1, t5##i);                                                    \
    d2 = GiSubtractFloat16(d2, t5##i);                                                    \
    d3 = MADD(d3, t5##i, v0, 6);                                               \
    d4 = MSUB(d4, t5##i, v0, 6);                                               \
    d5 = MADD(d5, t5##i, v0, 2);                                               \
    d6 = MSUB(d6, t5##i, v0, 2);                                               \
    d7 = MSUB(d7, t5##i, v0, 0);                                               \
    GiStoreFloat16(                                                            \
            dst +                                              \
                    (0 * Alpha + i) * ICB * nr_tiles_in_loop_ * PACK_C_SIZE +     \
                    icb * nr_tiles_in_loop_ * PACK_C_SIZE + tile_idx * PACK_C_SIZE, \
            d0);                                                               \
    GiStoreFloat16(                                                            \
            dst +                                              \
                    (1 * Alpha + i) * ICB * nr_tiles_in_loop_ * PACK_C_SIZE +     \
                    icb * nr_tiles_in_loop_ * PACK_C_SIZE + tile_idx * PACK_C_SIZE, \
            d1);                                                               \
    GiStoreFloat16(                                                            \
            dst +                                              \
                    (2 * Alpha + i) * ICB * nr_tiles_in_loop_ * PACK_C_SIZE +     \
                    icb * nr_tiles_in_loop_ * PACK_C_SIZE + tile_idx * PACK_C_SIZE, \
            d2);                                                               \
    GiStoreFloat16(                                                            \
            dst +                                              \
                    (3 * Alpha + i) * ICB * nr_tiles_in_loop_ * PACK_C_SIZE +     \
                    icb * nr_tiles_in_loop_ * PACK_C_SIZE + tile_idx * PACK_C_SIZE, \
            d3);                                                               \
    GiStoreFloat16(                                                            \
            dst +                                              \
                    (4 * Alpha + i) * ICB * nr_tiles_in_loop_ * PACK_C_SIZE +     \
                    icb * nr_tiles_in_loop_ * PACK_C_SIZE + tile_idx * PACK_C_SIZE, \
            d4);                                                               \
    GiStoreFloat16(                                                            \
            dst +                                              \
                    (5 * Alpha + i) * ICB * nr_tiles_in_loop_ * PACK_C_SIZE +     \
                    icb * nr_tiles_in_loop_ * PACK_C_SIZE + tile_idx * PACK_C_SIZE, \
            d5);                                                               \
    GiStoreFloat16(                                                            \
            dst +                                              \
                    (6 * Alpha + i) * ICB * nr_tiles_in_loop_ * PACK_C_SIZE +     \
                    icb * nr_tiles_in_loop_ * PACK_C_SIZE + tile_idx * PACK_C_SIZE, \
            d6);                                                               \
    GiStoreFloat16(                                                            \
            dst +                                              \
                    (7 * Alpha + i) * ICB * nr_tiles_in_loop_ * PACK_C_SIZE +     \
                    icb * nr_tiles_in_loop_ * PACK_C_SIZE + tile_idx * PACK_C_SIZE, \
            d7);
        UNROLL_CALL_RAW(8, cb);
#undef cb


)";
        return kernel;
    };

    std::string input_process = R"(
    const uint32_t OUTPUT_BLOCK_SIZE = 6;
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
                                 int32_t tmp_vec[4];
        gi_float16_t* vec_ptr = (gi_float16_t*)(&tmp_vec[0]);


    const gi_float16_t input_parameters[16] = {5.25f, 4.25f, 0.5f, 0.25f, 2.5f, 1.25f,
                                               2.0f,  4.0f,  5.0f, 0.0f,  0.0f, 0.0f,
                                               0.0f,  0.0f,  0.0f, 0.0f};

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
    gi_float16_t* patch = transform_mid_ptr;
    gi_float16_t* patchT = transform_mid_ptr + PACK_C_SIZE * Alpha * Alpha;

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

            
            ${InputPrepareF63NCHW88()}
            ${InputTransformF63NCHW88()}
        }
    }
    )";

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
                    .add("InputTransformF63NCHW88", InputTransformF63NCHW88)
                    .add("InputPrepareF63NCHW88", InputPrepareF63NCHW88)
                    .render(input_process);
    return ss.str();
}

std::string WinogradF63Strategy8x8MK8::DependMatmulSymbol() {
    return Fp16MatmulM8N8MK8Kernel().GetKernelSymbol(NULL);
}

std::string WinogradF63Strategy8x8MK8::OutputFeatureTrans(
        const std::vector<std::string>& strs, TContext* ctx) {
    std::string ouput_trans = R"(
    gi_float16_t* transform_output_ptr_ = ${transform_output_ptr};
    gi_float16_t* outptr_ = ${outptr};
    const gi_float16_t* bias = ${bias_ptr};
    
    uint32_t OH_ = ${OH};
    uint32_t OW_ = ${OW};
    uint32_t OC_ = ${OC};
    uint32_t tile_id_ = ${tile_id};
    uint32_t nr_tiles_in_loop_ = ${nr_tiles_in_loop};
    uint32_t tiles_w_ = (OW_ + OutputBlockSize -1) / OutputBlockSize;
    for (uint32_t oc = 0; oc < OC_; oc += PACK_C_SIZE) {
        for(uint32_t tile_idx = 0; tile_idx < nr_tiles_in_loop_; tile_idx++) {
            uint32_t index = tile_id_ + tile_idx;
            uint32_t nh = index / tiles_w_;
            uint32_t nw = index % tiles_w_;
            uint32_t oh_start = nh * OutputBlockSize;
            uint32_t ow_start = nw * OutputBlockSize;
        //! AT * m * A

        size_t OCB = OC_ / PACK_C_SIZE;
        size_t ocb = oc / PACK_C_SIZE;

#define cb(m, n)                                                   \
    GI_FLOAT16_t v##m##n = GiLoadFloat16(                                  \
            transform_output_ptr_ +                                 \
            (m * Alpha + n) * OCB * nr_tiles_in_loop_ * PACK_C_SIZE + \
            ocb * nr_tiles_in_loop_ * PACK_C_SIZE + tile_idx * PACK_C_SIZE); 

        UNROLL_CALL_NOWRAPPER_D2(8, 8, cb);
#undef cb

        /**
         * A
         *
         * 1    0    0      0       0         0
         * 1    1    1      1       1         1
         * 1   -1    1     -1       1        -1
         * 1    2    4      8      16        32
         * 1   -2    4     -8      16       -32
         * 1  0.5 0.25  0.125  0.0625   0.03125
         * 1 -0.5 0.25 -0.125  0.0625  -0.03125
         * 0    0    0      0       0         1
         */

        /*
         * v1addv2 = v1##m + v2##m;
         * v1subv2 = v1##m - v2##m;
         * v3addv4 = v3##m + v4##m;
         * v3subv4 = v3##m - v4##m;
         * v5addv6 = v5##m + v6##m;
         * v5subv6 = v5##m - v6##m;
         * t0##m = v0##m + v1addv2 + v3addv4 + v5addv6;
         * t1##m = v1subv2 + v3subv4 * 2.f + v5subv6 * 0.5f;
         * t2##m = v1addv2 + v3addv4 * 4.f + v5addv6 * 0.25f;
         * t3##m = v1subv2 + v3subv4 * 8.f + v5subv6 * 0.125f;
         * t4##m = v1addv2 + v3addv4 * 16.f + v5addv6 * 0.0625f;
         * t5##m = v1subv2 + v3subv4 * 32.f + v5subv6 * 0.03125f + v7##m;
         */
        GI_FLOAT16_t v1addv2, v1subv2, v3addv4, v3subv4, v5addv6, v5subv6;
#define cb(m)                                                                         \
    v1addv2 = GiAddFloat16(v1##m, v2##m);                                                     \
    v1subv2 = GiSubtractFloat16(v1##m, v2##m);                                                     \
    v3addv4 = GiAddFloat16(v3##m, v4##m);                                                     \
    v3subv4 = GiSubtractFloat16(v3##m, v4##m);                                                     \
    v5addv6 = GiAddFloat16(v5##m, v6##m);                                                     \
    v5subv6 = GiSubtractFloat16(v5##m, v6##m);                                                     \
    GI_FLOAT16_t t0##m = GiAddFloat16(GiAddFloat16(GiAddFloat16(v0##m, v1addv2), v3addv4), v5addv6);                  \
    GI_FLOAT16_t t1##m = GiAddFloat16(GiAddFloat16(v1subv2, GiMultiplyScalerFloat16(v3subv4, 2.f)), GiMultiplyScalerFloat16(v5subv6, 0.5f));      \
    GI_FLOAT16_t t2##m = GiAddFloat16(GiAddFloat16(v1addv2, GiMultiplyScalerFloat16(v3addv4, 4.f)), GiMultiplyScalerFloat16(v5addv6, 0.25f));     \
    GI_FLOAT16_t t3##m = GiAddFloat16(GiAddFloat16(v1subv2, GiMultiplyScalerFloat16(v3subv4, 8.f)), GiMultiplyScalerFloat16(v5subv6, 0.125f));    \
    GI_FLOAT16_t t4##m = GiAddFloat16(GiAddFloat16(v1addv2, GiMultiplyScalerFloat16(v3addv4, 16.f)), GiMultiplyScalerFloat16(v5addv6, 0.0625f));  \
    GI_FLOAT16_t t5##m =                                                                      \
            GiAddFloat16(GiAddFloat16(GiAddFloat16(v1subv2, GiMultiplyScalerFloat16(v3subv4, 32.f)), GiMultiplyScalerFloat16(v5subv6, 0.03125f)), \
                 v7##m);

        UNROLL_CALL_NOWRAPPER(8, cb);
#undef cb

        /*
         * v1addv2 = t##m##1 + t##m##2;
         * v1subv2 = t##m##1 - t##m##2;
         * v3addv4 = t##m##3 + t##m##4;
         * v3subv4 = t##m##3 - t##m##4;
         * v5addv6 = t##m##5 + t##m##6;
         * v5subv6 = t##m##5 - t##m##6;
         * v##m##0 = t##m##0 + v1addv2 + v3addv4 + v5addv6;
         * v##m##1 = v1subv2 + v3subv4 * 2.f + v5subv6 * 0.5f;
         * v##m##2 = v1addv2 + v3addv4 * 4.f + v5addv6 * 0.25f;
         * v##m##3 = v1subv2 + v3subv4 * 8.f + v5subv6 * 0.125f;
         * v##m##4 = v1addv2 + v3addv4 * 16.f + v5addv6 * 0.0625f;
         * v##m##5 = v1subv2 + v3subv4 * 32.f + v5subv6 * 0.03125f + t##m##7;
         */
#define cb(m)                                                                         \
    v1addv2 = GiAddFloat16(t##m##1, t##m##2);                                                 \
    v1subv2 = GiSubtractFloat16(t##m##1, t##m##2);                                                 \
    v3addv4 = GiAddFloat16(t##m##3, t##m##4);                                                 \
    v3subv4 = GiSubtractFloat16(t##m##3, t##m##4);                                                 \
    v5addv6 = GiAddFloat16(t##m##5, t##m##6);                                                 \
    v5subv6 = GiSubtractFloat16(t##m##5, t##m##6);                                                 \
    v##m##0 = GiAddFloat16(GiAddFloat16(GiAddFloat16(t##m##0, v1addv2), v3addv4), v5addv6);                   \
    v##m##1 = GiAddFloat16(GiAddFloat16(v1subv2, GiMultiplyScalerFloat16(v3subv4, 2.f)), GiMultiplyScalerFloat16(v5subv6, 0.5f));         \
    v##m##2 = GiAddFloat16(GiAddFloat16(v1addv2, GiMultiplyScalerFloat16(v3addv4, 4.f)), GiMultiplyScalerFloat16(v5addv6, 0.25f));        \
    v##m##3 = GiAddFloat16(GiAddFloat16(v1subv2, GiMultiplyScalerFloat16(v3subv4, 8.f)), GiMultiplyScalerFloat16(v5subv6, 0.125f));       \
    v##m##4 = GiAddFloat16(GiAddFloat16(v1addv2, GiMultiplyScalerFloat16(v3addv4, 16.f)), GiMultiplyScalerFloat16(v5addv6, 0.0625f));     \
    v##m##5 =                                                                         \
            GiAddFloat16(GiAddFloat16(GiAddFloat16(v1subv2, GiMultiplyScalerFloat16(v3subv4, 32.f)), GiMultiplyScalerFloat16(v5subv6, 0.03125f)), \
                 t##m##7);

        UNROLL_CALL_NOWRAPPER(6, cb);
#undef cb

        GI_FLOAT16_t vbias;
        if (bias) {
            vbias = GiLoadFloat16(bias + oc);

#define cb(m, n) v##m##n = GiAddFloat16(v##m##n, vbias);
            UNROLL_CALL_RAW_D2(6, 6, cb);
#undef cb
        }
${nonline_gen_init()}
${nonline_gen_func(v00, vbias)};v00=vbias;
${nonline_gen_func(v01, vbias)};v01=vbias;
${nonline_gen_func(v02, vbias)};v02=vbias;
${nonline_gen_func(v03, vbias)};v03=vbias;
${nonline_gen_func(v04, vbias)};v04=vbias;
${nonline_gen_func(v05, vbias)};v05=vbias;

${nonline_gen_func(v10, vbias)};v10=vbias;
${nonline_gen_func(v11, vbias)};v11=vbias;
${nonline_gen_func(v12, vbias)};v12=vbias;
${nonline_gen_func(v13, vbias)};v13=vbias;
${nonline_gen_func(v14, vbias)};v14=vbias;
${nonline_gen_func(v15, vbias)};v15=vbias;

${nonline_gen_func(v20, vbias)};v20=vbias;
${nonline_gen_func(v21, vbias)};v21=vbias;
${nonline_gen_func(v22, vbias)};v22=vbias;
${nonline_gen_func(v23, vbias)};v23=vbias;
${nonline_gen_func(v24, vbias)};v24=vbias;
${nonline_gen_func(v25, vbias)};v25=vbias;

${nonline_gen_func(v30, vbias)};v30=vbias;
${nonline_gen_func(v31, vbias)};v31=vbias;
${nonline_gen_func(v32, vbias)};v32=vbias;
${nonline_gen_func(v33, vbias)};v33=vbias;
${nonline_gen_func(v34, vbias)};v34=vbias;
${nonline_gen_func(v35, vbias)};v35=vbias;

${nonline_gen_func(v40, vbias)};v40=vbias;
${nonline_gen_func(v41, vbias)};v41=vbias;
${nonline_gen_func(v42, vbias)};v42=vbias;
${nonline_gen_func(v43, vbias)};v43=vbias;
${nonline_gen_func(v44, vbias)};v44=vbias;
${nonline_gen_func(v45, vbias)};v45=vbias;

${nonline_gen_func(v50, vbias)};v50=vbias;
${nonline_gen_func(v51, vbias)};v51=vbias;
${nonline_gen_func(v52, vbias)};v52=vbias;
${nonline_gen_func(v53, vbias)};v53=vbias;
${nonline_gen_func(v54, vbias)};v54=vbias;
${nonline_gen_func(v55, vbias)};v55=vbias;

#define out_save(oho, owo)                                                           \
    do {                                                                             \
        size_t oh = oh_start + oho;                                                  \
        size_t ow = ow_start + owo;                                                  \
        if (oh < OH && ow < OW) {                                                    \
            GiStoreFloat16(                                                          \
                    outptr_ + oc * OH * OW + oh * OW * PACK_C_SIZE + ow * PACK_C_SIZE,    \
                    v##oho##owo);                                                    \
        }                                                                            \
    } while (0);
        UNROLL_CALL_RAW_D2(6, 6, out_save);

#undef out_save

#undef MSUB
#undef MADD
        }
    }
    )";
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
