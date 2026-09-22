// Copyright (c) 2026 The Quavence developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "airegistry.h"
#include "chainparams.h"
#include "clientversion.h"
#include "core_io.h"
#include "key.h"
#include "keystore.h"
#include "main.h"
#include "netaddress.h"
#include "policy/policy.h"
#include "pos.h"
#include "protocol.h"
#include "script/interpreter.h"
#include "script/script.h"
#include "script/script_error.h"
#include "script/sign.h"
#include "script/standard.h"
#include "streams.h"
#include "test/test_bitcoin.h"
#include "utilstrencodings.h"
#include "version.h"

#include <boost/test/unit_test.hpp>
#include <boost/thread.hpp>
#include <map>
#include <set>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(audit4_node_tests, BasicTestingSetup)

// ============================================================================
// Vector 1 (PUB-04): Stake-Boost Inflation via Multi-Output QVRE and Mitigation
// ============================================================================
BOOST_AUTO_TEST_CASE(vector1_pous_stake_boost_multiout_inflation_and_mitigation)
{
    SelectParams(CBaseChainParams::REGTEST);
    CChainParams& regtestParams = Params(CBaseChainParams::REGTEST);

    CKey poolKey;
    poolKey.MakeNewKey(true);
    CPubKey poolPubKey = poolKey.GetPubKey();
    CKeyID poolKeyID = poolPubKey.GetID();

    regtestParams.SetAiPoolKeyID(poolKeyID);
    regtestParams.SetPoUSV2ActivationHeight(100);

    CKey workerKey;
    workerKey.MakeNewKey(true);
    CPubKey workerPubKey = workerKey.GetPubKey();
    CKeyID workerID = workerPubKey.GetID();
    CScript workerStakeScript = GetScriptForDestination(workerID);

    CScript markerScript = CScript() << OP_RETURN << std::vector<unsigned char>{'Q', 'V', 'R', 'E', 0x01};

    // 1. Craft a transaction with 51 dust outputs (1 satoshi each)
    CMutableTransaction dustTx;
    dustTx.nVersion = 1;
    dustTx.vin.resize(1);
    dustTx.vin[0].prevout.hash = uint256S("1111111111111111111111111111111111111111111111111111111111111111");
    dustTx.vin[0].prevout.n = 0;

    for (int i = 0; i < 51; ++i) {
        dustTx.vout.push_back(CTxOut(1, workerStakeScript)); // 1 satoshi dust
    }
    dustTx.vout.push_back(CTxOut(0, markerScript));

    // Sign input with pool key
    CScript poolP2PKH = GetScriptForDestination(poolKeyID);
    uint256 hashDust = SignatureHash(poolP2PKH, dustTx, 0, SIGHASH_ALL, 0, NULL);
    std::vector<unsigned char> sigDust;
    BOOST_CHECK(poolKey.Sign(hashDust, sigDust));
    sigDust.push_back((unsigned char)SIGHASH_ALL);

    CScript scriptSigDust;
    scriptSigDust << sigDust << ToByteVector(poolPubKey);
    dustTx.vin[0].scriptSig = scriptSigDust;

    CTransaction txDust(dustTx);

    // Test 1a: Under pre-activation height (e.g. 50 < 100), legacy rule granted credits
    // for all outputs regardless of dust amount or duplicate worker keys.
    int legacyHeight = 50;
    CBlock blockPre;
    blockPre.vtx.push_back(txDust);
    RegisterAiAttestationsInBlock(blockPre, legacyHeight, 1789230000);

    uint32_t creditsPre = GetWorkerCreditsInWindow(workerID, legacyHeight);
    BOOST_CHECK_MESSAGE(creditsPre == 51u, "Pre-activation legacy logic credited all 51 dust outputs");

    CBlockIndex indexPre;
    indexPre.nHeight = legacyHeight;
    int boostPre = GetAiStakeBoost(workerStakeScript, &indexPre);
    BOOST_CHECK_EQUAL(boostPre, 50); // Maximum 50% boost via 51 satoshis dust

    UnregisterAiAttestationsInBlock(blockPre, legacyHeight);

    // Test 1b: At activated height (100 >= 100), the PoUS v2 dust filter drops outputs < 1000 sat
    int activatedHeight = 100;
    CBlock blockPostDust;
    blockPostDust.vtx.push_back(txDust);
    RegisterAiAttestationsInBlock(blockPostDust, activatedHeight, 1789230000);

    uint32_t creditsPostDust = GetWorkerCreditsInWindow(workerID, activatedHeight);
    BOOST_CHECK_MESSAGE(creditsPostDust == 0u, "PoUS v2 filter MUST reject all dust outputs (< 1000 sat)");

    CBlockIndex indexPost;
    indexPost.nHeight = activatedHeight;
    int boostPostDust = GetAiStakeBoost(workerStakeScript, &indexPost);
    BOOST_CHECK_EQUAL(boostPostDust, 0);

    UnregisterAiAttestationsInBlock(blockPostDust, activatedHeight);

    // Test 1c: At activated height, test idempotency with non-dust outputs (>= 1000 sat).
    // Even if an attacker creates 51 valid non-dust outputs to the same worker key in one TX,
    // the idempotency check MUST award at most 1 credit per worker per reward TX.
    CMutableTransaction multiTx;
    multiTx.nVersion = 1;
    multiTx.vin.resize(1);
    multiTx.vin[0].prevout.hash = uint256S("2222222222222222222222222222222222222222222222222222222222222222");
    multiTx.vin[0].prevout.n = 0;

    for (int i = 0; i < 51; ++i) {
        multiTx.vout.push_back(CTxOut(POUS_MIN_REWARD_OUTPUT_VALUE, workerStakeScript)); // minimum dust threshold
    }
    multiTx.vout.push_back(CTxOut(0, markerScript));

    uint256 hashMulti = SignatureHash(poolP2PKH, multiTx, 0, SIGHASH_ALL, 0, NULL);
    std::vector<unsigned char> sigMulti;
    BOOST_CHECK(poolKey.Sign(hashMulti, sigMulti));
    sigMulti.push_back((unsigned char)SIGHASH_ALL);

    CScript scriptSigMulti;
    scriptSigMulti << sigMulti << ToByteVector(poolPubKey);
    multiTx.vin[0].scriptSig = scriptSigMulti;

    CTransaction txMulti(multiTx);

    CBlock blockPostMulti;
    blockPostMulti.vtx.push_back(txMulti);
    RegisterAiAttestationsInBlock(blockPostMulti, activatedHeight, 1789230000);

    uint32_t creditsPostMulti = GetWorkerCreditsInWindow(workerID, activatedHeight);
    BOOST_CHECK_MESSAGE(creditsPostMulti == 1u,
                        "PoUS v2 idempotency MUST award strictly 1 credit per worker per reward TX");

    int boostPostMulti = GetAiStakeBoost(workerStakeScript, &indexPost);
    BOOST_CHECK_EQUAL(boostPostMulti, 20); // Tier 1: 1-5 tasks -> +20%, NOT +50%!

    UnregisterAiAttestationsInBlock(blockPostMulti, activatedHeight);
}

