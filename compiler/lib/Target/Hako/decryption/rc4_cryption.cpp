/**
 * \file compiler/lib/Target/Hako/decryption/rc4_cryption.cpp
 *
 * This file is part of MegCC, a deep learning compiler developed by Megvii.
 *
 * \copyright Copyright (c) 2021-2022 Megvii Inc. All rights reserved.
 */

#include "rc4_cryption.h"
#include "rc4/rc4_cryption_impl.h"

#include <vector>

using namespace megcc;

std::vector<uint8_t> RC4::decrypt_model(
        const void* model_mem, size_t size, const std::vector<uint8_t>& key) {
    RC4Impl rc4_impl(model_mem, size, key);
    rc4_impl.init_rc4_state();
    return rc4_impl.decrypt_model();
}

std::vector<uint8_t> RC4::encrypt_model(
        const void* model_mem, size_t size, const std::vector<uint8_t>& key) {
    RC4Impl rc4_impl(model_mem, size, key);
    return rc4_impl.encrypt_model();
}

std::vector<uint8_t> RC4::get_decrypt_key() {
    std::vector<uint8_t> keys(128, 0);
    uint64_t* data = reinterpret_cast<uint64_t*>(keys.data());
    data[0] = rc4::key_gen_hash_key();
    data[1] = rc4::key_gen_enc_key();
    return keys;
}

std::vector<uint8_t> SimpleFastRC4::decrypt_model(
        const void* model_mem, size_t size, const std::vector<uint8_t>& key) {
    SimpleFastRC4Impl simple_fast_rc4_impl(model_mem, size, key);
    simple_fast_rc4_impl.init_sfrc4_state();
    return simple_fast_rc4_impl.decrypt_model();
}
std::vector<uint8_t> SimpleFastRC4::encrypt_model(
        const void* model_mem, size_t size, const std::vector<uint8_t>& key) {
    SimpleFastRC4Impl simple_fast_rc4_impl(model_mem, size, key);
    return simple_fast_rc4_impl.encrypt_model();
}

std::vector<uint8_t> SimpleFastRC4::get_decrypt_key() {
    std::vector<uint8_t> keys(128, 0);
    uint64_t* data = reinterpret_cast<uint64_t*>(keys.data());
    //! old model use rc4 key, but new model use this key
    data[0] = 0x0123456789abcdef;
    data[1] = 0xfdecba9876543210;
    return keys;
}

std::vector<uint8_t> NaiveEncrypt::decrypt_model(
        const void* model_mem, size_t size, const std::vector<uint8_t>& key) {
    NaiveEncrypImpl naive_impl(model_mem, size, key);
    return naive_impl.decrypt_model();
}

std::vector<uint8_t> NaiveEncrypt::get_decrypt_key() {
    std::vector<uint8_t> keys(128, 0);
    uint64_t* data = reinterpret_cast<uint64_t*>(keys.data());
    data[0] = rc4::key_gen_hash_key();
    data[1] = rc4::key_gen_enc_key();
    return keys;
}

// vim: syntax=cpp.doxygen foldmethod=marker foldmarker=f{{{,f}}}
