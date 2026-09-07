// Copyright (c) 2026 The Quavence developers
// Licensed under the Business Source License 1.1 (BSL-1.1)
// See LICENSE-ADDITIONS in the root of this repository

#include "airegistry.h"
#include "util.h"
#include "main.h"
#include "chainparams.h"
#include "script/script.h"
#include "script/standard.h"
#include "base58.h"
#include "pubkey.h"
#include <algorithm>
#include <boost/variant.hpp>

// ─── Internal state ──────────────────────────────────────────────────────────

static CCriticalSection cs_airegistry;

// height → QVAI attestation records (authorized Hub only)
static std::map<int, std::vector<AiAttestationRecord> > mapHeightToAttestations;

// height → {workerKeyID → credit_count} (authorized Pool QVRE TX only)
static std::map<int, std::map<CKeyID, uint32_t> > mapHeightToWorkerCredits;

// ─── Internal helpers ────────────────────────────────────────────────────────

static bool GetKeyIDFromScript(const CScript& script, CKeyID& out)
{
    CTxDestination dest;
    if (!ExtractDestination(script, dest)) return false;
    const CKeyID* k = boost::get<CKeyID>(&dest);
    if (!k) return false;
    out = *k;
    return true;
}

// Extract pubkey from scriptSig (P2PKH format: <sig> <pubkey>)
// Fully in RAM - zero UTXO lookup required.
// Works during ConnectBlock and WarmupAiRegistry (even if old UTXOs were already spent).
static bool TxSpendsFromKeyID(const CTransaction& tx, const CKeyID& authorizedID)
{
    for (size_t i = 0; i < tx.vin.size(); i++) {
        const CTxIn& txin = tx.vin[i];
        CScript::const_iterator pc = txin.scriptSig.begin();
        opcodetype opcode;
        std::vector<unsigned char> data;

        // P2PKH scriptSig: <sig> <pubkey>
        // First push is signature, skip it
        if (!txin.scriptSig.GetOp(pc, opcode, data)) continue;
        // Second push is public key
        if (!txin.scriptSig.GetOp(pc, opcode, data)) continue;

        CPubKey pubKey(data);
        if (!pubKey.IsValid()) continue;

        // Hash160(pubkey) == authorizedID ?
        if (pubKey.GetID() == authorizedID)
            return true;
    }
    LogPrint("airegistry", "TxSpendsFromKeyID: no matching P2PKH pubkey found for tx %s\n", tx.GetHash().ToString());
    return false;
}

static void PruneOldEntries(int currentHeight)
{
    int pruneBelow = currentHeight - (AI_ATTESTATION_WINDOW * 2);
    while (!mapHeightToAttestations.empty() &&
           mapHeightToAttestations.begin()->first < pruneBelow)
        mapHeightToAttestations.erase(mapHeightToAttestations.begin());
    while (!mapHeightToWorkerCredits.empty() &&
           mapHeightToWorkerCredits.begin()->first < pruneBelow)
        mapHeightToWorkerCredits.erase(mapHeightToWorkerCredits.begin());
}

// ─── Parsing ─────────────────────────────────────────────────────────────────

bool ExtractAiAttestation(const CTxOut& out, AiAttestationRecord& record)
{
    const CScript& script = out.scriptPubKey;
    if (script.empty() || script[0] != OP_RETURN) return false;

    CScript::const_iterator pc = script.begin() + 1;
    opcodetype opcode;
    std::vector<unsigned char> data;
    if (!script.GetOp(pc, opcode, data) || data.size() < 44) return false;

    if (data[0] != AI_MAGIC[0] || data[1] != AI_MAGIC[1] ||
        data[2] != AI_MAGIC[2] || data[3] != AI_MAGIC[3]) return false;

    // Strict versioning
    switch (data[4]) {
        case 0x01:
            if (data.size() != 44) return false;
            break;
        default:
            return false; // unknown version - reject
    }

    record.version        = data[4];
    record.taskType       = data[5];
    record.consensusHash  = uint256(std::vector<unsigned char>(data.begin() + 6, data.begin() + 38));
    record.workerCount    = data[38];
    record.agreementRatio = data[39];
    record.refBlockHeight = (uint32_t(data[40]) << 24) | (uint32_t(data[41]) << 16) |
                            (uint32_t(data[42]) << 8)  | uint32_t(data[43]);
    return true;
}