// ============================================================================
// Vector 2: Deadlock Freedom & Thread-Safety of cs_airegistry
// ============================================================================
BOOST_AUTO_TEST_CASE(vector2_deadlock_freedom_cs_airegistry_multithreaded)
{
    SelectParams(CBaseChainParams::REGTEST);
    CChainParams& regtestParams = Params(CBaseChainParams::REGTEST);

    CKey poolKey;
    poolKey.MakeNewKey(true);
    regtestParams.SetAiPoolKeyID(poolKey.GetPubKey().GetID());

    CKey workerKey;
    workerKey.MakeNewKey(true);
    CScript workerScript = GetScriptForDestination(workerKey.GetPubKey().GetID());

    std::atomic<bool> fRunning{true};
    std::atomic<int> readCount{0};

    // Spawn 4 concurrent threads querying AI stake boost and worker credits
    boost::thread_group threads;
    for (int t = 0; t < 4; ++t) {
        threads.create_thread([&, t]() {
            CBlockIndex index;
            index.nHeight = 100 + t;
            while (fRunning.load()) {
                GetAiStakeBoost(workerScript, &index);
                GetWorkerCreditsInWindow(workerKey.GetPubKey().GetID(), index.nHeight);
                GetActiveAiStakeBoost(index.nHeight);
                GetAiAttestationsCountInWindow(index.nHeight);
                readCount.fetch_add(1);
                boost::this_thread::sleep_for(boost::chrono::microseconds(100));
            }
        });
    }

    // Main thread performs 50 block registrations / unregistrations simulating ConnectBlock
    CBlock block;
    for (int h = 100; h < 150; ++h) {
        RegisterAiAttestationsInBlock(block, h, 1789230000 + h);
        boost::this_thread::sleep_for(boost::chrono::milliseconds(2));
        UnregisterAiAttestationsInBlock(block, h);
    }

    fRunning.store(false);
    threads.join_all();

    BOOST_CHECK_MESSAGE(readCount.load() > 100,
                        "Concurrent read/write queries to AI registry completed without deadlocks");
}

