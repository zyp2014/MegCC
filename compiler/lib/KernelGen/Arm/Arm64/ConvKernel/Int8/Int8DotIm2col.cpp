/**
 * \file
 * compiler/lib/KernelGen/Arm/Arm64/ConvKernel/Int8/Int8DotIm2col.cpp
 *
 * This file is part of MegCC, a deep learning compiler developed by Megvii.
 *
 * \copyright Copyright (c) 2021-2022 Megvii Inc. All rights reserved.
 */
#include <sstream>
#include <string>
#include "Arm/Arm64/Activation.h"
#include "Arm/Arm64/ConvKernel.h"
#include "Arm/Arm64/InternalKernel/InternalKernel.h"
#include "Arm/ArmCommon/Im2colHelper.h"
#include "compiler/KernelGen/KernelGen.h"

using namespace megcc;
using namespace KernelGen;
using namespace Arm64;
using namespace ArmCommon;
namespace {

int get_pack_c_size(TContext* ctx) {
    int pack_c_size = 1;
    auto fmt = ctx->getAttrStr("format");
    if (fmt == "NCHW44_DOT") {
        pack_c_size = 4;
    } else {
        CC_ASSERT(fmt == "NCHW");
    }
    return pack_c_size;
}

int get_group_weight_ndim(TContext* ctx) {
    int group_weight_ndim = 5;
    auto fmt = ctx->getAttrStr("format");
    if (fmt == "NCHW44_DOT") {
        group_weight_ndim = 7;
    } else {
        CC_ASSERT(fmt == "NCHW");
    }
    return group_weight_ndim;
}

std::string gen_im2col(TContext* ctx, TContext* inner_ctx) {
    std::stringstream ss;
    auto fmt = ctx->getAttrStr("format");
    if (fmt == "NCHW44_DOT") {
        ss << gen_nchw44_im2col_kern(inner_ctx);
        ss << gen_nchw44_pad_src_kern(inner_ctx);
    } else {
        CC_ASSERT(fmt == "NCHW");
        auto sh = ctx->getAttrInt("stride_h");
        auto sw = ctx->getAttrInt("stride_w");
        if (sh == sw && sw == 1) {
            ss << nchw_im2col_s1_kern;
        } else {
            ss << nchw_im2col_kern;
        }
        ss << nchw_pad_src_kern;
    }
    return ss.str();
}

}  // namespace
std::string ConvIm2colDot::GetKernelSymbol(TContext* ctx) const {
    std::stringstream extra_ss;
    if (is_bias(ctx)) {
        extra_ss << "_bias";
    }
    if (ctx->haveAttr("nonlineMode") && ctx->getAttrStr("nonlineMode") != "IDENTITY") {
        extra_ss << "_" << ctx->getAttrStr("nonlineMode");
    }
    extra_ss << "_" << SymbolHelper::gen_io_str(ctx);
    std::string name_temp =
            "Arm64_kernel_conv2d_im2col_dot_${kernel_h}x${kernel_w}_${"
            "format}_${sparse}_p${pad_h}x${pad_w}_s${stride_h}x${stride_w}_d${"
            "dilate_h}x${dilate_w}${extra}";
    return StringTemplate::StringTemplateArgs(ctx)
            .add_ctx_int("kernel_h")
            .add_ctx_int("kernel_w")
            .add_ctx_str("format")
            .add_ctx_str("sparse")
            .add_ctx_int("pad_h")
            .add_ctx_int("pad_w")
            .add_ctx_int("stride_h")
            .add_ctx_int("stride_w")
            .add_ctx_int("dilate_h")
            .add_ctx_int("dilate_w")
            .add("extra", extra_ss.str())
            .render(name_temp);
}

