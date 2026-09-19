// Copyright (c) 2026 The Quavence developers
// Licensed under the Business Source License 1.1 (BSL-1.1)
// See LICENSE-ADDITIONS in the root of this repository

#ifndef BITCOIN_AIREGISTRY_H
#define BITCOIN_AIREGISTRY_H

#include "uint256.h"
#include "chain.h"
#include "primitives/transaction.h"
#include "script/standard.h"
#include "sync.h"
#include "pubkey.h"
#include <map>
#include <set>
#include <vector>

class CChainParams;

// ─── Constants ───────────────────────────────────────────────────────────────

static const int AI_ATTESTATION_WINDOW    = 1440;   // ~24h at 60s blocks
static const int MAX_AI_BOOST_PERCENT     = 50;
static const int BASE_AI_BOOST_PERCENT    = 20;
static const int MIN_ATTESTATIONS_FOR_BOOST = 1;

// Magic bytes for Hub attestation OP_RETURN
static const unsigned char AI_MAGIC[4]   = {'Q','V','A','I'};

// Magic bytes for Pool reward marker OP_RETURN
static const unsigned char QVRE_MAGIC[4] = {'Q','V','R','E'};

// Magic bytes for PoUS Glyph Carrier OP_RETURN
static const unsigned char GLYPH_MAGIC[4] = {'Q', 'V', 'N', 'C'};
static const CAmount GLYPH_CARRIER_DUST   = 10000; // 0.00010000 QVNC

// ─── Structs ─────────────────────────────────────────────────────────────────

struct AiAttestationRecord {
    uint256  txid;
    int      blockHeight;
    int64_t  blockTime;
    uint256  consensusHash;
    uint8_t  version;
    uint8_t  taskType;
    uint8_t  workerCount;
    uint8_t  agreementRatio;
    uint32_t refBlockHeight;
};

struct GlyphCarrierRecord {
    uint8_t  version;
    uint8_t  opType;
    uint256  glyphHash;
    uint16_t edition;
};

// ─── Parsing ─────────────────────────────────────────────────────────────────

bool ExtractAiAttestation(const CTxOut& out, AiAttestationRecord& record);
bool HasPoUSRewardMarker(const CTransaction& tx);
bool ExtractGlyphRecord(const CTxOut& out, GlyphCarrierRecord& record);
bool GetTxGlyphCarrier(const CTransaction& tx, unsigned int nOut, GlyphCarrierRecord& record);

// ─── Authorization ───────────────────────────────────────────────────────────

bool IsAuthorizedAiHubTx(const CTransaction& tx, int nHeight = -1);
bool IsAuthorizedAiPoolTx(const CTransaction& tx, int nHeight = -1);
bool IsValidAiAttestationTx(const CTransaction& tx, int nHeight = -1);
bool IsValidPoUSRewardTx(const CTransaction& tx, int nHeight = -1);

// ─── Registry lifecycle ──────────────────────────────────────────────────────

void RegisterAiAttestationsInBlock(const CBlock& block, int nHeight, int64_t nTime);
void UnregisterAiAttestationsInBlock(const CBlock& block, int nHeight);

// ─── Query API ───────────────────────────────────────────────────────────────

int      GetAiAttestationsCountInWindow(int currentHeight);
uint32_t GetWorkerCreditsInWindow(const CKeyID& workerID, int currentHeight);
int      GetWorkerPoUSBoost(uint32_t credits);

// Main consensus boost calculation by stake scriptPubKey (zero disk I/O)
int GetAiStakeBoost(const CScript& stakeScript, const CBlockIndex* pindexPrev);

// Global metric for RPC / logging
int GetActiveAiStakeBoost(int currentHeight);

// ─── Startup ─────────────────────────────────────────────────────────────────

void WarmupAiRegistry(const CChainParams& chainparams);

#endif // BITCOIN_AIREGISTRY_H