bool HasPoUSRewardMarker(const CTransaction& tx)
{
    for (size_t i = 0; i < tx.vout.size(); i++) {
        const CTxOut& vout = tx.vout[i];
        const CScript& s = vout.scriptPubKey;
        if (s.size() < 7 || s[0] != OP_RETURN) continue;
        CScript::const_iterator pc = s.begin() + 1;
        opcodetype op;
        std::vector<unsigned char> data;
        if (!s.GetOp(pc, op, data) || data.size() < 5) continue;
        if (data[0] == QVRE_MAGIC[0] && data[1] == QVRE_MAGIC[1] &&
            data[2] == QVRE_MAGIC[2] && data[3] == QVRE_MAGIC[3] &&
            data[4] == 0x01)
            return true;
    }
    return false;
}

// ─── Authorization ───────────────────────────────────────────────────────────

bool IsAuthorizedAiHubTx(const CTransaction& tx)
{
    const CKeyID& id = Params().GetConsensus().aiHubKeyID;
    if (id.IsNull()) return false;
    return TxSpendsFromKeyID(tx, id);
}

bool IsAuthorizedAiPoolTx(const CTransaction& tx)
{
    const CKeyID& id = Params().GetConsensus().aiPoolKeyID;
    if (id.IsNull()) return false;
    return TxSpendsFromKeyID(tx, id);
}

bool IsValidAiAttestationTx(const CTransaction& tx)
{
    if (!IsAuthorizedAiHubTx(tx)) return false;
    for (size_t i = 0; i < tx.vout.size(); i++) {
        AiAttestationRecord rec;
        if (ExtractAiAttestation(tx.vout[i], rec)) return true;
    }
    return false;
}

bool IsValidPoUSRewardTx(const CTransaction& tx)
{
    return IsAuthorizedAiPoolTx(tx) && HasPoUSRewardMarker(tx);
}

// ─── Registry lifecycle ──────────────────────────────────────────────────────

void RegisterAiAttestationsInBlock(const CBlock& block, int nHeight, int64_t nTime)
{
    LOCK(cs_airegistry);

    // Pass 1: QVAI attestations from authorized Hub
    std::vector<AiAttestationRecord> atts;
    for (size_t i = 0; i < block.vtx.size(); i++) {
        const CTransaction& tx = block.vtx[i];
        if (!IsValidAiAttestationTx(tx)) continue;
        for (size_t j = 0; j < tx.vout.size(); j++) {
            AiAttestationRecord rec;
            if (ExtractAiAttestation(tx.vout[j], rec)) {
                rec.txid = tx.GetHash();
                rec.blockHeight = nHeight;
                rec.blockTime   = nTime;
                atts.push_back(rec);
                LogPrintf("AI Registry: QVAI at height=%d tx=%s workers=%d\n",
                          nHeight, rec.txid.ToString(), rec.workerCount);
            }
        }
    }
    if (!atts.empty()) mapHeightToAttestations[nHeight] = atts;

    // Pass 2: QVRE reward TX - credits per worker key
    std::map<CKeyID, uint32_t> credits;
    for (size_t i = 0; i < block.vtx.size(); i++) {
        const CTransaction& tx = block.vtx[i];
        if (!IsValidPoUSRewardTx(tx)) continue;
        for (size_t j = 0; j < tx.vout.size(); j++) {
            const CTxOut& vout = tx.vout[j];
            if (!vout.scriptPubKey.empty() && vout.scriptPubKey[0] == OP_RETURN)
                continue; // skip OP_RETURN outputs
            CKeyID workerID;
            if (GetKeyIDFromScript(vout.scriptPubKey, workerID)) {
                credits[workerID]++;
                LogPrintf("AI Registry: QVRE credit -> %s at height=%d\n",
                          CBitcoinAddress(workerID).ToString(), nHeight);
            }
        }
    }
    if (!credits.empty()) mapHeightToWorkerCredits[nHeight] = credits;

    PruneOldEntries(nHeight);
}

void UnregisterAiAttestationsInBlock(const CBlock& block, int nHeight)
{
    LOCK(cs_airegistry);
    mapHeightToAttestations.erase(nHeight);
    mapHeightToWorkerCredits.erase(nHeight);
}

// ─── Query API ───────────────────────────────────────────────────────────────

int GetAiAttestationsCountInWindow(int currentHeight)
{
    LOCK(cs_airegistry);
    int minH = std::max(0, currentHeight - AI_ATTESTATION_WINDOW);
    int count = 0;
    for (std::map<int, std::vector<AiAttestationRecord> >::const_iterator it = mapHeightToAttestations.lower_bound(minH);
         it != mapHeightToAttestations.end() && it->first <= currentHeight; ++it) {
        for (size_t i = 0; i < it->second.size(); i++) {
            const AiAttestationRecord& r = it->second[i];
            if (r.workerCount >= 1 && r.agreementRatio >= 178)
                count++;
        }
    }
    return count;
}