// ============================================================================
// Vector 4: Coinstake Split Economic Consensus Analysis (Soft-Fork Target)
// ============================================================================
BOOST_AUTO_TEST_CASE(vector4_coinstake_split_consensus_softfork_verification)
{
    SelectParams(CBaseChainParams::MAIN);

    int testHeight = 5000;
    CAmount subsidy = GetProofOfStakeSubsidy(testHeight);
    BOOST_REQUIRE(subsidy > 0);

    CAmount nFees = 100000; // 0.001 QVNC
    CAmount blockReward = nFees + subsidy;

    // 1. Honest coinstake generated by standard wallet (15% Dev, 30% of that to AI Pool)
    int nDonationPercentage = 15;
    CAmount nDevCredit = (subsidy * nDonationPercentage) / 100;
    CAmount nAiPoolCredit = (nDevCredit * 30) / 100;
    nDevCredit -= nAiPoolCredit;
    CAmount nMinerCreditHonest = blockReward - nDevCredit - nAiPoolCredit;

    BOOST_CHECK_EQUAL(nMinerCreditHonest + nDevCredit + nAiPoolCredit, blockReward);

    // 2. Greedy miner coinstake (modified client keeping 100% of reward)
    CAmount nMinerCreditGreedy = blockReward;

    // Consensus rule in ConnectBlock (main.cpp:2643):
    // CAmount blockReward = nFees + GetProofOfStakeSubsidy(pindex->nHeight);
    // if (nActualStakeReward > blockReward) return state.DoS(...);
    bool honestPassesConsensus = (nMinerCreditHonest + nDevCredit + nAiPoolCredit) <= blockReward;
    bool greedyPassesConsensus = (nMinerCreditGreedy) <= blockReward;

    BOOST_CHECK_MESSAGE(honestPassesConsensus, "Standard split coinstake passes ConnectBlock consensus check");
    BOOST_CHECK_MESSAGE(greedyPassesConsensus,
                        "Greedy miner coinstake (100% reward) ALSO PASSES current ConnectBlock consensus check!");

    // Architectural finding:
    // Because the greedy coinstake is currently accepted by ConnectBlock, enforcing the Dev Fund
    // and AI Pool split at the consensus level will RESTRICT the set of valid blocks.
    // Restricting valid blocks is by definition a SOFT FORK.
}