bool ConvIm2colDot::IsAvailable(TContext* ctx) const {
    auto fmt = ctx->getAttrStr("format");
    int nr_operands = ctx->getAttrInt("nr_operands");
    std::string dst_oprands = std::string("operand:") + std::to_string(nr_operands - 1);
    bool param_value_ok =
            ctx->getAttrUInt("dilate_h") == 1 && ctx->getAttrUInt("dilate_w") == 1;
    bool param_mode_ok =
            (fmt == "NCHW44_DOT") && ctx->getAttrStr("mode") == "CROSS_CORRELATION";
    bool noline_ok = !ctx->haveAttr("nonlineMode") ||
                     ctx->getAttrStr("nonlineMode") == "IDENTITY" ||
                     ctx->getAttrStr("nonlineMode") == "RELU" ||
                     ctx->getAttrStr("nonlineMode") == "H_SWISH";
    bool type_ok = is_qint8_conv_dtype(ctx);
    bool layout_ok = ctx->getAttrOprand("operand:0").shape.size() == 4 &&
                     ctx->getAttrOprand(dst_oprands).shape.size() == 4;
    bool weight_ok = (ctx->getAttrStr("sparse") == "GROUP" &&
                      ctx->getAttrOprand("operand:1").shape.size() == 5) ||
                     (ctx->getAttrStr("sparse") == "DENSE" &&
                      ctx->getAttrOprand("operand:1").shape.size() == 4);
    if (fmt == "NCHW44_DOT") {
        layout_ok = ctx->getAttrOprand("operand:0").shape.size() == 5 &&
                    ctx->getAttrOprand(dst_oprands).shape.size() == 5;
        weight_ok = (ctx->getAttrStr("sparse") == "GROUP" &&
                     ctx->getAttrOprand("operand:1").shape.size() == 7) ||
                    (ctx->getAttrStr("sparse") == "DENSE" &&
                     ctx->getAttrOprand("operand:1").shape.size() == 6);
    }
    bool availabe = param_value_ok && param_mode_ok && type_ok && noline_ok &&
                    layout_ok && weight_ok;
    return availabe;
}

std::string ConvIm2colDot::GetInitBody(TContext* ctx) const {
    std::stringstream writer;
    auto inner_ctx = GetInnerCtx(ctx);
    auto inner_gemm = GetInnerCtxMatmul(ctx);
    writer << inner_gemm->GetPackASignature(inner_ctx.get()) << ";\n";
    writer << inner_gemm->GetPackAWorkspaceSignature(inner_ctx.get()) << ";\n";
    writer << GenCommonRet() << " " << GetInitSignature(ctx);
    const bool is_group = ctx->getAttrStr("sparse") == "GROUP";
    const uint32_t nr_out_weight = 1;
    const std::string group_str = is_group ? "in_weights->layout.dims[0]" : "1";
    const int ocpg_offset = is_group ? 1 : 0;
    const int pack_c_size = get_pack_c_size(ctx);
    const std::string common_def = StringTemplate::StringTemplateArgs()
                                           .add("group_str", group_str)
                                           .add("ocpg_offset", ocpg_offset)
                                           .add("pack_c_size", pack_c_size)
                                           .render(R"(
        const int pack_c_size = ${pack_c_size};
        Tensor* in_weights = inputs[1];
        const int group = ${group_str};
        const int ymax = in_weights->layout.dims[${ocpg_offset}] * pack_c_size;
        const int kmax = pack_c_size * in_weights->layout.dims[${ocpg_offset} + 1] * in_weights->layout.dims[${ocpg_offset} + 2] * in_weights->layout.dims[${ocpg_offset} + 3];
        const int ldin = kmax * pack_c_size;
    )");
    const std::string fill_weight_attr =
            R"(
        out_weights->layout.nr_dim = 2;
        out_weights->layout.dims[0] = group;
        out_weights->layout.dims[1] = )" +
            inner_gemm->GetPackAWorkspaceSymbol(inner_ctx.get()) +
            R"((0, ymax, 0, kmax);
        out_weights->layout.stride[0] = out_weights->layout.dims[1];
        out_weights->layout.stride[1] = 1;
        out_weights->dtype.type_enum = TinyNN_QINT8;
        out_weights->name = in_weights->name;
        out_weights->dtype.param.scale = in_weights->dtype.param.scale;
    )";
    const std::string fill_weight_transform =
            StringTemplate::StringTemplateArgs()
                    .add("packa_sym", inner_gemm->GetPackASymbol(inner_ctx.get()))
                    .render(
                            R"(    
        int8_t* outptr = out_weights->ptr;
        int8_t* inptr = in_weights->ptr;
        for(int group_idx = 0; group_idx < group; ++group_idx){
            ${packa_sym}(outptr, inptr, ldin, 0, ymax, 0, kmax);
            outptr += out_weights->layout.dims[1];
            inptr += ymax * kmax;
        }
    )");
    writer << StringTemplate::render_init_body(
            nr_out_weight, fill_weight_attr, fill_weight_transform, common_def);

    return writer.str();
}

