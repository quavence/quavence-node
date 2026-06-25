// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pow.h"

#include "arith_uint256.h"
#include "chain.h"
#include "primitives/block.h"
#include "uint256.h"
#include "util.h"
#include <stdio.h>

static arith_uint256 GetTargetLimit(int64_t nTime, const Consensus::Params& params, bool fProofOfStake)
{
    uint256 nLimit;

    if (fProofOfStake) {
        if (params.IsProtocolV2(nTime))
            nLimit = params.posLimitV2;
        else
            nLimit = params.posLimit;
    } else {
        nLimit = params.powLimit;
    }

    return UintToArith256(nLimit);
}

unsigned int GetNextTargetRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params, bool fProofOfStake)
{
    if (pindexLast == NULL) {
        /** Pre-chain templates; mainnet optionally uses bootstrap easy bits for genesis-era PoW helpers. */
        if (!fProofOfStake && params.nPowBootstrapCompactTarget != 0)
            return params.nPowBootstrapCompactTarget;
        return UintToArith256(params.powLimit).GetCompact();
    }

    const int nNextHeight = pindexLast->nHeight + 1;

    /** Fixed easy PoW difficulty for bootstrap window heights 1..nLastPOWBlock — keeps CPU mining predictable. PoS unaffected. */
    if (!fProofOfStake && params.nPowBootstrapCompactTarget != 0
        && nNextHeight >= 1 && nNextHeight <= params.nLastPOWBlock) {
        return params.nPowBootstrapCompactTarget;
    }

    const CBlockIndex* pindexPrev = GetLastBlockIndex(pindexLast, fProofOfStake);
    unsigned int nTargetLimit = GetTargetLimit(pindexLast->GetBlockTime(), params, fProofOfStake).GetCompact();

    if (pindexPrev->pprev == NULL)
        return nTargetLimit; // first block
    const CBlockIndex* pindexPrevPrev = GetLastBlockIndex(pindexPrev->pprev, fProofOfStake);
    if (pindexPrevPrev->pprev == NULL)
        return nTargetLimit; // second block

    return CalculateNextTargetRequired(pindexPrev, pindexPrevPrev->GetBlockTime(), params, fProofOfStake);
}

unsigned int CalculateNextTargetRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params, bool fProofOfStake)
{
    if (fProofOfStake) {
        if (params.fPoSNoRetargeting)
            return pindexLast->nBits;
    } else {
        if (params.fPowNoRetargeting)
            return pindexLast->nBits;
    }

    int64_t nTargetSpacing = params.IsProtocolV2(pindexLast->GetBlockTime()) ? params.nTargetSpacing : params.nTargetSpacingV1;
    int64_t nActualSpacing = pindexLast->GetBlockTime() - nFirstBlockTime;

    // Limit adjustment step
    if (params.IsProtocolV1RetargetingFixed(pindexLast->GetBlockTime()) && nActualSpacing < 0)
        nActualSpacing = nTargetSpacing;
    if (params.IsProtocolV3(pindexLast->GetBlockTime()) && nActualSpacing > nTargetSpacing*10)
        nActualSpacing = nTargetSpacing*10;

    // retarget with exponential moving toward target spacing
    const arith_uint256 bnTargetLimit = GetTargetLimit(pindexLast->GetBlockTime(), params, fProofOfStake);
    arith_uint256 bnNew;
    bnNew.SetCompact(pindexLast->nBits);
    int64_t nInterval = params.nTargetTimespan / nTargetSpacing;
    bnNew *= ((nInterval - 1) * nTargetSpacing + nActualSpacing + nActualSpacing);
    bnNew /= ((nInterval + 1) * nTargetSpacing);

    if (bnNew <= 0 || bnNew > bnTargetLimit)
        bnNew = bnTargetLimit;

    return bnNew.GetCompact();
}