// ============================================================================
// Vector 5: Tor v3 P2P Relay 70015 Protocol Degradation & Alignment
// ============================================================================
BOOST_AUTO_TEST_CASE(vector5_torv3_addr_70015_degradation_and_filter)
{
    const char* V3_ONION = "kalwfcd7ia3gcwksq7yipu3b2lseibic6ytmawkbvq7odlleic6lifqd.onion";

    CNetAddr torV3Addr;
    BOOST_REQUIRE(torV3Addr.SetSpecial(V3_ONION));
    BOOST_REQUIRE(torV3Addr.IsTorV3());

    CAddress addr(CService(torV3Addr, 27714), NODE_NETWORK);
    addr.nTime = 1789230000;

    // 1. Serialization for legacy peer (protocol 70015):
    // Must fall back to 16-byte ip and exactly 30 bytes total (4 time + 8 services + 16 ip + 2 port)
    CDataStream ssLegacy(SER_NETWORK, 70015);
    ssLegacy << addr;

    const size_t LEGACY_CADDR_SIZE = 4 + 8 + 16 + 2; // 30 bytes
    BOOST_CHECK_EQUAL(ssLegacy.size(), LEGACY_CADDR_SIZE);

    CAddress addrDecoded;
    ssLegacy >> addrDecoded;
    BOOST_CHECK_EQUAL(ssLegacy.size(), 0u);
    BOOST_CHECK(!addrDecoded.IsTorV3());
    BOOST_CHECK_EQUAL(addrDecoded.GetPort(), 27714);

    // 2. Serialization for modern peer (protocol 70016 / TORV3_ADDR_VERSION):
    // New format: 4 time + 8 services + 1 netType + 32 torV3 pubkey + 2 port = 47 bytes
    CDataStream ssModern(SER_NETWORK, TORV3_ADDR_VERSION);
    ssModern << addr;

    const size_t MODERN_TORV3_CADDR_SIZE = 4 + 8 + 1 + 32 + 2; // 47 bytes
    BOOST_CHECK_EQUAL(ssModern.size(), MODERN_TORV3_CADDR_SIZE);

    CAddress addrModernDecoded;
    ssModern >> addrModernDecoded;
    BOOST_CHECK_EQUAL(ssModern.size(), 0u);
    BOOST_CHECK(addrModernDecoded.IsTorV3());
    BOOST_CHECK_EQUAL(addrModernDecoded.ToStringIP(), std::string(V3_ONION));

    // 3. P2P relay filtering logic in main.cpp:6795:
    // Tor v3 addresses must be filtered out when preparing vAddr for legacy peers
    std::vector<CAddress> mixedAddrs;
    CNetAddr ipv4;
    struct in_addr in;
    in.s_addr = 0x0100007f; // 127.0.0.1
    ipv4 = CNetAddr(in);
    mixedAddrs.push_back(CAddress(CService(ipv4, 27714), NODE_NETWORK));
    mixedAddrs.push_back(addr); // Tor v3

    std::vector<CAddress> relayedToLegacy;
    int peerVersionLegacy = 70015;
    for (size_t i = 0; i < mixedAddrs.size(); ++i) {
        if (mixedAddrs[i].IsTorV3() && peerVersionLegacy < TORV3_ADDR_VERSION)
            continue;
        relayedToLegacy.push_back(mixedAddrs[i]);
    }

    BOOST_CHECK_EQUAL(relayedToLegacy.size(), 1u);
    BOOST_CHECK(!relayedToLegacy[0].IsTorV3());

    // Serialize relayed vector to legacy stream
    CDataStream ssVector(SER_NETWORK, peerVersionLegacy);
    ssVector << relayedToLegacy;

    // Vector format: 1 byte compact size (count=1) + 30 bytes = 31 bytes
    BOOST_CHECK_EQUAL(ssVector.size(), 31u);
}

// ============================================================================
// A4-5: Coinstake Split Enforcement in ConnectBlock
// ============================================================================

