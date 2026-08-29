# Quavence Proof-of-Useful-Stake (PoUS) Consensus Architecture

## 1. Overview & Problem Statement

Traditional Proof-of-Stake (PoS) blockchains reward token holders solely for locking collateral. This creates the **free-rider dilemma**: passive stakers earn the same yield as active contributors who provide computational power, participate in governance, or secure the decentralized ecosystem.

**Proof-of-Useful-Stake (PoUS) v1.1** introduces an active compute attestation mechanism. Stakers who operate verified AI worker nodes (processing decentralized AI inference, RAG knowledge verification, proposal summarization, and security reviews) receive a **Tiered Staking Boost (+20% to +50%)** on top of standard block rewards.

```
┌───────────────────────────────┐
│        AI Worker Node         │
│  (LM Studio / Ollama / GPU)   │
└───────────────┬───────────────┘
                │ Submit Task Proof & Attestation
                ▼
┌───────────────────────────────┐
│     AI Consensus Engine       │
│  (Multi-worker Consensus)     │
└───────────────┬───────────────┘
                │ Authorized QVRE Reward Output
                ▼
┌───────────────────────────────┐
│   On-Chain UTXO Transaction   │
│   (aiPoolKeyID Destination)   │
└───────────────┬───────────────┘
                │ Verified within 1440-block rolling window
                ▼
┌───────────────────────────────┐
│     PoUS Stake Kernel         │
│  (+20%..+50% Tier Multiplier) │
└───────────────────────────────┘
```

---

## 2. Dynamic Tier Matrix

PoUS dynamically recalculates node staking tiers over a rolling **1,440-block window** (~24 hours on 60-second block targets):

| Tier Level | Requirement (Rolling 1440 Blocks) | Staking Boost Multiplier | Effective Network Status |
|---|---|---|---|
| **Tier 3 (Max Boost)** | ≥ 10 Verified Tasks completed | **+50%** Staking Weight | `Active (Tier 3)` |
| **Tier 2 (High Boost)** | 5 – 9 Verified Tasks completed | **+35%** Staking Weight | `Active (Tier 2)` |
| **Tier 1 (Base Boost)** | 1 – 4 Verified Tasks completed | **+20%** Staking Weight | `Active (Tier 1)` |
| **Standby** | 0 Verified Tasks completed | **0%** (Standard PoS Base) | `Standby` |

---

## 3. Cryptographic Verification & Memory Architecture

To prevent sybil attacks and maintain high transaction throughput, PoUS employs three core architectural pillars:

### A. Authorized `aiPoolKeyID` Source Tagging
Stake boost eligibility is strictly derived from UTXO outputs created by the network's authorized AI pool script hash:
```cpp
// Authorized pool key ID for compute attestations
static const CKeyID aiPoolKeyID = CBitcoinAddress("ScmZ5fYVTADyMcH11CXtf9iC9qVeRHA31M").GetKeyID();
```
Only transactions bearing the OP_RETURN `QVRE` attestation marker and spending from `aiPoolKeyID` increment the staker's rolling task counter.

### B. 100% In-Memory (RAM) Pubkey Extraction
During stake kernel validation (`CheckStakeKernelHash`), the node resolves the staker's public key directly from `scriptSig` in memory via `TxSpendsFromKeyID`, avoiding disk I/O operations and ensuring fast block verification even during chain warmup.

### C. `CStakeCache` Optimization
Staking caches preserve pre-validated `scriptPubKey` associations, ensuring that recurring staking loops execute in constant time $O(1)$.

---

## 4. Supported AI Worker Tasks

1. **`TASK_RAG_IDLE_VERIFICATION`**: Autonomous knowledge base verification, Q&A pair extraction, and embedding coherence checks.
2. **`TASK_BOUNTY_COMPOSER_TURN`**: Spec synthesis and rubric formulation.
3. **`TASK_SUMMARY`**: Multi-perspective proposal abstraction.
4. **`TASK_RISK_FLAGS`**: Smart contract & parameter security auditing (2-of-3 consensus).
5. **`TASK_BOUNTY_SUBMISSION_SCREEN`**: Anti-spam, moderation, and plagiarism screening.
6. **`TASK_PROPOSAL_PAYOUT_REVIEW`**: Multi-agent Treasury Guard execution reviews.

---

## 5. Security Invariants

* **No Cold-Staker Penalty:** Non-participating stakers never lose principal tokens; they simply remain in `0% (Standby)` state without the yield multiplier.
* **Deterministic Rollback:** If a chain reorganization occurs, the 1440-block sliding window automatically rewinds without orphan state corruption.
* **Anti-Grinding:** PoUS boosts modify the stake target threshold deterministically based on timestamp and stake kernel hash without introducing RNG grinding vectors.
