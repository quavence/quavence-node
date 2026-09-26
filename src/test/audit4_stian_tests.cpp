// Independent audit 4 — regressions introduced by the NEW-6 fix (757fc753e).
#include "addrdb.h"
#include "chainparams.h"
#include "clientversion.h"
#include "hash.h"
#include "netaddress.h"
#include "netbase.h"
#include "protocol.h"
#include "streams.h"
#include "test/test_bitcoin.h"
#include "version.h"

#include <boost/test/unit_test.hpp>
#include <map>

BOOST_FIXTURE_TEST_SUITE(audit4_tests, BasicTestingSetup)

static const char* V3SEED =
    "kalwfcd7ia3gcwksq7yipu3b2lseibic6ytmawkbvq7odlleic6lifqd.onion";
// A Tor v2 style onion (16 base32 chars -> 10 bytes), OnionCat prefix, no v3 key.
static const char* V2SEED = "6zeveij73qxcmfie.onion";

// ---------------------------------------------------------------------------
// The NEW-6 fix itself: distinct identities must no longer collide.
// ---------------------------------------------------------------------------
static CNetAddr MakeCollidingAddr(const CNetAddr& v3)
{
    static const unsigned char ONIONCAT[6] = {0xFD,0x87,0xD8,0x7E,0xEB,0x43};
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

BOOST_AUTO_TEST_CASE(new6_collision_is_fixed)
{
    CNetAddr v3;
    BOOST_REQUIRE(v3.SetSpecial(V3SEED));
    CNetAddr spoof = MakeCollidingAddr(v3);

    BOOST_CHECK_MESSAGE(!(spoof == v3), "NEW-6 must no longer compare equal");
    BOOST_CHECK_MESSAGE((spoof < v3) || (v3 < spoof), "must be order-distinct");

    std::map<CNetAddr, int> m;
    m[v3] = 1; m[spoof] = 2;
    BOOST_CHECK_MESSAGE(m.size() == 2, "must occupy separate map slots");

    CSubNet banOnSpoof(spoof);
    BOOST_CHECK_MESSAGE(!banOnSpoof.Match(v3),
        "banning the spoof must no longer ban the honest v3 peer");

    CSubNet banOnV3(v3);
    BOOST_CHECK_MESSAGE(!banOnV3.Match(spoof),
        "banning the honest v3 peer must no longer ban the spoof/non-v3 address");
}

// ---------------------------------------------------------------------------
// NEW-9: CSubNet's new read path decides whether a 32-byte Tor v3 key follows
// by testing `network.IsTor() && s.size() >= 32`. IsTor() is true for ANY
// OnionCat-range address (Tor v2 included), but the WRITE path only emits the
// key when IsTorV3(). So a v2/OnionCat subnet followed by >=32 bytes makes the
// reader eat 32 bytes that belong to the next field.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(subnet_torv2_desyncs_the_stream)
{
    CNetAddr v2;
    BOOST_REQUIRE(v2.SetSpecial(V2SEED));
    BOOST_REQUIRE(!v2.IsTorV3());
    BOOST_REQUIRE(v2.IsTor());          // OnionCat prefix -> IsTor() true

    CSubNet sn(v2);
    BOOST_REQUIRE(sn.IsValid());

    // Write the subnet, then a recognisable 64-byte trailer (as banlist.dat does:
    // every CSubNet is followed by its CBanEntry, and then more entries).
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << sn;
    const size_t subnetBytes = ss.size();
    for (int i = 0; i < 64; i++) ss << (unsigned char)0xAB;

    CSubNet back;
    bool threw = false; std::string what;
    try { ss >> back; } catch (const std::exception& e) { threw = true; what = e.what(); }

    size_t consumedByRead = (subnetBytes + 64) - ss.size();
    BOOST_TEST_MESSAGE("subnet wrote      = " + std::to_string(subnetBytes) + " bytes");
    BOOST_TEST_MESSAGE("read consumed     = " + std::to_string(consumedByRead) + " bytes");
    BOOST_TEST_MESSAGE("trailer remaining = " + std::to_string(ss.size()) + " of 64");
    if (threw) BOOST_TEST_MESSAGE("threw: " + what);

    BOOST_CHECK_MESSAGE(!threw, "reading a Tor v2 subnet must not throw");
    BOOST_CHECK_MESSAGE(consumedByRead == subnetBytes,
        "NEW-9: the reader consumed MORE than was written for a Tor v2 subnet - "
        "it ate bytes belonging to the next record, desyncing banlist.dat");
    BOOST_CHECK_MESSAGE(ss.size() == 64,
        "NEW-9: the trailing record was corrupted by the over-read");
}

// A Tor v3 subnet written as the LAST record, with fewer than 32 trailing bytes,
// silently loses its key -- and Match() now requires an exact key, so the ban
// stops working.
BOOST_AUTO_TEST_CASE(subnet_torv3_last_record_loses_key)
{
    CNetAddr v3;
    BOOST_REQUIRE(v3.SetSpecial(V3SEED));
    CSubNet sn(v3);

    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << sn;
    for (int i = 0; i < 8; i++) ss << (unsigned char)0x00;   // short trailer (<32)

    CSubNet back;
    bool threw = false; std::string what;
    try { ss >> back; } catch (const std::exception& e) { threw = true; what = e.what(); }
    BOOST_TEST_MESSAGE(std::string("threw=") + (threw ? what : "no"));
    if (!threw) {
        BOOST_TEST_MESSAGE("reloaded subnet : " + back.ToString());
        BOOST_TEST_MESSAGE(std::string("still matches the v3 peer = ") +
                           (back.Match(v3) ? "YES" : "NO"));
        BOOST_CHECK_MESSAGE(back.Match(v3),
            "NEW-9: a Tor v3 ban stored as the final record loses its key and "
            "silently stops matching the banned peer");
    }
}

// Round-trip of a full multi-entry banlist, exactly as CBanDB does it.
BOOST_AUTO_TEST_CASE(banlist_multi_entry_roundtrip)
{
    CNetAddr v3, v2;
    BOOST_REQUIRE(v3.SetSpecial(V3SEED));
    BOOST_REQUIRE(v2.SetSpecial(V2SEED));

    banmap_t out;
    CBanEntry e1(1789000000); e1.nBanUntil = 1789999999;
    CBanEntry e2(1789000001); e2.nBanUntil = 1789999998;
    CBanEntry e3(1789000002); e3.nBanUntil = 1789999997;
    out[CSubNet(v2)] = e1;
    out[CSubNet(v3)] = e2;
    out[CSubNet(CNetAddr())] = e3;   // a plain address too

    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << out;

    banmap_t back;
    bool threw = false; std::string what;
    try { ss >> back; } catch (const std::exception& e) { threw = true; what = e.what(); }

    BOOST_TEST_MESSAGE("wrote " + std::to_string(out.size()) + " ban entries");
    BOOST_TEST_MESSAGE(std::string("read threw = ") + (threw ? what : "no"));
    BOOST_TEST_MESSAGE("read back  = " + std::to_string(back.size()) +
                       " entries, leftover=" + std::to_string(ss.size()));

    BOOST_CHECK_MESSAGE(!threw, "NEW-9: a mixed banlist fails to deserialize");
    BOOST_CHECK_MESSAGE(back.size() == out.size(),
        "NEW-9: entries were lost or merged on reload");
    BOOST_CHECK_MESSAGE(ss.size() == 0, "NEW-9: bytes left over -> stream desync");
}

// ---------------------------------------------------------------------------
// NEW-10: CSubNet::Match now requires exact v3 key equality whenever EITHER
// side is v3. A range ban covering the OnionCat space therefore no longer
// matches any Tor v3 peer.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(range_ban_no_longer_covers_torv3)
{
    CNetAddr v3;
    BOOST_REQUIRE(v3.SetSpecial(V3SEED));

    // Operator bans the whole OnionCat range (the usual way to block Tor).
    CSubNet range;
    BOOST_REQUIRE(LookupSubNet("fd87:d87e:eb43::/48", range));
    BOOST_REQUIRE(range.IsValid());
    BOOST_TEST_MESSAGE("range ban  : " + range.ToString());
    bool matches = range.Match(v3);
    BOOST_TEST_MESSAGE(std::string("covers the v3 peer = ") + (matches ? "YES" : "NO"));

    BOOST_CHECK_MESSAGE(matches,
        "NEW-10: a subnet/range ban no longer matches Tor v3 peers, so v3 nodes "
        "cannot be blocked by range - only by exact onion key");
}


// Deterministic banlist corruption: a Tor v2 subnet followed by ANOTHER entry.
// (The map-ordered test above can pass by luck when the v2 entry sorts last and
// fewer than 32 bytes follow it.)
BOOST_AUTO_TEST_CASE(banlist_v2_entry_corrupts_following_entries)
{
    CNetAddr v2, plain;
    BOOST_REQUIRE(v2.SetSpecial(V2SEED));
    BOOST_REQUIRE(LookupHost("203.0.113.7", plain, false));

    // Hand-build the stream so the v2 subnet is definitely NOT last:
    //   [CSubNet(v2)] [CBanEntry] [CSubNet(plain)] [CBanEntry]
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    CBanEntry e1(1789000000); e1.nBanUntil = 1789999999;
    CBanEntry e2(1789000001); e2.nBanUntil = 1789999998;
    ss << CSubNet(v2);
    ss << e1;
    ss << CSubNet(plain);
    ss << e2;
    const size_t total = ss.size();

    CSubNet s1, s2; CBanEntry r1, r2;
    bool threw = false; std::string what;
    try {
        ss >> s1; ss >> r1; ss >> s2; ss >> r2;
    } catch (const std::exception& e) { threw = true; what = e.what(); }

    BOOST_TEST_MESSAGE("stream total = " + std::to_string(total) + " bytes");
    BOOST_TEST_MESSAGE(std::string("threw = ") + (threw ? what : "no"));
    if (!threw) {
        BOOST_TEST_MESSAGE("entry 1 : " + s1.ToString());
        BOOST_TEST_MESSAGE("entry 2 : " + s2.ToString() + "  (expected 203.0.113.7/32)");
        BOOST_TEST_MESSAGE("leftover = " + std::to_string(ss.size()));
    }

    BOOST_CHECK_MESSAGE(!threw,
        "NEW-9: a banlist whose first entry is a Tor v2 subnet fails to parse");
    if (!threw) {
        BOOST_CHECK_MESSAGE(s2.ToString() == "203.0.113.7/32",
            "NEW-9: the entry AFTER the Tor v2 subnet was corrupted by the 32-byte over-read");
        BOOST_CHECK_MESSAGE(ss.size() == 0,
            "NEW-9: stream desynced - bytes left over after reading all records");
    }
}

BOOST_AUTO_TEST_SUITE_END()