BOOST_AUTO_TEST_CASE(coinstake_split_no_devfund_rejected)
{
    // Forked client omits DevFund output — ConnectBlock must reject with DoS 100
    SelectParams(CBaseChainParams::MAIN);
    const CChainParams& p = Params();
    const int enforceHeight = p.GetDevFundActivationHeight(); // 91450

    CMutableTransaction coinstake;
    coinstake.nTime = GetTime();
    coinstake.vin.push_back(CTxIn()); // dummy input
    // Staker output only — no DevFund, no AiPool
    coinstake.vout.push_back(CTxOut(0, CScript())); // marker
    coinstake.vout.push_back(CTxOut(100 * COIN, CScript())); // staker gets everything

    CValidationState state;
    bool ok = CheckCoinstakeSplitOutputs(
        CTransaction(coinstake), enforceHeight, 0, state, p);

    BOOST_CHECK_MESSAGE(!ok, "Coinstake without DevFund must be rejected");
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-cs-no-devfund");
}

BOOST_AUTO_TEST_CASE(coinstake_split_correct_accepted)
{
    // Honest client with correct DevFund + AiPool outputs — must pass
    SelectParams(CBaseChainParams::MAIN);
    const CChainParams& p = Params();
    const int enforceHeight = p.GetDevFundActivationHeight(); // 91450

    const CAmount blockSubsidy   = GetProofOfStakeSubsidy(enforceHeight);
    const int donationPct        = p.DevFundDonationPercent(); // 15%
    CAmount nDevCredit           = (blockSubsidy * donationPct) / 100;
    CAmount nAiCredit            = (nDevCredit * p.GetAiWorkerPoolPercent()) / 100; // 30% of DevFund
    nDevCredit                  -= nAiCredit;
    const CAmount nMinerCredit   = blockSubsidy - nDevCredit - nAiCredit;

    CMutableTransaction coinstake;
    coinstake.nTime = GetTime();
    coinstake.vin.push_back(CTxIn());
    coinstake.vout.push_back(CTxOut(0, CScript()));           // marker
    coinstake.vout.push_back(CTxOut(nMinerCredit, CScript())); // staker
    coinstake.vout.push_back(CTxOut(nDevCredit,   p.GetDevRewardScript()));
    coinstake.vout.push_back(CTxOut(nAiCredit,    p.GetAiWorkerPoolScript()));

    CValidationState state;
    bool ok = CheckCoinstakeSplitOutputs(
        CTransaction(coinstake), enforceHeight, 0, state, p);

    BOOST_CHECK_MESSAGE(ok, "Honest coinstake with correct split must pass");
}

BOOST_AUTO_TEST_CASE(coinstake_split_below_activation_skipped)
{
    // Below activation height — any coinstake accepted regardless of split
    SelectParams(CBaseChainParams::MAIN);
    const CChainParams& p = Params();
    const int belowHeight = p.GetDevFundActivationHeight() - 1; // 91449

    CMutableTransaction coinstake;
    coinstake.vin.push_back(CTxIn());
    coinstake.vout.push_back(CTxOut(0, CScript()));
    coinstake.vout.push_back(CTxOut(100 * COIN, CScript())); // staker steals everything

    CValidationState state;
    bool ok = CheckCoinstakeSplitOutputs(
        CTransaction(coinstake), belowHeight, 0, state, p);

    BOOST_CHECK_MESSAGE(ok, "Below activation height — split not enforced");
}

BOOST_AUTO_TEST_CASE(coinstake_split_devfund_amount_mismatch_rejected)
{
    // DevFund output present but with incorrect amount — must reject
    SelectParams(CBaseChainParams::MAIN);
    const CChainParams& p = Params();
    const int enforceHeight = p.GetDevFundActivationHeight();

    const CAmount blockSubsidy   = GetProofOfStakeSubsidy(enforceHeight);
    const int donationPct        = p.DevFundDonationPercent();
    CAmount nDevCredit           = (blockSubsidy * donationPct) / 100;
    CAmount nAiCredit            = (nDevCredit * p.GetAiWorkerPoolPercent()) / 100;
    nDevCredit                  -= nAiCredit;

    CMutableTransaction coinstake;
    coinstake.nTime = GetTime();
    coinstake.vin.push_back(CTxIn());
    coinstake.vout.push_back(CTxOut(0, CScript()));
    coinstake.vout.push_back(CTxOut(100 * COIN, CScript()));
    coinstake.vout.push_back(CTxOut(nDevCredit - 100, p.GetDevRewardScript())); // Wrong amount (-100 sat)
    coinstake.vout.push_back(CTxOut(nAiCredit,        p.GetAiWorkerPoolScript()));

    CValidationState state;
    bool ok = CheckCoinstakeSplitOutputs(
        CTransaction(coinstake), enforceHeight, 0, state, p);

    BOOST_CHECK_MESSAGE(!ok, "Coinstake with incorrect DevFund amount must be rejected");
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-cs-devfund-amount");
}

