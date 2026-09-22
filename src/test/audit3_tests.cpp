// Independent audit 3 of v15.1.3 (NEW-1 / NEW-3 / NEW-4 fixes).
// Focus: regressions introduced BY the fixes.
#include "airegistry.h"
#include "addrman.h"
#include "base58.h"
#include "chainparams.h"
#include "clientversion.h"
#include "core_io.h"
#include "hash.h"
#include "key.h"
#include "netaddress.h"
#include "primitives/transaction.h"
#include "protocol.h"
#include "script/interpreter.h"
#include "script/script.h"
#include "script/sign.h"
#include "script/standard.h"
#include "streams.h"
#include "test/test_bitcoin.h"
#include "utilstrencodings.h"
#include "version.h"

#include <boost/test/unit_test.hpp>
#include <map>
#include <set>

BOOST_FIXTURE_TEST_SUITE(audit3_tests, BasicTestingSetup)

static const char* V3SEED =
    "kalwfcd7ia3gcwksq7yipu3b2lseibic6ytmawkbvq7odlleic6lifqd.onion";

// The OnionCat prefix used for the first 6 bytes of `ip`.
static const unsigned char ONIONCAT[6] = {0xFD,0x87,0xD8,0x7E,0xEB,0x43};

// ---------------------------------------------------------------------------
// NEW-6 candidate: the reworked operator== / operator< compare the 32-byte v3
// key ONLY when BOTH sides are v3. A non-v3 CNetAddr carrying the same 16-byte
// `ip` therefore compares EQUAL to a Tor v3 address.
//
// `ip` for v3 is: OnionCat(6) || first 10 bytes of Hash(pubkey)  -- and a Tor v2
// address copies its 10 decoded bytes into `ip` verbatim, with no hashing. So an
// attacker needs no preimage search: they just emit those 10 bytes.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(torv3_collides_with_nonv3_same_ip)
{
    CNetAddr v3;
    BOOST_REQUIRE(v3.SetSpecial(V3SEED));
    BOOST_REQUIRE(v3.IsTorV3());

    // Rebuild the victim's 16-byte ip exactly as InitIpFromTorV3() does.
    std::vector<unsigned char> key = v3.GetTorV3();
    BOOST_REQUIRE_EQUAL(key.size(), 32u);
    uint256 h = Hash(key.begin(), key.end());
    unsigned char spoofIp[16];
    memcpy(spoofIp, ONIONCAT, 6);
    memcpy(spoofIp + 6, h.begin(), 10);

    // An attacker can put those 16 bytes on the wire directly: with the new
    // versioned format, netType==1 reads 16 raw bytes and clears vchTorV3.
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    uint8_t netType = 1;
    ss << netType;
    ss.write((const char*)spoofIp, 16);
    CNetAddr spoof;
    ss >> spoof;

    BOOST_TEST_MESSAGE("victim v3 : " + v3.ToStringIP());
    BOOST_TEST_MESSAGE("spoof     : " + spoof.ToStringIP() +
                       " isTorV3=" + std::to_string(spoof.IsTorV3()));

    BOOST_CHECK_MESSAGE(!spoof.IsTorV3(), "spoof must not carry a v3 key");

    bool equal    = (spoof == v3);
    bool ordersEq = (!(spoof < v3) && !(v3 < spoof));
    BOOST_TEST_MESSAGE(std::string("spoof == v3         : ") + (equal ? "YES" : "no"));
    BOOST_TEST_MESSAGE(std::string("equivalent ordering : ") + (ordersEq ? "YES" : "no"));

    BOOST_CHECK_MESSAGE(!equal,
        "NEW-6: a non-v3 address with a matching 16-byte ip compares EQUAL to a "
        "Tor v3 address; distinct network identities must never compare equal");
    BOOST_CHECK_MESSAGE(!ordersEq,
        "NEW-6: spoof and the real v3 address are order-equivalent, so they "
        "collide as a single key in addrman/banlist containers");
}

