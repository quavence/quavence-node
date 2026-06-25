// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/validation.h>
#include <key.h>
#include <main.h>
#include <miner.h>
#include <pubkey.h>
#include <txmempool.h>
#include <random.h>
#include <script/standard.h>
#include <test/test_bitcoin.h>
#include <utiltime.h>

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(tx_validationcache_tests)

static bool
ToMemPool(CMutableTransaction& tx, std::string* rejectReason = nullptr)
{
    LOCK(cs_main);

    CValidationState state;
    bool missingInputs = false;
    const CTransaction spend(tx);
    const bool ok = AcceptToMemoryPool(mempool, state, spend, false, &missingInputs, true, 0);
    if (!ok && rejectReason) {
        if (missingInputs)
            *rejectReason = "missing-inputs";
        else if (!state.GetRejectReason().empty())
            *rejectReason = state.GetRejectReason();
        else
            *rejectReason = "accept-failed";
    }
    return ok;
}

BOOST_FIXTURE_TEST_CASE(tx_mempool_block_doublespend, TestChain100Setup)
{
    // Make sure skipping validation of transctions that were
    // validated going into the memory pool does not allow
    // double-spends in blocks to pass validation when they should not.

    CScript scriptPubKey = CScript() <<  ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
    // Mature coinbases (regtest maturity == initial chain length).
    CreateAndProcessBlock({}, scriptPubKey);

    BOOST_REQUIRE(!coinbaseTxns.empty());
    const CTransaction& fundCoinbase = coinbaseTxns[0];

    // Create a double-spend of mature coinbase txn:
    std::vector<CMutableTransaction> spends;
    spends.resize(2);
    const uint32_t spendTime = fundCoinbase.nTime;
    for (int i = 0; i < 2; i++)
    {
        spends[i].nVersion = 1;
        spends[i].nTime = spendTime;
        spends[i].nLockTime = 0;
        spends[i].vin.resize(1);
        spends[i].vin[0].prevout.hash = fundCoinbase.GetHash();
        spends[i].vin[0].prevout.n = 0;
        spends[i].vout.resize(1);
        spends[i].vout[0].nValue = 11*CENT;
        spends[i].vout[0].scriptPubKey = scriptPubKey;

        // Sign:
        std::vector<unsigned char> vchSig;
        uint256 hash = SignatureHash(scriptPubKey, spends[i], 0, SIGHASH_ALL, 0);
        BOOST_CHECK(coinbaseKey.Sign(hash, vchSig));
        vchSig.push_back((unsigned char)SIGHASH_ALL);
        spends[i].vin[0].scriptSig << vchSig;
    }

    CBlock block;

    // Test 1: block with both of those transactions should be rejected.
    block = CreateAndProcessBlock(spends, scriptPubKey, false);
    BOOST_CHECK(chainActive.Tip()->GetBlockHash() != block.GetHash());

    // Test 2: ... and should be rejected if spend1 is in the memory pool
    {
        std::string reason;
        BOOST_CHECK_MESSAGE(ToMemPool(spends[0], &reason), reason.c_str());
    }
    block = CreateAndProcessBlock(spends, scriptPubKey, false);
    BOOST_CHECK(chainActive.Tip()->GetBlockHash() != block.GetHash());
    mempool.clear();

    // Test 3: ... and should be rejected if spend2 is in the memory pool
    {
        std::string reason;
        BOOST_CHECK_MESSAGE(ToMemPool(spends[1], &reason), reason.c_str());
    }
    block = CreateAndProcessBlock(spends, scriptPubKey, false);
    BOOST_CHECK(chainActive.Tip()->GetBlockHash() != block.GetHash());
    mempool.clear();

    // Final sanity test: first spend in mempool, second in block, that's OK:
    std::vector<CMutableTransaction> oneSpend;
    oneSpend.push_back(spends[0]);
    {
        std::string reason;
        BOOST_CHECK_MESSAGE(ToMemPool(spends[1], &reason), reason.c_str());
    }
    block = CreateAndProcessBlock(oneSpend, scriptPubKey);
    BOOST_CHECK(chainActive.Tip()->GetBlockHash() == block.GetHash());
    // spends[1] should have been removed from the mempool when the
    // block with spends[0] is accepted:
    BOOST_CHECK_EQUAL(mempool.size(), 0);
}

BOOST_AUTO_TEST_SUITE_END()
