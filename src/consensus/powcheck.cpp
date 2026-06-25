// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow.h>

#include <arith_uint256.h>
#include <uint256.h>

#include <algorithm>

bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    /** Maximum allowed target for this network (easiest valid difficulty permitted). */
    arith_uint256 bnPowCeiling = UintToArith256(params.powLimit);
    if (params.nPowBootstrapCompactTarget != 0) {
        arith_uint256 bnBootCeiling = UintToArith256(params.powLimitBootstrap);
        bnPowCeiling = std::max(bnPowCeiling, bnBootCeiling);
    }

    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > bnPowCeiling)
        return false;

    if (UintToArith256(hash) > bnTarget)
        return false;

    return true;
}
