// Independent re-audit of the QV-2026-002 / QV-2026-003 fixes (v15.1.2).
#include "airegistry.h"
#include "chainparams.h"
#include "clientversion.h"
#include "key.h"
#include "addrman.h"
#include "base58.h"
#include "core_io.h"
#include "primitives/transaction.h"
#include "netaddress.h"
#include "protocol.h"
#include "script/interpreter.h"
#include "script/script.h"
#include "script/sign.h"
#include "script/standard.h"
#include "streams.h"
#include "test/test_bitcoin.h"
#include "version.h"

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(audit2_tests, BasicTestingSetup)

static const char* SEED =
    "kalwfcd7ia3gcwksq7yipu3b2lseibic6ytmawkbvq7odlleic6lifqd.onion";

// ---------------------------------------------------------------------------
// QV-2026-003: does the Tor v3 address now survive a round trip?
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(torv3_roundtrip_now_fixed)
{
    CNetAddr a;
    BOOST_REQUIRE(a.SetSpecial(SEED));
    BOOST_REQUIRE(a.IsTorV3());

    CAddress addr(CService(a, 27714), NODE_NETWORK);
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << addr;
    CAddress back;
    ss >> back;
    BOOST_TEST_MESSAGE("v70016 sent:     " + addr.ToStringIP());
    BOOST_TEST_MESSAGE("v70016 received: " + back.ToStringIP());
    BOOST_CHECK(back.IsTorV3());
    BOOST_CHECK_EQUAL(back.ToStringIP(), std::string(SEED));
    BOOST_CHECK(back == addr);
    BOOST_CHECK_EQUAL(back.GetPort(), 27714);
}

// Talking to a pre-fix peer (70015) must still be lossy-but-safe, not desynced.
BOOST_AUTO_TEST_CASE(torv3_legacy_peer_still_lossy)
{
    CNetAddr a;
    BOOST_REQUIRE(a.SetSpecial(SEED));
    CAddress addr(CService(a, 27714), NODE_NETWORK);
    CDataStream ss(SER_NETWORK, 70015);
    ss << addr;
    CAddress back;
    ss >> back;
    BOOST_TEST_MESSAGE("v70015 received: " + back.ToStringIP());
    BOOST_CHECK(!back.IsTorV3());               // expected: v3 key dropped for old peers
    BOOST_CHECK_EQUAL(back.GetPort(), 27714);   // but the stream must stay aligned
    BOOST_CHECK_EQUAL(ss.size(), 0u);           // nothing left over
}

// ---------------------------------------------------------------------------
// Upgrade path: can v15.1.2 still read a peers.dat written by v15.1.1?
// An old node wrote CAddress(SER_DISK) as:
//   int32 nVersion | uint32 nTime | uint64 nServices | 16-byte ip | uint16 port
// (no netType byte). CAddress re-reads nVersion FROM THE FILE, so the addrman
// migration hook (s.SetVersion(70015)) cannot influence this nested read.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(old_peersdat_entry_upgrade)
{
    const int OLD_CLIENT_VERSION = 150101;   // v15.1.1 wrote this into each record
    BOOST_TEST_MESSAGE("CLIENT_VERSION now = " + std::to_string(CLIENT_VERSION));
    BOOST_CHECK_MESSAGE(OLD_CLIENT_VERSION >= TORV3_ADDR_VERSION,
        "old record version is >= TORV3_ADDR_VERSION, so the new code takes the v3 path");

    // Hand-build exactly what the old binary wrote for 1.2.3.4:27714
    CDataStream old(SER_DISK, OLD_CLIENT_VERSION);
    old << (int32_t)OLD_CLIENT_VERSION;
    old << (uint32_t)1789000000;            // nTime
    old << (uint64_t)NODE_NETWORK;          // nServices
    unsigned char ip[16] = {0,0,0,0,0,0,0,0,0,0,0xff,0xff,1,2,3,4};
    old.write((const char*)ip, 16);
    unsigned short portN = htons(27714);
    old.write((const char*)&portN, 2);
    const size_t wrote = old.size();

    // Read it back with the new code, exactly as CAddrDB::Read would.
    CDataStream in(old.begin(), old.end(), SER_DISK, CLIENT_VERSION);
    CAddress got;
    bool threw = false;
    std::string what;
    try {
        in >> got;
    } catch (const std::exception& e) {
        threw = true; what = e.what();
    }
    BOOST_TEST_MESSAGE("old record bytes=" + std::to_string(wrote) +
                       " threw=" + std::to_string(threw) + " " + what);
    if (!threw) {
        BOOST_TEST_MESSAGE("parsed as: " + got.ToStringIP() +
                           " port=" + std::to_string(got.GetPort()) +
                           " leftover=" + std::to_string(in.size()));
    }
    // A clean upgrade would yield 1.2.3.4:27714 with no bytes left over.
    bool clean = (!threw && got.ToStringIP() == "1.2.3.4" && got.GetPort() == 27714 && in.size() == 0);
    BOOST_CHECK_MESSAGE(clean, "REGRESSION: v15.1.1 peers.dat record does not survive upgrade");
}