MatmulInternal* ConvIm2colDot::GetInnerCtxMatmul(TContext* ctx) const {
    static MatmulInt8DotM8N12MK4Kernel inner_mk4_gemm;
    static MatmulM8N12Kernel inner_gemm;
    auto fmt = ctx->getAttrStr("format");
    if (fmt == "NCHW44_DOT") {
        return &inner_mk4_gemm;
    } else {
        return &inner_gemm;
    }
}

std::string ConvIm2colDot::GetWorkspaceBodyCondition(TContext* ctx, bool jit) const {
    std::stringstream ss;
    auto inner_ctx = GetInnerCtx(ctx);
    auto inner_gemm = GetInnerCtxMatmul(ctx);
    const int pack_c_size = get_pack_c_size(ctx);
    const int group_weight_dim = get_group_weight_ndim(ctx);
    if (jit) {
        ss << inner_gemm->GetPackBWorkspaceBody(inner_ctx.get()) << ";\n";
    } else {
        ss << "extern " << inner_gemm->GetPackBWorkspaceSignature(inner_ctx.get())
           << ";\n";
    }
    ss << GenCommonRet() << " " << GetWorkspaceSignature(ctx);
    std::string bypass_workspace;
    if (ConvImpl::is_no_pad(ctx)) {
        bypass_workspace = "pad_out = 0;";
    }
    std::string temp_workspace = "size_t temp_dst = 0;";
    if (inner_gemm->need_post_process(inner_ctx.get())) {
        temp_workspace = "size_t temp_dst = oc * ohw * sizeof(int32_t) + 64;";
    }
    std::string workspace_temp =
            R"({
        TINYNN_ASSERT(workspace);
        const int pack_c_size = ${pack_c_size};
        const Layout in_layout = inputs[0]->layout;
        const uint32_t ic = in_layout.dims[1] * pack_c_size;
        const uint32_t ih = in_layout.dims[2];
        const uint32_t iw = in_layout.dims[3];
        const Layout weight_layout = inputs[1]->layout;
        uint32_t group = 1;
        uint32_t oc = weight_layout.dims[0] * 4;
        uint32_t fh = weight_layout.dims[2];
        uint32_t fw = weight_layout.dims[3];
        if (weight_layout.nr_dim == ${group_weight_dim}) {
            group = weight_layout.dims[0];
            oc = weight_layout.dims[1] * 4;
            fh = weight_layout.dims[3];
            fw = weight_layout.dims[4];
        }
        const uint32_t icpg = ic / group;
        
        const uint32_t k = fh * fw * icpg; 
        const uint32_t oh = (ih - fh + 2 * ${pad_h}) / ${stride_h} + 1;
        const uint32_t ow = (iw - fw + 2 * ${pad_w}) / ${stride_w} + 1;        
        const uint32_t ohw = oh * ow;
        
        const int preset_block_ohw = 192;
        const int block_ohw = preset_block_ohw > ohw ? ohw : preset_block_ohw;
        size_t pad_out = (size_t) icpg * (ih + 2 * ${pad_h}) * (iw + 2 * ${pad_w}) * sizeof(int8_t) + 64;
        size_t im2col_out = (size_t) block_ohw * k * sizeof(int8_t) + 64;                
        size_t packed_out = ${packb_workspace_func}(0, block_ohw, 0, k) + 64;
        ${temp_workspace}
        ${bypass_workspace}
        size_t res = im2col_out + packed_out + pad_out + temp_dst;

        *workspace = res;
        return TinyNN_SUCCESS;
    })";
    ss << StringTemplate::StringTemplateArgs(ctx)
                    .add_ctx_int("pad_h")
                    .add_ctx_int("pad_w")
                    .add_ctx_int("stride_h")
                    .add_ctx_int("stride_w")
                    .add("pack_c_size", pack_c_size)
                    .add("group_weight_dim", group_weight_dim)
                    .add("packb_workspace_func",
                         inner_gemm->GetPackBWorkspaceSymbol(inner_ctx.get()))
                    .add("temp_workspace", temp_workspace)
                    .add("bypass_workspace", bypass_workspace)
                    .render(workspace_temp);
    return ss.str();
}

