# Quavence Core Release Notes — v15.0.0 (Protocol 70015)

Quavence Core version `15.0.0` is now officially available. This major release delivers **Proof-of-Useful-Stake (PoUS) Stake Boost v1.1**, full integration of the **Autonomous AI Worker All-in-One GUI**, zero-disk-I/O RAM pubkey validation, and comprehensive protocol optimizations.

---

## 🌟 Major Highlights & Architecture Upgrades

### 1. PoUS Stake Boost v1.1 Consensus
- **Tiered Staking Boosts (+20% .. +50%):** Stakers actively contributing compute to the decentralized network through authorized AI workers receive a dynamic tier boost on their staking weight within a rolling 1440-block window.
- **Standby State (0% Boost):** Non-active stakers receive standard PoS base rewards (`0% (Standby)` boost), permanently resolving the free-rider vulnerability.
- **Cryptographic Attestation:** Boost verification is strictly bound to authorized `aiPoolKeyID` reward outputs (`QVRE` script marker) generated upon valid task proof submission.

### 2. High-Performance Consensus & Memory Optimization
- **Zero-I/O Pubkey Extraction:** Full RAM-based public key extraction directly from `scriptSig` in `TxSpendsFromKeyID`, eliminating disk reads during stake kernel validation and node warmup.
- **`CStakeCache` Architecture:** Direct forwarding of `scriptPubKey` into `CheckStakeKernelHash`, streamlining stake validation across high-frequency PoS block generation.

### 3. PoUS All-in-One Qt GUI Wallet
- Integrated **AI Worker Tab** directly inside the Qt wallet, supporting seamless connection to local inference backends (LM Studio, Ollama, OpenAI-compatible APIs).
- Native support for **`TASK_RAG_IDLE_VERIFICATION`** (automated Q&A pair generation, embedding verification, chunk coherence) alongside governance and bounty verification tasks.
- Real-time **Accrued Rewards Overview** displaying confirmed and unconfirmed QVNC worker earnings with sub-second synchronization.

### 4. Windows PE Metadata & Codebase Sanitation
- Executable Windows PE version metadata updated to `15.0.0.0` across all Windows binaries (`quavence-qt`, `quavenced`, `quavence-cli`).
- Comprehensive codebase standardization: 100% English documentation, clean header structures, and removal of internal legacy references.

---

## 📋 Compatibility & Upgrade Instructions

- **Protocol Version:** `70015`
- **Client Version:** `150000`
- **Hard Fork Notice:** This release is mandatory for all network participants to track consensus validation with PoUS Stake Boost v1.1.

### Upgrading from Previous Versions:
1. Shut down your running node or Qt wallet (`quavence-cli stop` or Exit GUI).
2. Replace existing executables with `v15.0.0` binaries.
3. Restart `quavenced` or `quavence-qt`. Chainstate re-index is **not** required.

---

## 📦 Released Binaries

- `quavence-qt-pous-allinone-v15.0.exe` — Windows PoUS All-in-One GUI Wallet + AI Worker
- `quavence-qt-standard-v15.0.exe` — Windows Classic Qt GUI Wallet
- `quavenced-v15.0.exe` / `quavence-cli-v15.0.exe` — Windows Headless Node Daemon & CLI
- `quavence-qt-pous-allinone-v15.0` — Linux x86_64 PoUS All-in-One GUI Wallet
- `quavence-qt-standard-v15.0` — Linux x86_64 Classic Qt GUI Wallet
- `quavenced-v15.0` / `quavence-cli-v15.0` — Linux x86_64 Headless Node Daemon & CLI