// banlist.dat: CSubNet serializes CNetAddr with the *stream* version and has no
// migration hook at all, so the same shift applies there.
BOOST_AUTO_TEST_CASE(old_banlist_entry_upgrade)
{
    // what an old node wrote for a CSubNet: 16-byte ip | 16-byte netmask | 1 byte valid
    CDataStream old(SER_DISK, 150101);
    unsigned char ip[16] = {0,0,0,0,0,0,0,0,0,0,0xff,0xff,10,0,0,1};
    unsigned char mask[16]; memset(mask, 0xff, 16);
    unsigned char valid = 1;
    old.write((const char*)ip, 16);
    old.write((const char*)mask, 16);
    old.write((const char*)&valid, 1);

    CDataStream in(old.begin(), old.end(), SER_DISK, CLIENT_VERSION);
    CSubNet sn;
    bool threw = false; std::string what;
    try { in >> sn; } catch (const std::exception& e) { threw = true; what = e.what(); }
    BOOST_TEST_MESSAGE("banlist threw=" + std::to_string(threw) + " " + what +
                       " parsed=" + (threw ? std::string("-") : sn.ToString()) +
                       " leftover=" + std::to_string(in.size()));
    bool clean = (!threw && sn.ToString() == "10.0.0.1/32" && in.size() == 0);
    BOOST_CHECK_MESSAGE(clean, "REGRESSION: v15.1.1 banlist.dat record does not survive upgrade");
}

// End-to-end: feed CAddrMan::Unserialize a real v15.1.1-format peers.dat body
// (format byte 1). The migration hook does s.SetVersion(70015), but every
// CAddrInfo record re-reads its own version from the file, so the hook has no
// effect on the records it was written to protect.
BOOST_AUTO_TEST_CASE(peersdat_v1_migration_hook_is_ineffective)
{
    const int OLD_CLIENT_VERSION = 150101;

    CDataStream f(SER_DISK, OLD_CLIENT_VERSION);
    f << (unsigned char)1;            // addrman format version written by v15.1.1
    f << (unsigned char)32;           // keysize
    f << uint256S("abcd");            // nKey
    f << (int)1;                      // nNew
    f << (int)0;                      // nTried
    f << (int)(ADDRMAN_NEW_BUCKET_COUNT ^ (1 << 30));

    // one CAddrInfo record, exactly as the old binary laid it out:
    // CAddress { int32 ver | uint32 nTime | uint64 nServices | 16B ip | 16B port }
    // then CAddrInfo's own { CNetAddr source | int64 nLastSuccess | int nAttempts }
    f << (int32_t)OLD_CLIENT_VERSION;
    f << (uint32_t)1789000000;
    f << (uint64_t)NODE_NETWORK;
    unsigned char ip[16] = {0,0,0,0,0,0,0,0,0,0,0xff,0xff,1,2,3,4};
    f.write((const char*)ip, 16);
    unsigned short portN = htons(27714);
    f.write((const char*)&portN, 2);
    f.write((const char*)ip, 16);     // source CNetAddr (plain 16 bytes, old format)
    f << (int64_t)0;                  // nLastSuccess
    f << (int)0;                      // nAttempts
    for (int b = 0; b < ADDRMAN_NEW_BUCKET_COUNT; b++) f << (int)0;  // empty buckets

    CDataStream in(f.begin(), f.end(), SER_DISK, CLIENT_VERSION);
    CAddrMan am;
    bool threw = false; std::string what;
    try { in >> am; } catch (const std::exception& e) { threw = true; what = e.what(); }
    BOOST_TEST_MESSAGE("peers.dat v1 load: threw=" + std::to_string(threw) + " " + what +
                       " size=" + std::to_string(am.size()));
    BOOST_CHECK_MESSAGE(!threw && am.size() == 1,
        "REGRESSION: a v15.1.1 peers.dat cannot be loaded by v15.1.2 - "
        "the s.SetVersion(70015) migration hook does not reach CAddress records");
}

