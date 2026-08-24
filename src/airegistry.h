// Copyright (c) 2026 The Quavence Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_AIREGISTRY_H
#define BITCOIN_AIREGISTRY_H

#include "uint256.h"
#include "chain.h"
#include "primitives/transaction.h"
#include "script/standard.h"
#include "sync.h"
#include <map>
#include <set>
#include <vector>

class CChainParams;

static const int AI_ATTESTATION_WINDOW = 1440;       // ~24 hours at 60s blocks
static const int MAX_AI_BOOST_PERCENT = 50;           // maximum 50% boost
static const int BASE_AI_BOOST_PERCENT = 20;          // base 20% boost
static const int MIN_ATTESTATIONS_FOR_BOOST = 1;      // minimum attestations to qualify
static const unsigned char AI_MAGIC[4] = {'Q', 'V', 'A', 'I'};

struct AiAttestationRecord {
    uint256 txid;
    int blockHeight;
    int64_t blockTime;
    uint256 consensusHash;
    uint8_t taskType;
    uint8_t workerCount;
    uint8_t agreementRatio;
    uint32_t refBlockHeight;
};

// Extracts AI attestation from a transaction output if OP_RETURN contains QVAI magic
bool ExtractAiAttestation(const CTxOut& out, AiAttestationRecord& record);

// Evaluates whether a staking prevout qualifies for Proof-of-Useful-Stake boost
int GetAiStakeBoost(const COutPoint& prevout, const CBlockIndex* pindexPrev);

// Attestation count and active boost helpers for consensus query
int GetAiAttestationsCountInWindow(int currentHeight);
int GetActiveAiStakeBoost(int currentHeight);

// Connect / disconnect block updates to the attestation registry
void RegisterAiAttestationsInBlock(const CBlock& block, int nHeight, int64_t nTime);
void UnregisterAiAttestationsInBlock(const CBlock& block, int nHeight);

// Scans active chain blocks within attestation window and warms up the in-memory registry on node startup
void WarmupAiRegistry(const CChainParams& chainparams);

#endif // BITCOIN_AIREGISTRY_H
