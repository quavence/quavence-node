// Copyright (c) 2026 The Quavence Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "airegistry.h"
#include "util.h"
#include "main.h"
#include "chainparams.h"
#include "script/script.h"
#include <algorithm>

static CCriticalSection cs_airegistry;
static std::map<int, std::vector<AiAttestationRecord> > mapHeightToAttestations;

bool ExtractAiAttestation(const CTxOut& out, AiAttestationRecord& record)
{
    const CScript& script = out.scriptPubKey;
    if (script.empty() || script[0] != OP_RETURN) {
        return false;
    }

    CScript::const_iterator pc = script.begin() + 1;
    opcodetype opcode;
    std::vector<unsigned char> data;

    if (!script.GetOp(pc, opcode, data) || data.size() < 44) {
        return false;
    }

    if (data[0] != AI_MAGIC[0] || data[1] != AI_MAGIC[1] ||
        data[2] != AI_MAGIC[2] || data[3] != AI_MAGIC[3]) {
        return false;
    }

    record.taskType = data[5];
    record.consensusHash = uint256(std::vector<unsigned char>(data.begin() + 6, data.begin() + 38));
    record.workerCount = data[38];
    record.agreementRatio = data[39];
    record.refBlockHeight = (static_cast<uint32_t>(data[40]) << 24) |
                            (static_cast<uint32_t>(data[41]) << 16) |
                            (static_cast<uint32_t>(data[42]) << 8)  |
                            (static_cast<uint32_t>(data[43]));

    return true;
}

void RegisterAiAttestationsInBlock(const CBlock& block, int nHeight, int64_t nTime)
{
    LOCK(cs_airegistry);

    std::vector<AiAttestationRecord> blockAttestations;
    for (size_t i = 0; i < block.vtx.size(); i++) {
        const CTransaction& tx = block.vtx[i];
        for (size_t j = 0; j < tx.vout.size(); j++) {
            AiAttestationRecord rec;
            if (ExtractAiAttestation(tx.vout[j], rec)) {
                rec.txid = tx.GetHash();
                rec.blockHeight = nHeight;
                rec.blockTime = nTime;
                blockAttestations.push_back(rec);
                LogPrintf("AI Registry: Indexed attestation in block %d, tx %s, task_type=%d, workers=%d\n",
                          nHeight, rec.txid.ToString().c_str(), rec.taskType, rec.workerCount);
            }
        }
    }

    if (!blockAttestations.empty()) {
        mapHeightToAttestations[nHeight] = blockAttestations;
    }

    int pruneHeight = nHeight - (AI_ATTESTATION_WINDOW * 2);
    while (!mapHeightToAttestations.empty() && mapHeightToAttestations.begin()->first < pruneHeight) {
        mapHeightToAttestations.erase(mapHeightToAttestations.begin());
    }
}

void UnregisterAiAttestationsInBlock(const CBlock& block, int nHeight)
{
    LOCK(cs_airegistry);
    mapHeightToAttestations.erase(nHeight);
}

int GetAiAttestationsCountInWindow(int currentHeight)
{
    LOCK(cs_airegistry);

    int minHeight = std::max(0, currentHeight - AI_ATTESTATION_WINDOW);
    int qualifyingAttestations = 0;

    for (std::map<int, std::vector<AiAttestationRecord> >::const_iterator it = mapHeightToAttestations.lower_bound(minHeight);
         it != mapHeightToAttestations.end() && it->first <= currentHeight; ++it) {
        for (size_t i = 0; i < it->second.size(); i++) {
            const AiAttestationRecord& rec = it->second[i];
            if (rec.workerCount >= 1 && rec.agreementRatio >= 178) { // >= 70% agreement (178/255)
                qualifyingAttestations++;
            }
        }
    }

    return qualifyingAttestations;
}

int GetActiveAiStakeBoost(int currentHeight)
{
    int qualifyingAttestations = GetAiAttestationsCountInWindow(currentHeight);
    if (qualifyingAttestations < MIN_ATTESTATIONS_FOR_BOOST) {
        return 0;
    }

    int scaledBoost = BASE_AI_BOOST_PERCENT + std::min(30, qualifyingAttestations * 5);
    return std::min(MAX_AI_BOOST_PERCENT, scaledBoost);
}

int GetAiStakeBoost(const COutPoint& prevout, const CBlockIndex* pindexPrev)
{
    if (!pindexPrev) {
        return 0;
    }
    return GetActiveAiStakeBoost(pindexPrev->nHeight);
}

void WarmupAiRegistry(const CChainParams& chainparams)
{
    LOCK(cs_airegistry);

    int tipHeight = chainActive.Height();
    if (tipHeight <= 0) {
        LogPrintf("AI Registry: Chain is empty, skipping warmup\n");
        return;
    }

    int startHeight = std::max(1, tipHeight - (AI_ATTESTATION_WINDOW * 2));
    LogPrintf("AI Registry: Warming up attestation cache from height %d to %d...\n", startHeight, tipHeight);

    int loadedAttestations = 0;
    int blocksScanned = 0;

    for (int h = startHeight; h <= tipHeight; h++) {
        CBlockIndex* pindex = chainActive[h];
        if (!pindex) continue;

        CBlock block;
        if (!ReadBlockFromDisk(block, pindex, chainparams.GetConsensus())) {
            LogPrintf("AI Registry: Warning - failed to read block at height %d during warmup\n", h);
            continue;
        }

        blocksScanned++;
        std::vector<AiAttestationRecord> blockAttestations;
        for (size_t i = 0; i < block.vtx.size(); i++) {
            const CTransaction& tx = block.vtx[i];
            for (size_t j = 0; j < tx.vout.size(); j++) {
                AiAttestationRecord rec;
                if (ExtractAiAttestation(tx.vout[j], rec)) {
                    rec.txid = tx.GetHash();
                    rec.blockHeight = h;
                    rec.blockTime = pindex->GetBlockTime();
                    blockAttestations.push_back(rec);
                    loadedAttestations++;
                }
            }
        }

        if (!blockAttestations.empty()) {
            mapHeightToAttestations[h] = blockAttestations;
        }
    }

    int activeBoost = GetActiveAiStakeBoost(tipHeight);
    LogPrintf("AI Registry: Warmup complete. Scanned %d blocks, loaded %d attestations. Active PoUS Stake Boost at height %d: +%d%%\n",
              blocksScanned, loadedAttestations, tipHeight, activeBoost);
}