// ---------------------------------------------------------------------------
// QV-2026-002: signature must not be replayable into a different transaction.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(pous_signature_not_replayable_sighash_all)
{
    SelectParams(CBaseChainParams::REGTEST);
    CChainParams& p = Params(CBaseChainParams::REGTEST);
    CKey pool; pool.MakeNewKey(true);
    p.SetAiPoolKeyID(pool.GetPubKey().GetID());
    p.SetPoUSV2ActivationHeight(0);

    CKey attacker; attacker.MakeNewKey(true);
    CScript attackerScript = GetScriptForDestination(attacker.GetPubKey().GetID());
    CScript marker = CScript() << OP_RETURN
        << std::vector<unsigned char>{'Q','V','R','E',0x01};
    CScript poolP2PKH = GetScriptForDestination(pool.GetPubKey().GetID());

    // Genuine pool tx, properly signed with SIGHASH_ALL
    CMutableTransaction good;
    good.vin.resize(1);
    good.vin[0].prevout.hash = uint256S("11");
    good.vout.push_back(CTxOut(1000, poolP2PKH));
    good.vout.push_back(CTxOut(0, marker));
    uint256 h = SignatureHash(poolP2PKH, good, 0, SIGHASH_ALL, 0, NULL);
    std::vector<unsigned char> sig;
    BOOST_REQUIRE(pool.Sign(h, sig));
    sig.push_back((unsigned char)SIGHASH_ALL);
    good.vin[0].scriptSig = CScript() << sig << ToByteVector(pool.GetPubKey());
    BOOST_CHECK_MESSAGE(IsAuthorizedAiPoolTx(CTransaction(good), 100),
                        "genuine signed pool tx must be authorized");

    // Attacker lifts that scriptSig into their own tx paying themselves
    CMutableTransaction evil;
    evil.vin.resize(1);
    evil.vin[0].prevout.hash = uint256S("11");
    evil.vin[0].scriptSig = good.vin[0].scriptSig;   // replayed signature
    for (int i = 0; i < 51; i++)
        evil.vout.push_back(CTxOut(1000, attackerScript));
    evil.vout.push_back(CTxOut(0, marker));
    BOOST_CHECK_MESSAGE(!IsAuthorizedAiPoolTx(CTransaction(evil), 100),
                        "SIGHASH_ALL signature must not authorize a different tx");
    BOOST_CHECK_MESSAGE(!IsValidPoUSRewardTx(CTransaction(evil), 100),
                        "replayed reward must be rejected");
}

// Hardening probe: the fix accepts ANY hashtype. If the pool ever signs an input
// with SIGHASH_NONE|ANYONECANPAY, that signature authorizes arbitrary outputs.
BOOST_AUTO_TEST_CASE(pous_permissive_hashtype_is_replayable)
{
    SelectParams(CBaseChainParams::REGTEST);
    CChainParams& p = Params(CBaseChainParams::REGTEST);
    CKey pool; pool.MakeNewKey(true);
    p.SetAiPoolKeyID(pool.GetPubKey().GetID());
    p.SetPoUSV2ActivationHeight(0);

    CKey attacker; attacker.MakeNewKey(true);
    CScript attackerScript = GetScriptForDestination(attacker.GetPubKey().GetID());
    CScript marker = CScript() << OP_RETURN
        << std::vector<unsigned char>{'Q','V','R','E',0x01};
    CScript poolP2PKH = GetScriptForDestination(pool.GetPubKey().GetID());

    int ht = SIGHASH_NONE | SIGHASH_ANYONECANPAY;
    CMutableTransaction orig;
    orig.vin.resize(1);
    orig.vin[0].prevout.hash = uint256S("22");
    orig.vout.push_back(CTxOut(500, poolP2PKH));
    uint256 h = SignatureHash(poolP2PKH, orig, 0, ht, 0, NULL);
    std::vector<unsigned char> sig;
    BOOST_REQUIRE(pool.Sign(h, sig));
    sig.push_back((unsigned char)ht);
    CScript ss = CScript() << sig << ToByteVector(pool.GetPubKey());

    CMutableTransaction evil;
    evil.vin.resize(1);
    evil.vin[0].prevout.hash = uint256S("22");   // same outpoint (ANYONECANPAY binds it)
    evil.vin[0].scriptSig = ss;
    for (int i = 0; i < 51; i++)
        evil.vout.push_back(CTxOut(1000, attackerScript));
    evil.vout.push_back(CTxOut(0, marker));

    bool accepted = IsValidPoUSRewardTx(CTransaction(evil), 100);
    BOOST_TEST_MESSAGE(std::string("NONE|ANYONECANPAY replay accepted = ") +
                       (accepted ? "YES" : "no"));
    BOOST_CHECK_MESSAGE(!accepted,
        "HARDENING: authorization should require SIGHASH_ALL, else a permissively "
        "signed pool input authorizes attacker-chosen outputs");
}


