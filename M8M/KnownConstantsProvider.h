/*
 * Copyright (C) 2015 Massimo Del Zotto
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "../Common/AREN/ArenDataTypes.h"
#include <vector>
#include "../Common/AES.h"

//! Special magic values common to various kernels.
enum class CryptoConstant { // scoped, thus no need for cc_ prefix :)
    AES_T,
    SIMD_alpha,
    SIMD_beta
};


/*! A big class containing those table constants often recurring in various crypto algorithms. */
class KnownConstantProvider {
    std::vector<ashort> simd_alpha;
    std::vector<aushort> simd_beta;
    std::vector<auint> aes_t_tables;

    void Init_aes_t_tables() {
        aes_t_tables.resize(4 * 256);
        auint *lut = aes_t_tables.data();
        aes::RoundTableRowZero(lut);
        for(asizei i = 0; i < 256; i++) lut[1 * 256 + i] = _rotl(lut[i],  8);
        for(asizei i = 0; i < 256; i++) lut[2 * 256 + i] = _rotl(lut[i], 16);
        for(asizei i = 0; i < 256; i++) lut[3 * 256 + i] = _rotl(lut[i], 24);
    }
    void Init_simd_alpha() {
        /* The ALPHA table contains (41^n) % 257, with n [0..255]. Due to large powers, you might thing this is a huge mess but it really isn't
        due to modulo properties. More information can be found in SIMD documentation from Ecole Normale Superieure, webpage of Gaetan Laurent,
        you need to look for the "Full Submission Package", will end up with a file SIMD.zip, containing reference.c which explains what to do at LN121.
        Anyway, the results of the above operations are MOSTLY 8-bit numbers. There's an exception however: alphaValue[128] is 0x0100.
        I cut it easy and make everything a short. */
        simd_alpha.resize(256);
        const int base = 41;
        int power = 1; // base^n
        for(int loop = 0; loop < 256; loop++) {
            simd_alpha[loop] = ashort(power);
            power = (power * base) % 257;
        }
    }
    void Init_simd_beta() {
        // The BETA table is very similar to ALPHA. It is built in two steps. In the first, it is basically an alpha table with a different base...
        simd_beta.resize(256);
        int base = 163;  // according to documentation, this should be "alpha^127 (respectively alpha^255 for SIMD-256)" which is not 0xA3 to me but I don't really care.
        int power = 1; // base^n
        for(int loop = 0; loop < 256; loop++) {
            simd_beta[loop] = static_cast<aushort>(power);
            power = (power * base) % 257;
        }
        // Now reference implementation mangles it again adding the powers of 40^n,
        // but only in the "final" message expansion. So we need to do nothing more.
        // For some reason the beta value table is called "yoff_b_n" in legacy kernels by lib-SPH...
    }

public:
    std::pair<const aubyte*, asizei> GetPrecomputedConstant(CryptoConstant what) {
        const aubyte *data = nullptr;
        asizei footprint = 0;
        switch(what) {
        case CryptoConstant::AES_T:
            if(aes_t_tables.size() == 0) Init_aes_t_tables();
            data = reinterpret_cast<const aubyte*>(aes_t_tables.data());
            footprint = sizeof(auint) * aes_t_tables.size();
            break;
        case CryptoConstant::SIMD_alpha:
            if(simd_alpha.size() == 0) Init_simd_alpha();
            data = reinterpret_cast<const aubyte*>(simd_alpha.data());
            footprint = sizeof(ashort) * simd_alpha.size();
            break;
        case CryptoConstant::SIMD_beta:
            if(simd_beta.size() == 0) Init_simd_beta();
            data = reinterpret_cast<const aubyte*>(simd_beta.data());
            footprint = sizeof(aushort) * simd_beta.size();
            break;
        }
        if(!data) throw "Unknown precomputed constant requested.";
        return std::make_pair(data, footprint);
    }
    std::pair<const aubyte*, asizei> operator[](CryptoConstant what) { return GetPrecomputedConstant(what); }
};
