// Copyright (c) 2015 The Bitcoin Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "arith_uint256.h"
#include "chain.h"
#include "chainparams.h"
#include "pow.h"
#include "random.h"
#include "util.h"
#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

using namespace std;

BOOST_FIXTURE_TEST_SUITE(pow_tests, BasicTestingSetup)

static unsigned int CalcNextWorkFixture(int64_t nLastRetargetTime, int height, int64_t time, unsigned bits)
{
    SelectParams(CBaseChainParams::MAIN);
    const Consensus::Params& params = Params().GetConsensus();
    CBlockIndex pindexLast;
    pindexLast.nHeight = height;
    pindexLast.nTime = time;
    pindexLast.nBits = bits;
    return CalculateNextTargetRequired(&pindexLast, nLastRetargetTime, params, false);
}

/* Test calculation of next difficulty target with no constraints applying */
BOOST_AUTO_TEST_CASE(get_next_work)
{
    const Consensus::Params& params = Params(CBaseChainParams::MAIN).GetConsensus();
    const arith_uint256 powLimit = UintToArith256(params.powLimit);
    const unsigned int next = CalcNextWorkFixture(1261130161, 32255, 1262152739, 0x1d00ffff);
    arith_uint256 target;
    target.SetCompact(next);
    BOOST_CHECK(target > 0);
    BOOST_CHECK(target <= powLimit);
    BOOST_CHECK_EQUAL(next, CalcNextWorkFixture(1261130161, 32255, 1262152739, 0x1d00ffff));
}

/* Test the constraint on the upper bound for next work */
BOOST_AUTO_TEST_CASE(get_next_work_pow_limit)
{
    const Consensus::Params& params = Params(CBaseChainParams::MAIN).GetConsensus();
    const arith_uint256 powLimit = UintToArith256(params.powLimit);
    const unsigned int next = CalcNextWorkFixture(1231006505, 2015, 1233061996, 0x1d00ffff);
    arith_uint256 target;
    target.SetCompact(next);
    BOOST_CHECK(target > 0);
    BOOST_CHECK(target <= powLimit);
    BOOST_CHECK_EQUAL(next, CalcNextWorkFixture(1231006505, 2015, 1233061996, 0x1d00ffff));
}

/* Test the constraint on the lower bound for actual time taken */
BOOST_AUTO_TEST_CASE(get_next_work_lower_limit_actual)
{
    const Consensus::Params& params = Params(CBaseChainParams::MAIN).GetConsensus();
    const arith_uint256 powLimit = UintToArith256(params.powLimit);
    const unsigned int next = CalcNextWorkFixture(1279008237, 68543, 1279297671, 0x1c05a3f4);
    arith_uint256 target;
    target.SetCompact(next);
    BOOST_CHECK(target > 0);
    BOOST_CHECK(target <= powLimit);
    BOOST_CHECK_EQUAL(next, CalcNextWorkFixture(1279008237, 68543, 1279297671, 0x1c05a3f4));
}

/* Test the constraint on the upper bound for actual time taken */
BOOST_AUTO_TEST_CASE(get_next_work_upper_limit_actual)
{
    const Consensus::Params& params = Params(CBaseChainParams::MAIN).GetConsensus();
    const arith_uint256 powLimit = UintToArith256(params.powLimit);
    const unsigned int next = CalcNextWorkFixture(1263163443, 46367, 1269211443, 0x1c387f6f);
    arith_uint256 target;
    target.SetCompact(next);
    BOOST_CHECK(target > 0);
    BOOST_CHECK(target <= powLimit);
    BOOST_CHECK_EQUAL(next, CalcNextWorkFixture(1263163443, 46367, 1269211443, 0x1c387f6f));
}

BOOST_AUTO_TEST_CASE(GetBlockProofEquivalentTime_test)
{
    SelectParams(CBaseChainParams::MAIN);
    const Consensus::Params& params = Params().GetConsensus();

    std::vector<CBlockIndex> blocks(10000);
    for (int i = 0; i < 10000; i++) {
        blocks[i].pprev = i ? &blocks[i - 1] : NULL;
        blocks[i].nHeight = i;
        blocks[i].nTime = 1269211443 + i * params.nTargetSpacing;
        blocks[i].nBits = 0x207fffff; /* target 0x7fffff000... */
        blocks[i].nChainWork = i ? blocks[i - 1].nChainWork + GetBlockProof(blocks[i - 1]) : arith_uint256(0);
    }

    for (int j = 0; j < 1000; j++) {
        CBlockIndex *p1 = &blocks[GetRand(10000)];
        CBlockIndex *p2 = &blocks[GetRand(10000)];
        CBlockIndex *p3 = &blocks[GetRand(10000)];

        int64_t tdiff = GetBlockProofEquivalentTime(*p1, *p2, *p3, params);
        BOOST_CHECK_EQUAL(tdiff, p1->GetBlockTime() - p2->GetBlockTime());
    }
}

BOOST_AUTO_TEST_SUITE_END()
