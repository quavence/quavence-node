#include "arith_uint256.h"
// Copyright (c) 2026 The Quavence developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "airegistry.h"
#include "chainparams.h"
#include "core_io.h"
#include "key.h"
#include "keystore.h"
#include "main.h"
#include "policy/policy.h"
#include "script/script.h"
#include "script/script_error.h"
#include "script/sign.h"
#include "script/standard.h"
#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(pous_auth_bypass_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(pous_authorization_bypass_regression_test)
{
    // Configure stand-in pool authority key in regtest params
    SelectParams(CBaseChainParams::REGTEST);
    CChainParams& regtestParams = Params(CBaseChainParams::REGTEST);

    CKey poolKey;
    poolKey.MakeNewKey(true);
    CPubKey poolPubKey = poolKey.GetPubKey();
    CKeyID poolKeyID = poolPubKey.GetID();

    regtestParams.SetAiPoolKeyID(poolKeyID);
    regtestParams.SetPoUSV2ActivationHeight(100);

    // 1. Attacker sets up an OP_2DROP OP_1 P2SH script
    // This script is spendable by anyone without needing any signature.
    CScript redeemScript = CScript() << OP_2DROP << OP_1;
    CScript p2shScriptPubKey = GetScriptForDestination(CScriptID(redeemScript));

    // Attacker generates their own staking key to receive the illicit boost
    CKey attackerKey;
    attackerKey.MakeNewKey(true);
    CPubKey attackerPubKey = attackerKey.GetPubKey();
    CScript attackerStakeScript = GetScriptForDestination(attackerPubKey.GetID());

    // 2. Attacker crafts forged transaction spending the P2SH output
    // scriptSig pushes: <dummy_sig> <poolPubKey> <redeemScript>
    // In legacy code, TxSpendsFromKeyID saw poolPubKey as the second push and authorized it!
    CMutableTransaction forgedTx;
    forgedTx.nVersion = 1;
    forgedTx.vin.resize(1);
    forgedTx.vin[0].prevout.hash = uint256S("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
    forgedTx.vin[0].prevout.n = 0;

    std::vector<unsigned char> dummySig = {0x30, 0x06, 0x02, 0x01, 0x01, 0x02, 0x01, 0x01, 0x01}; // dummy bytes
    CScript scriptSig;
    scriptSig << dummySig
              << ToByteVector(poolPubKey)
              << ToByteVector(redeemScript);
    forgedTx.vin[0].scriptSig = scriptSig;

    // Add 51 outputs paying to attacker's staking key to qualify for top boost tier (+50%)
    for (int i = 0; i < 51; ++i) {
        forgedTx.vout.push_back(CTxOut(1000 * COIN, attackerStakeScript));
    }
    // Add QVRE v1 reward marker: OP_RETURN 'Q' 'V' 'R' 'E' 0x01
    CScript markerScript = CScript() << OP_RETURN << std::vector<unsigned char>{'Q', 'V', 'R', 'E', 0x01};
    forgedTx.vout.push_back(CTxOut(0, markerScript));

    CTransaction forged(forgedTx);

    // Assertion 1: P2SH script verification passes without pool signature
    BaseSignatureChecker dummyChecker;
    ScriptError serror = SCRIPT_ERR_OK;
    bool verifyOk = VerifyScript(forged.vin[0].scriptSig, p2shScriptPubKey,
                                 SCRIPT_VERIFY_P2SH, dummyChecker, &serror);
    BOOST_CHECK(verifyOk);
    BOOST_CHECK_EQUAL(serror, SCRIPT_ERR_OK);

    // Assertion 2: Transaction is standard
    std::string reason;
    BOOST_CHECK(IsStandardTx(forged, reason));

    // Assertion 3 & 4: Under legacy height (< activation height 100), the old vulnerability permitted it
    int legacyHeight = 50;
    BOOST_CHECK_MESSAGE(IsAuthorizedAiPoolTx(forged, legacyHeight),
                        "Legacy height should permit pre-activation historical blocks");
    BOOST_CHECK_MESSAGE(IsValidPoUSRewardTx(forged, legacyHeight),
                        "Legacy height treats forged reward as valid under old rules");

    // Assertion 5 & 6: Under activated height (>= activation height 100) or mempool (-1),
    // the forged transaction MUST be rejected cryptographically!
    int activatedHeight = 100;
    BOOST_CHECK_MESSAGE(!IsAuthorizedAiPoolTx(forged, activatedHeight),
                        "Activated height MUST reject forged pool authorization");
    BOOST_CHECK_MESSAGE(!IsValidPoUSRewardTx(forged, activatedHeight),
                        "Activated height MUST reject forged reward marker");
    BOOST_CHECK_MESSAGE(!IsAuthorizedAiPoolTx(forged, -1),
                        "Mempool / unspecified height MUST reject forged pool authorization");
    BOOST_CHECK_MESSAGE(!IsValidPoUSRewardTx(forged, -1),
                        "Mempool / unspecified height MUST reject forged reward marker");

    // Also test full-length (>= 70 bytes) invalid DER signature to verify VerifyScript logic specifically
    CMutableTransaction forgedLongTx(forgedTx);
    std::vector<unsigned char> longDummySig(72, 0x01);
    longDummySig[0] = 0x30; longDummySig[1] = 0x44;
    CScript longScriptSig;
    longScriptSig << longDummySig << ToByteVector(poolPubKey) << ToByteVector(redeemScript);
    forgedLongTx.vin[0].scriptSig = longScriptSig;
    CTransaction forgedLong(forgedLongTx);
    BOOST_CHECK_MESSAGE(!IsAuthorizedAiPoolTx(forgedLong, activatedHeight),
                        "Full-length invalid signature must be rejected by VerifyScript");
    BOOST_CHECK_MESSAGE(!IsValidPoUSRewardTx(forgedLong, activatedHeight),
                        "Full-length invalid reward must be rejected by VerifyScript");

    // Assertion 7: If forged transaction is processed in a block at activated height,
    // attacker MUST NOT receive any worker credits or stake boost!
    CBlock block;
    block.vtx.push_back(forged);
    RegisterAiAttestationsInBlock(block, activatedHeight, 1789230000);

    CBlockIndex indexPrev;
    indexPrev.nHeight = activatedHeight;
    int attackerBoost = GetAiStakeBoost(attackerStakeScript, &indexPrev);
    BOOST_CHECK_EQUAL(attackerBoost, 0);

    UnregisterAiAttestationsInBlock(block, activatedHeight);

    // Assertion 8: Genuine authorized transactions WITH REAL SIGNATURE succeed at activated height.
    // Under PoUS v2 (PUB-04 mitigation), each reward transaction awards at most 1 credit per worker.
    // 51 legitimate reward transactions award 51 credits, achieving the top boost tier (+50%).
    CBlock genuineBlock;
    CScript poolP2PKH = GetScriptForDestination(poolKeyID);

    for (int i = 0; i < 51; ++i) {
        CMutableTransaction genuineTx;
        genuineTx.nVersion = 1;
        genuineTx.vin.resize(1);
        genuineTx.vin[0].prevout.hash = ArithToUint256(arith_uint256(0xfedcba98) + i);
        genuineTx.vin[0].prevout.n = 0;
        genuineTx.vout.push_back(CTxOut(1000 * COIN, attackerStakeScript));
        genuineTx.vout.push_back(CTxOut(0, markerScript));

        uint256 hash = SignatureHash(poolP2PKH, genuineTx, 0, SIGHASH_ALL, 0, NULL);
        std::vector<unsigned char> genuineSig;
        BOOST_CHECK(poolKey.Sign(hash, genuineSig));
        genuineSig.push_back((unsigned char)SIGHASH_ALL);

        CScript genuineScriptSig;
        genuineScriptSig << genuineSig << ToByteVector(poolPubKey);
        genuineTx.vin[0].scriptSig = genuineScriptSig;

        CTransaction genuine(genuineTx);
        if (i == 0) {
            BOOST_CHECK_MESSAGE(IsAuthorizedAiPoolTx(genuine, activatedHeight),
                                "Genuine pool transaction with valid signature MUST be authorized");
            BOOST_CHECK_MESSAGE(IsValidPoUSRewardTx(genuine, activatedHeight),
                                "Genuine pool reward with valid signature MUST be valid");
        }
        genuineBlock.vtx.push_back(genuine);
    }

    RegisterAiAttestationsInBlock(genuineBlock, activatedHeight, 1789230000);

    int genuineWorkerBoost = GetAiStakeBoost(attackerStakeScript, &indexPrev);
    BOOST_CHECK_EQUAL(genuineWorkerBoost, 50);

    UnregisterAiAttestationsInBlock(genuineBlock, activatedHeight);
}

BOOST_AUTO_TEST_SUITE_END()