std::vector<KernelObj> ConvIm2colDot::GetDependInternalSymbol(TContext* ctx) const {
    auto inner_ctx = GetInnerCtx(ctx);
    auto inner_gemm = GetInnerCtxMatmul(ctx);
    return {
            {inner_gemm->GetKernelSymbol(inner_ctx.get()),
             inner_gemm->GetKernelBody(inner_ctx.get()),
             inner_gemm->GetBodyGuardBegin(inner_ctx.get()),
             inner_gemm->GetBodyGuardEnd(inner_ctx.get()),
             inner_gemm->GetDependInternalSymbol(inner_ctx.get())}};
}

std::shared_ptr<TContext> ConvIm2colDot::GetInnerCtx(TContext* ctx) const {
    auto inner_ctx = std::make_shared<CodeGenContext>();
    if (ctx->haveAttr("nonlineMode")) {
        inner_ctx->setAttr("nonlineMode", CCAttr(ctx->getAttrStr("nonlineMode")));
    }
    inner_ctx->setAttr("with_bias", ConvImpl::is_bias(ctx));
    inner_ctx->setAttr("transposeA", false);
    inner_ctx->setAttr("transposeB", false);
    inner_ctx->setAttr("dtype", ctx->getAttrOprand("operand:0").dtype);
    auto fmt = ctx->getAttrStr("format");
    if (fmt == "NCHW44_DOT") {
        inner_ctx->setAttr("format", "MK4_DOT");
    } else {
        CC_ASSERT(fmt == "NCHW");
        inner_ctx->setAttr("format", "NCHW");
    }
    return inner_ctx;
}