// Practical consequence: they occupy the SAME slot in a std::map keyed by
// CNetAddr, which is exactly how addrman's mapAddr and the ban map are keyed.
BOOST_AUTO_TEST_CASE(torv3_collision_squats_map_slot)
{
    CNetAddr v3;
    BOOST_REQUIRE(v3.SetSpecial(V3SEED));
    std::vector<unsigned char> key = v3.GetTorV3();
    uint256 h = Hash(key.begin(), key.end());
    unsigned char spoofIp[16];
    memcpy(spoofIp, ONIONCAT, 6);
    memcpy(spoofIp + 6, h.begin(), 10);

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    uint8_t netType = 1;
    ss << netType;
    ss.write((const char*)spoofIp, 16);
    CNetAddr spoof;
    ss >> spoof;

    std::map<CNetAddr, int> m;
    m[v3] = 1;
    m[spoof] = 2;   // should be a DIFFERENT key
    BOOST_TEST_MESSAGE("map size after inserting v3 + spoof = " + std::to_string(m.size()));
    BOOST_TEST_MESSAGE("value stored under the real v3 key   = " + std::to_string(m[v3]));

    BOOST_CHECK_MESSAGE(m.size() == 2,
        "NEW-6: the attacker's address overwrote the real Tor v3 peer's map entry "
        "(addrman slot squatting / eclipse primitive)");
}

// ---------------------------------------------------------------------------
// CSubNet now pins the nested CNetAddr to the legacy format on BOTH disk and
// wire. Check what that does to a ban on a Tor v3 peer.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(subnet_torv3_ban_roundtrip)
{
    CNetAddr v3;
    BOOST_REQUIRE(v3.SetSpecial(V3SEED));
    CSubNet sn(v3);
    BOOST_REQUIRE(sn.IsValid());
    BOOST_TEST_MESSAGE("subnet before: " + sn.ToString());

    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << sn;
    CSubNet back;
    ss >> back;
    BOOST_TEST_MESSAGE("subnet after : " + back.ToString() +
                       " leftover=" + std::to_string(ss.size()));

    BOOST_CHECK_EQUAL(ss.size(), 0u);          // stream must stay aligned
    BOOST_CHECK_MESSAGE(back.Match(v3),
        "a persisted ban on a Tor v3 peer must still match that peer after reload");
}

// ---------------------------------------------------------------------------
// NEW-3: SIGHASH_ALL is now required. Confirm the permissive-hashtype replay is
// rejected, and that a REAL mainnet attestation is still accepted.
// ---------------------------------------------------------------------------
static const char* LIVE_QVAI_TX_4725 =
 "010000000e12af6a014fc52ee5a23547af8749987f6d972bd2f1b4d048a28b8bf1a3455684"
 "1b8b5c13010000006a4730440220354e200dd952ae84831cfcf22022d06b125d22ba6b0394"
 "4e4e48031cf492f72e02207e865cee2aa6baf7c4eec856057b87d5991c04759a137bf0dd71"
 "68268559b578012102eea4aef631cccd52f9d1dfb2eae9050830830989c7e691b377d57c3d"
 "1a7f410dffffffff0200000000000000002e6a2c515641490103e58d799a1915785c410075"
 "e818fc38b3ab4a6c5b03068b89a2c471909f13d3f601ff00001274d0403069000000001976"
 "a914a9bfdee0c1874e4f8f167959166fc89c85316c7988ac00000000";

BOOST_AUTO_TEST_CASE(live_attestation_survives_sighash_all_rule)
{
    SelectParams(CBaseChainParams::MAIN);
    CChainParams& p = Params(CBaseChainParams::MAIN);

    CTransaction tx;
    BOOST_REQUIRE(DecodeHexTx(tx, LIVE_QVAI_TX_4725));
    std::vector<std::vector<unsigned char> > stack;
    {
        CScript::const_iterator pc = tx.vin[0].scriptSig.begin();
        opcodetype op; std::vector<unsigned char> data;
        while (tx.vin[0].scriptSig.GetOp(pc, op, data)) stack.push_back(data);
    }
    BOOST_REQUIRE_EQUAL(stack.size(), 2u);
    BOOST_TEST_MESSAGE("live sig hashtype byte = 0x" +
        HexStr(std::vector<unsigned char>(stack[0].end()-1, stack[0].end())));
    CPubKey signer(stack[1]);
    p.SetAiHubKeyID(signer.GetID());

    // Well above the new activation height of 6500 -> strict path.
    bool ok = IsValidAiAttestationTx(tx, 7000);
    BOOST_TEST_MESSAGE(std::string("accepted under SIGHASH_ALL rule = ") + (ok ? "YES" : "NO"));
    BOOST_CHECK_MESSAGE(ok,
        "REGRESSION: the SIGHASH_ALL rule rejects a real mainnet attestation");
}

