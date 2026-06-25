// Copyright (c) 2026
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "consensus/subsidy_schedule.h"
#include "main.h"
#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(subsidy_schedule_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(subsidy_table_totals_to_pos_cap)
{
    int64_t total = 0;
    for (size_t i = 0; i < SUBSIDY_ERA_COUNT; ++i) {
        const SubsidyEraRow& row = SUBSIDY_ERAS[i];
        int64_t blocks = (int64_t)row.eraEndHeight - (int64_t)row.eraStartHeight + 1;
        BOOST_CHECK(blocks > 0);
        BOOST_CHECK(row.rewardPerBlock >= 0);
        total += blocks * row.rewardPerBlock;
    }
    BOOST_CHECK_EQUAL(total, SUBSIDY_POS_CAP_UNITS);
}

BOOST_AUTO_TEST_CASE(subsidy_table_contiguous_heights)
{
    for (size_t i = 0; i < SUBSIDY_ERA_COUNT; ++i) {
        const SubsidyEraRow& row = SUBSIDY_ERAS[i];
        BOOST_CHECK(row.eraStartHeight <= row.eraEndHeight);
        if (i > 0) {
            const SubsidyEraRow& prev = SUBSIDY_ERAS[i - 1];
            BOOST_CHECK_EQUAL(row.eraStartHeight, prev.eraEndHeight + 1);
        } else {
            BOOST_CHECK_EQUAL(row.eraStartHeight, SUBSIDY_FIRST_HEIGHT);
        }
    }
}

BOOST_AUTO_TEST_CASE(get_proof_of_stake_subsidy_boundaries)
{
    BOOST_CHECK_EQUAL(GetProofOfStakeSubsidy(0), 0);
    BOOST_CHECK_EQUAL(GetProofOfStakeSubsidy(SUBSIDY_FIRST_HEIGHT - 1), 0);
    BOOST_CHECK(GetProofOfStakeSubsidy(SUBSIDY_FIRST_HEIGHT) > 0);

    const SubsidyEraRow& last = SUBSIDY_ERAS[SUBSIDY_ERA_COUNT - 1];
    BOOST_CHECK(GetProofOfStakeSubsidy(last.eraEndHeight) > 0);
    BOOST_CHECK_EQUAL(GetProofOfStakeSubsidy(last.eraEndHeight + 1), 0);
}

BOOST_AUTO_TEST_SUITE_END()