uint32_t GetWorkerCreditsInWindow(const CKeyID& workerID, int currentHeight)
{
    LOCK(cs_airegistry);
    int minH = std::max(0, currentHeight - AI_ATTESTATION_WINDOW);
    uint32_t total = 0;
    for (std::map<int, std::map<CKeyID, uint32_t> >::const_iterator it = mapHeightToWorkerCredits.lower_bound(minH);
         it != mapHeightToWorkerCredits.end() && it->first <= currentHeight; ++it) {
        std::map<CKeyID, uint32_t>::const_iterator wit = it->second.find(workerID);
        if (wit != it->second.end())
            total += wit->second;
    }
    return total;
}

int GetWorkerPoUSBoost(uint32_t credits)
{
    if (credits == 0)  return 0;
    if (credits < 6)   return 20;  // 1-5 tasks -> +20%
    if (credits < 21)  return 30;  // 6-20 tasks -> +30%
    if (credits < 51)  return 40;  // 21-50 tasks -> +40%
    return 50;                     // 51+         -> +50%
}

int GetActiveAiStakeBoost(int currentHeight)
{
    // Global metric for RPC / logging (not used directly in consensus stake validation)
    int q = GetAiAttestationsCountInWindow(currentHeight);
    if (q < MIN_ATTESTATIONS_FOR_BOOST) return 0;
    return std::min(MAX_AI_BOOST_PERCENT,
                    BASE_AI_BOOST_PERCENT + std::min(30, q * 5));
}

int GetAiStakeBoost(const CScript& stakeScript, const CBlockIndex* pindexPrev)
{
    if (!pindexPrev) return 0;

    // Extract staker CKeyID directly from scriptPubKey (zero disk I/O)
    CKeyID stakeKeyID;
    if (!GetKeyIDFromScript(stakeScript, stakeKeyID))
        return 0;

    uint32_t credits = GetWorkerCreditsInWindow(stakeKeyID, pindexPrev->nHeight);
    return GetWorkerPoUSBoost(credits);
}

// ─── Warmup on startup ───────────────────────────────────────────────────────

void WarmupAiRegistry(const CChainParams& chainparams)
{
    LOCK(cs_airegistry);
    int tipHeight = chainActive.Height();
    if (tipHeight <= 0) {
        LogPrintf("AI Registry: chain empty, skipping warmup\n");
        return;
    }
    int startHeight = std::max(1, tipHeight - (AI_ATTESTATION_WINDOW * 2));
    LogPrintf("AI Registry: warming up from height %d to %d...\n", startHeight, tipHeight);

    int nAtts = 0, nCredits = 0;
    for (int h = startHeight; h <= tipHeight; h++) {
        CBlockIndex* pindex = chainActive[h];
        if (!pindex) continue;
        CBlock block;
        if (!ReadBlockFromDisk(block, pindex, chainparams.GetConsensus())) continue;
        int64_t nTime = pindex->GetBlockTime();

        std::vector<AiAttestationRecord> atts;
        for (size_t i = 0; i < block.vtx.size(); i++) {
            const CTransaction& tx = block.vtx[i];
            if (!IsValidAiAttestationTx(tx)) continue;
            for (size_t j = 0; j < tx.vout.size(); j++) {
                AiAttestationRecord rec;
                if (ExtractAiAttestation(tx.vout[j], rec)) {
                    rec.txid = tx.GetHash();
                    rec.blockHeight = h;
                    rec.blockTime   = nTime;
                    atts.push_back(rec);
                    nAtts++;
                }
            }
        }
        if (!atts.empty()) mapHeightToAttestations[h] = atts;

        std::map<CKeyID, uint32_t> credits;
        for (size_t i = 0; i < block.vtx.size(); i++) {
            const CTransaction& tx = block.vtx[i];
            if (!IsValidPoUSRewardTx(tx)) continue;
            for (size_t j = 0; j < tx.vout.size(); j++) {
                const CTxOut& vout = tx.vout[j];
                if (!vout.scriptPubKey.empty() && vout.scriptPubKey[0] == OP_RETURN) continue;
                CKeyID wid;
                if (GetKeyIDFromScript(vout.scriptPubKey, wid)) {
                    credits[wid]++;
                    nCredits++;
                }
            }
        }
        if (!credits.empty()) mapHeightToWorkerCredits[h] = credits;
    }
    LogPrintf("AI Registry: warmup done. Scanned %d blocks, loaded %d attestations, %d worker credits\n",
              tipHeight - startHeight + 1, nAtts, nCredits);
}