BOOST_AUTO_TEST_CASE(permissive_hashtype_now_rejected)
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

    CMutableTransaction evil;
    evil.vin.resize(1);
    evil.vin[0].prevout.hash = uint256S("22");
    evil.vin[0].scriptSig = CScript() << sig << ToByteVector(pool.GetPubKey());
    for (int i = 0; i < 51; i++)
        evil.vout.push_back(CTxOut(1000, attackerScript));
    evil.vout.push_back(CTxOut(0, marker));

    bool accepted = IsValidPoUSRewardTx(CTransaction(evil), 100);
    BOOST_TEST_MESSAGE(std::string("NONE|ANYONECANPAY replay accepted = ") + (accepted ? "YES" : "no"));
    BOOST_CHECK_MESSAGE(!accepted, "NEW-3 should reject non-SIGHASH_ALL authorization");
}


// Helper: build the attacker's colliding non-v3 address for a given v3 address.
static CNetAddr MakeCollidingAddr(const CNetAddr& v3)
{
    std::vector<unsigned char> key = v3.GetTorV3();
    uint256 h = Hash(key.begin(), key.end());
    unsigned char spoofIp[16];
    memcpy(spoofIp, ONIONCAT, 6);
    memcpy(spoofIp + 6, h.begin(), 10);
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    uint8_t netType = 1;
    ss << netType;
    ss.write((const char*)spoofIp, 16);
    CNetAddr spoof;
    ss >> spoof;
    return spoof;
}

// Impact 1: ban poisoning. The attacker connects from the colliding address and
// deliberately misbehaves. The node bans THAT address -- and the ban matches the
// honest Tor v3 peer too, knocking it off the network.
BOOST_AUTO_TEST_CASE(ban_poisoning_via_collision)
{
    CNetAddr v3;
    BOOST_REQUIRE(v3.SetSpecial(V3SEED));
    CNetAddr spoof = MakeCollidingAddr(v3);

    CSubNet banOnSpoof(spoof);          // what the node records when banning the attacker
    BOOST_REQUIRE(banOnSpoof.IsValid());
    bool hitsVictim = banOnSpoof.Match(v3);
    BOOST_TEST_MESSAGE("ban recorded on : " + banOnSpoof.ToString());
    BOOST_TEST_MESSAGE(std::string("also bans the honest v3 peer = ") + (hitsVictim ? "YES" : "no"));

    BOOST_CHECK_MESSAGE(!hitsVictim,
        "NEW-6: banning the attacker's crafted address also bans the honest Tor v3 "
        "peer -- a remote DoS that evicts any chosen onion node");
}

// Impact 2: the real CAddrMan. Adding the attacker's colliding address must not
// displace or absorb a known Tor v3 peer.
BOOST_AUTO_TEST_CASE(addrman_slot_squat_real)
{
    CNetAddr v3;
    BOOST_REQUIRE(v3.SetSpecial(V3SEED));
    CNetAddr spoof = MakeCollidingAddr(v3);

    CAddrMan am;
    CNetAddr source;
    source.SetSpecial(V3SEED);

    CAddress a1(CService(v3, 27714), NODE_NETWORK);
    a1.nTime = 1789000000;
    CAddress a2(CService(spoof, 27714), NODE_NETWORK);
    a2.nTime = 1789000000;

    am.Add(a1, source);
    size_t afterReal = am.size();
    am.Add(a2, source);
    size_t afterSpoof = am.size();

    BOOST_TEST_MESSAGE("addrman size after real v3   = " + std::to_string(afterReal));
    BOOST_TEST_MESSAGE("addrman size after spoof add = " + std::to_string(afterSpoof));
    BOOST_CHECK_MESSAGE(afterSpoof == afterReal + 1,
        "NEW-6: the colliding address was absorbed into the Tor v3 peer's addrman "
        "entry instead of being stored separately");
}

BOOST_AUTO_TEST_SUITE_END()