// ---------------------------------------------------------------------------
// LIVE-CHAIN CHECK: a real mainnet QVAI attestation (block 4725, i.e. well above
// the new activation height of 600). Does the hardened TxSpendsFromKeyID still
// accept it? If not, v15.1.2 computes different credits than the chain was built
// with, and stake weights -- hence block validity -- diverge.
// ---------------------------------------------------------------------------
static const char* LIVE_QVAI_TX_4725 =
 "010000000e12af6a014fc52ee5a23547af8749987f6d972bd2f1b4d048a28b8bf1a3455684"
 "1b8b5c13010000006a4730440220354e200dd952ae84831cfcf22022d06b125d22ba6b0394"
 "4e4e48031cf492f72e02207e865cee2aa6baf7c4eec856057b87d5991c04759a137bf0dd71"
 "68268559b578012102eea4aef631cccd52f9d1dfb2eae9050830830989c7e691b377d57c3d"
 "1a7f410dffffffff0200000000000000002e6a2c515641490103e58d799a1915785c410075"
 "e818fc38b3ab4a6c5b03068b89a2c471909f13d3f601ff00001274d0403069000000001976"
 "a914a9bfdee0c1874e4f8f167959166fc89c85316c7988ac00000000";

BOOST_AUTO_TEST_CASE(live_mainnet_attestation_still_accepted)
{
    SelectParams(CBaseChainParams::MAIN);
    CChainParams& p = Params(CBaseChainParams::MAIN);

    CTransaction tx;
    BOOST_REQUIRE(DecodeHexTx(tx, LIVE_QVAI_TX_4725));
    BOOST_REQUIRE_EQUAL(tx.vin.size(), 1u);

    // Recover the signing pubkey from the real scriptSig and treat it as the
    // authorized AI hub key (that is who actually signed this on mainnet).
    std::vector<std::vector<unsigned char> > stack;
    {
        CScript::const_iterator pc = tx.vin[0].scriptSig.begin();
        opcodetype op; std::vector<unsigned char> data;
        while (tx.vin[0].scriptSig.GetOp(pc, op, data)) stack.push_back(data);
    }
    BOOST_REQUIRE_EQUAL(stack.size(), 2u);
    CPubKey signerPub(stack[1]);
    BOOST_REQUIRE(signerPub.IsFullyValid());
    p.SetAiHubKeyID(signerPub.GetID());

    BOOST_TEST_MESSAGE("live tx scriptSig size = " + std::to_string(tx.vin[0].scriptSig.size()));
    BOOST_TEST_MESSAGE("live tx signer address = " + CBitcoinAddress(signerPub.GetID()).ToString());

    // Legacy path (below activation) - this is how the chain was actually built.
    bool legacyOK = IsValidAiAttestationTx(tx, 500);
    // Enforced path - this is how v15.1.2 revalidates blocks >= 600.
    bool enforcedOK = IsValidAiAttestationTx(tx, 4725);

    BOOST_TEST_MESSAGE(std::string("accepted below activation (h=500)  : ") + (legacyOK ? "YES" : "NO"));
    BOOST_TEST_MESSAGE(std::string("accepted above activation (h=4725) : ") + (enforcedOK ? "YES" : "NO"));

    BOOST_CHECK_MESSAGE(legacyOK, "sanity: legacy rule should accept a real mainnet attestation");
    BOOST_CHECK_MESSAGE(enforcedOK,
        "CONSENSUS SPLIT: v15.1.2 rejects a real mainnet attestation that the chain "
        "was built on; credits and stake weights will diverge from the live chain");
}

BOOST_AUTO_TEST_SUITE_END()