std::string ConvIm2colDot::GetKernelBody(TContext* ctx) const {
    auto inner_ctx = GetInnerCtx(ctx);
    auto inner_gemm = GetInnerCtxMatmul(ctx);
    std::stringstream writer;
    writer << "#include <string.h>\n";
    writer << "#include <stdbool.h>\n";
    writer << "#include <arm_neon.h>\n";
    writer << inner_gemm->GetPackBWorkspaceSignature(inner_ctx.get()) << ";\n";
    writer << inner_gemm->GetNakedKernelSignature(inner_ctx.get()) << ";\n";
    writer << inner_gemm->GetPackBSignature(inner_ctx.get()) << ";\n";

    writer << gen_im2col(ctx, inner_ctx.get());
    writer << GenCommonRet() << " " << GetKernelSignature(ctx);
    std::string bias_ptr_str = is_bias(ctx) ? "inputs[2]->ptr;" : "0;";
    const int pack_c_size = get_pack_c_size(ctx);
    std::string do_pad_src = R"(
        pad_src(input_data + group_idx * group_src_stride, pad_out_ptr, icpg, ih, iw, pad_h, pad_w);
        )";
    if (ConvImpl::is_no_pad(ctx)) {
        do_pad_src = "pad_out_ptr = input_data + group_idx * group_src_stride;";
    }
    std::string gen_temp_dst = "int32_t* temp_dst = NULL;";
    if (inner_gemm->need_post_process(ctx)) {
        gen_temp_dst = "int32_t* temp_dst = (int32_t*)(pad_out_ptr + pad_out_offset);";
    }
    std::string temp_body =
            R"({
        const int pack_c_size = ${pack_c_size};
        const uint32_t pad_h = ${pad_h};
        const uint32_t pad_w = ${pad_w};
        const uint32_t stride_h = ${stride_h};
        const uint32_t stride_w = ${stride_w};
        const uint32_t fh = ${kernel_h};
        const uint32_t fw = ${kernel_w};

        const float src_scale = inputs[0]->dtype.param.scale;
        const float flt_scale = inputs[1]->dtype.param.scale;
        const float dst_scale = outputs[0]->dtype.param.scale;
        const float temp_scale = src_scale * flt_scale;
        const float dst_scale_inv = 1.f / dst_scale;
        const float scale = src_scale * flt_scale / dst_scale;

        int8_t* input_data = inputs[0]->ptr;
        int8_t* output_data = outputs[0]->ptr;

        Layout in_layout = inputs[0]->layout;
        Layout out_layout = outputs[0]->layout;
        const int n = in_layout.dims[0];
        const int ic = in_layout.dims[1] * pack_c_size;
        const int ih = in_layout.dims[2];
        const int iw = in_layout.dims[3];

        const int oc = out_layout.dims[1] * pack_c_size;
        const int oh = out_layout.dims[2];
        const int ow = out_layout.dims[3];
        const int ohw = oh * ow;
        
        const size_t N = ohw;
        const size_t LDC = ohw * pack_c_size;
        const size_t align_size = 64;
        const int preset_block_ohw = 192;
        const int block_ohw = preset_block_ohw > ohw ? ohw : preset_block_ohw;

        Layout weight_layout = inputs[1]->layout;
        const int group = weight_layout.dims[0];
        const int icpg = ic / group;
        const int ocpg = oc / group;
        const int group_src_stride = icpg * ih * iw;
        const int group_weight_stride = weight_layout.dims[1];
        const size_t K = fh * fw * icpg;

        const size_t temp_pad_out = (size_t) icpg * (ih + 2 * pad_h) * (iw + 2 * pad_w) * sizeof(int8_t);
        const size_t pad_out_offset = (temp_pad_out + align_size - 1) / align_size * align_size;

        const size_t temp_im2col = block_ohw * K * sizeof(int8_t);
        const size_t im2col_offset = (temp_im2col + align_size - 1) / align_size * align_size;
 
        const size_t temp_packb = ${packb_size_sym}(0, block_ohw, 0, K);
        const size_t packb_offset = (temp_packb + align_size - 1) / align_size * align_size;

        int8_t* im2col_ptr = workspace->ptr;
        int8_t* packb_ptr = workspace->ptr + im2col_offset;
        int8_t* pad_out_ptr = workspace->ptr + im2col_offset + packb_offset;
        ${gen_temp_dst}

        for (int n_idx = 0; n_idx < n; ++n_idx) {
            int8_t* weight_data = inputs[1]->ptr;
            int32_t* bias_data = ${bias_ptr_str}            
            for(int group_idx = 0; group_idx < group; ++group_idx){
                int8_t* group_weight_data = weight_data + group_idx * group_weight_stride;
                int32_t* group_bias_data = bias_data + group_idx * ocpg;
                int8_t* group_ouput_data = output_data + group_idx * ocpg * ohw;
                ${do_pad_src}
                for(int ohw_idx = 0; ohw_idx < ohw; ohw_idx += block_ohw){
                    const int real_block_ohw = block_ohw < (ohw - ohw_idx)? block_ohw:(ohw - ohw_idx);
                    const int packed_iw = iw + 2 * pad_w; 
                    const int packed_ih = ih + 2 * pad_h;
                    
                    img2col(pad_out_ptr, im2col_ptr, ow, icpg, packed_ih, packed_iw, fh, fw,
                            stride_h, stride_w, ohw_idx, real_block_ohw);
                            
                    ${pack_b_sym}(packb_ptr, im2col_ptr, real_block_ohw * pack_c_size, 0, real_block_ohw, 0, K);
                    ${naked_kern_sym}(group_weight_data, packb_ptr, group_ouput_data + ohw_idx * pack_c_size, LDC, ocpg, real_block_ohw, K, group_bias_data, temp_dst, scale, temp_scale, dst_scale_inv);
                }
            }
            input_data += ic * ih * iw;
            output_data += oc * oh * ow;
        }
        return TinyNN_SUCCESS;
    })";
    writer << StringTemplate::StringTemplateArgs(ctx)
                      .add_ctx_int("pad_h")
                      .add_ctx_int("pad_w")
                      .add_ctx_int("stride_h")
                      .add_ctx_int("stride_w")
                      .add_ctx_int("kernel_h")
                      .add_ctx_int("kernel_w")
                      .add("pack_c_size", pack_c_size)
                      .add("bias_ptr_str", bias_ptr_str)
                      .add("pack_b_sym", inner_gemm->GetPackBSymbol(inner_ctx.get()))
                      .add("naked_kern_sym",
                           inner_gemm->GetNakedKernelSymbol(inner_ctx.get()))
                      .add("do_pad_src", do_pad_src)
                      .add("packb_size_sym",
                           inner_gemm->GetPackBWorkspaceSymbol(inner_ctx.get()))
                      .add("gen_temp_dst", gen_temp_dst)
                      .render(temp_body);
    return writer.str();
}

// vim: syntax=cpp.doxygen