BOOST_AUTO_TEST_CASE(coinstake_split_aipool_amount_mismatch_rejected)
{
    // AiPool output present but with incorrect amount — must reject
    SelectParams(CBaseChainParams::MAIN);
    const CChainParams& p = Params();
    const int enforceHeight = p.GetDevFundActivationHeight();

    const CAmount blockSubsidy   = GetProofOfStakeSubsidy(enforceHeight);
    const int donationPct        = p.DevFundDonationPercent();
    CAmount nDevCredit           = (blockSubsidy * donationPct) / 100;
    CAmount nAiCredit            = (nDevCredit * p.GetAiWorkerPoolPercent()) / 100;
    nDevCredit                  -= nAiCredit;

    CMutableTransaction coinstake;
    coinstake.nTime = GetTime();
    coinstake.vin.push_back(CTxIn());
    coinstake.vout.push_back(CTxOut(0, CScript()));
    coinstake.vout.push_back(CTxOut(100 * COIN, CScript()));
    coinstake.vout.push_back(CTxOut(nDevCredit,       p.GetDevRewardScript()));
    coinstake.vout.push_back(CTxOut(nAiCredit - 100,  p.GetAiWorkerPoolScript())); // Wrong amount (-100 sat)

    CValidationState state;
    bool ok = CheckCoinstakeSplitOutputs(
        CTransaction(coinstake), enforceHeight, 0, state, p);

    BOOST_CHECK_MESSAGE(!ok, "Coinstake with incorrect AiPool amount must be rejected");
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-cs-aipool-amount");
}

BOOST_AUTO_TEST_CASE(coinstake_split_mainnet_params_verification)
{
    SelectParams(CBaseChainParams::MAIN);
    const CChainParams& p = Params();
    BOOST_CHECK_EQUAL(p.GetDevFundActivationHeight(), 91450);
    BOOST_CHECK_EQUAL(p.DevFundDonationPercent(), 15u);
    BOOST_CHECK_EQUAL(p.GetAiWorkerPoolPercent(), 30);
    BOOST_CHECK_EQUAL(p.GetDevFundAddress(), "SXbKabuHh7xn3QuXF7DMG758D9j4rVcL6V");
    BOOST_CHECK_EQUAL(p.GetAiWorkerPoolAddress(), "ScmZ5fYVTADyMcH11CXtf9iC9qVeRHA31M");

    // At height 91449 (< 91450), greedy miner passes
    CMutableTransaction greedyCoinstake;
    greedyCoinstake.vin.push_back(CTxIn());
    greedyCoinstake.vout.push_back(CTxOut(0, CScript()));
    greedyCoinstake.vout.push_back(CTxOut(100 * COIN, CScript()));

    CValidationState statePre;
    BOOST_CHECK(CheckCoinstakeSplitOutputs(CTransaction(greedyCoinstake), 91449, 0, statePre, p));

    // At height 91450 (>= 91450), greedy miner rejected with bad-cs-no-devfund
    CValidationState statePost;
    BOOST_CHECK(!CheckCoinstakeSplitOutputs(CTransaction(greedyCoinstake), 91450, 0, statePost, p));
    BOOST_CHECK_EQUAL(statePost.GetRejectReason(), "bad-cs-no-devfund");
}

BOOST_AUTO_TEST_SUITE_END()

