# Quavence Core Node & PoUS All-in-One Wallet (QVNC)

[![Release](https://img.shields.io/badge/release-v15.1.9-blue.svg)](https://github.com/quavence/quavence-node/releases)
[![Consensus](https://img.shields.io/badge/consensus-PoS3.0%20%2B%20PoUS-emerald.svg)](https://quavence.com)
[![License](https://img.shields.io/badge/license-MIT%20%2F%20BSL--1.1-green.svg)](LICENSE-ADDITIONS)

Quavence (QVNC) is a decentralized Layer-1 DePIN AI compute network powered by **Proof-of-Useful-Stake (PoUS)**. It combines high-throughput PoS 3.0 staking with autonomous on-chain AI worker attestation, knowledge-base RAG verification, and real-time staking yield boosts.

### 🌐 Official Resources & Documentation
* **Website & DAO Hub:** [https://quavence.com](https://quavence.com)
* **Architecture Docs & Whitepaper:** [https://quavence.com/docs](https://quavence.com/docs)
* **Blockchain Explorer:** [https://explorer.quavence.com](https://explorer.quavence.com)
* **Bitcointalk Official ANN:** [Topic #5592414](https://bitcointalk.org/index.php?topic=5592414.0)
* **Discord Community:** [discord.gg/5c8jY9aCa7](https://discord.gg/5c8jY9aCa7)
* **X / Twitter:** [@QuavenceX](https://x.com/QuavenceX)

---

## Key Features

- **Integrated AI Worker:** Connects to local LLM engines (Ollama, LM Studio, OpenAI-compatible APIs) to process consensus audits, governance proposals, and verification tasks.
- **Proof-of-Useful-Stake (PoUS):** Stakers operating active AI workers receive dynamic staking boosts (up to **+50%**) based on verified task execution within the on-chain attestation registry.
- **On-Chain Fee Routing:** Native protocol-level fee allocation to decentralized security and compute reward pools.
- **Coin Control & Staking Optimization:** Built-in UTXO splitting and coin control for optimized staking weight.
- **Modular Targets:**
  - `quavenced`: Headless P2P daemon for servers, validators, and mining pools.
  - `quavence-cli`: Command-line RPC client.
  - `quavence-qt`: All-in-One GUI desktop wallet with integrated AI Worker and PoUS dashboard.

---

## 🏦 Genesis Allocation (50,000 QVNC)

At block 0 (Genesis), a fixed allocation of 50,000.00000000 QVNC was minted into 8 strictly defined, verifiable cold and active on-chain operational buckets. There is no hidden minting, private founder backdoor, or unverified supply.

| Bucket | Address | Amount (QVNC) | Mode | Purpose |
|---|---|:---:|:---:|---|
| Cold_Worker_Reward_Reserve | SP25CJVNG5CDrNhJqhzYqVmGYv24EJyqsQ | 13,000 | Cold | Worker reward reserve pool |
| Cold_Team_Reserve | Sa9YBWLrF6FjQwaXXcjpHyrGJ4YzLsiVX1 | 12,500 | Cold | Core team reserve (5% hardcap) |
| Cold_DAO_Treasury_Vault | SZjekg13eHqJKEcwt6P92A3XU8ETfZt8sP | 9,500 | Cold | DAO Treasury vault |
| Cold_Launch_Distribution | SXj6w7JWCXCF5JwERqYrY5xPLKrnp72tNk | 7,500 | Cold | Strategic launch distribution |
| Cold_Liquidity_Ops | SiyCrHSwuFSuKTW6S5iDc6e11eEJocAvbQ | 3,000 | Cold | Infrastructure & liquidity reserve |
| Cold_Ecosystem_Grants | SVU1wDNPJ6soi9eZCoyFBnkdWaZcWv5rcS | 2,000 | Cold | Ecosystem development grants |
| Active_Staking_Anchor | SMTGZc3cQVijVrcuy3awFiRm17vvk35gDa | 2,000 | Active | Initial consensus PoS anchor |
| Hot_DevFee_Float | SPdSBBkFNKop3auzvFjGJpaCAdVnMQFyyd | 500 | Hot | Hub operations & bounty payouts |
| **TOTAL** | **8 Genesis Buckets** | **50,000.00000000** | — | **100% Verifiable On-Chain** |

---

## 🪙 Supply Cap & Consensus Emission Model (200,000 QVNC)

Quavence enforces a strict mathematical supply ceiling of **200,000.00000000 QVNC** following the canonical Bitcoin Core UTXO emission architecture:

* **Geometric Decay Table (`src/consensus/subsidy_schedule.h`)**: The issuance schedule consists of 704 discrete decay eras of 500,000 blocks each. The cumulative sum of all era subsidies equals exactly `SUBSIDY_POS_CAP_UNITS` (200,000.00000000 QVNC), verified by test `subsidy_table_totals_to_pos_cap`.
* **Consensus Enforcement (`src/main.cpp:1788-1798`, `ConnectBlock:2731`)**: For any block, `GetProofOfStakeSubsidy(nHeight)` yields the strictly bounded era reward. Beyond the final era (> block 352,000,000), `GetProofOfStakeSubsidy` returns `0`. Any coinstake attempting to claim greater than `nFees + GetProofOfStakeSubsidy(nHeight)` is instantly rejected at consensus level with maximum ban penalty: `state.DoS(100, "bad-cs-amount")`.
* **State Invariant**: In accordance with Bitcoin Core consensus design, cumulative supply is governed by deterministic per-block limits rather than a mutable global state counter, ensuring total immunity against chain reorganization (reorg) state corruption.

---

## ⚙️ Protocol Coinstake Reward Split (Height 91,450+)

Starting at block height **91,450** (`nDevFundActivationHeight`), proof-of-stake coinstake rewards incorporate an automated 15% protocol funding split (`src/main.cpp:1825-1835`) designed to sustainably subsidize decentralized compute workers and network infrastructure:

* **Staker / Validator Share (85.0%)**: The vast majority (85%) of block rewards is always retained directly by the staking node.
* **AI Worker Compute Pool (4.5%)**: 30% of the protocol allocation (4.5% of total block reward) routes to `ScmZ5fYVTADyMcH11CXtf9iC9qVeRHA31M` to fund autonomous PoUS inference dividends for DePIN compute workers.
* **Core Infrastructure & Grants (10.5%)**: The remaining 70% of the protocol allocation (10.5% of total block reward) routes to `SXbKabuHh7xn3QuXF7DMG758D9j4rVcL6V` for server infrastructure, seed node hosting, and DAO ecosystem grants.
* **Activation Horizon**: Prior to height 91,450, 100% of the block subsidy is minted directly to the staking validator without deductions.

---

## 🛡️ Consensus Architecture & PoUS Oracle Governance

Quavence enforces a clear architectural separation between base L1 consensus and layer-2 compute attestation:

* **Layer-1 Core Consensus (100% Permissionless PoS 3.0)**: Block production, transaction validation, and peer-to-peer settlement are fully decentralized and permissionless. Any node holding a non-zero UTXO balance can stake and validate blocks without allowlists, registration, or third-party authorization.
* **PoUS Compute Attestation Layer (Oracle-Anchored Boost)**: Dynamic PoUS staking weight multipliers (+20% to +50%) and compute dividend routing are verified via cryptographic task attestations (`src/airegistry.cpp`). To prevent Sybil exploitation and fake compute spam during Phase 01 Bootstrap, attestation records require cryptographic authorization anchored by the protocol oracle keys (`aiHubKeyID`, `aiPoolKeyID`), with strict signature enforcement active since block 6,500.
* **Decentralization Roadmap**: In accordance with Phase 02/03 milestones, oracle attestation authority transitions to threshold multisig validator sets governed by on-chain DAO proposals.

---

## Building from Source (Linux / Ubuntu / Debian)

### 1. Install Dependencies

```bash
sudo apt-get update
sudo apt-get install -y build-essential libtool autotools-dev automake pkg-config \
    libssl-dev libevent-dev bsdmainutils python3 \
    libboost-system-dev libboost-filesystem-dev libboost-chrono-dev \
    libboost-program-options-dev libboost-test-dev libboost-thread-dev \
    libqt5gui5 libqt5core5a libqt5dbus5 qttools5-dev qttools5-dev-tools \
    libprotobuf-dev protobuf-compiler libqrencode-dev libdb5.3++-dev libdb5.3++
```

### 2. Configure and Build

```bash
# Generate build scripts
./autogen.sh

# Create build directory
mkdir build-linux && cd build-linux

# Configure (builds daemon, cli, and Qt wallet)
../configure --prefix=/ --disable-bench --disable-tests --enable-wallet --with-gui=qt5

# Compile (adjust -j flag to your CPU core count)
make -j4
```

To compile only the headless server daemon without GUI dependencies:

```bash
../configure --prefix=/ --disable-bench --disable-tests --enable-wallet --without-gui
make -j4
```

Built binaries will be located in `src/`:
- `src/quavenced`
- `src/quavence-cli`
- `src/qt/quavence-qt`

---

## Node Configuration

Create the configuration file at `~/.quavence/quavence.conf`:

```ini
server=1
listen=1
daemon=1
rpcuser=your_rpc_username
rpcpassword=your_secure_rpc_password
rpcallowip=127.0.0.1
rpcport=15715
port=15714
staking=1
```

### Running the Node

```bash
# Start daemon
./src/quavenced -daemon

# Check blockchain info via CLI
./src/quavence-cli getblockchaininfo

# Check staking status
./src/quavence-cli getstakinginfo
```

---

## AI Worker Setup (GUI Wallet)

1. Launch the All-in-One Wallet: `./src/qt/quavence-qt`
2. Navigate to the **AI Worker** tab.
3. Enter your **Node Token** (generated in the Quavence Dashboard).
4. Set the **Inference Endpoint** (e.g., `http://127.0.0.1:11434` for Ollama or `http://127.0.0.1:1234/v1` for LM Studio).
5. Click **Start Worker** to connect, process verification tasks, and activate the PoUS Staking Boost.

---

## License

- Quavence Blockchain Core, P2P Node, and Qt Wallet are released under the terms of the **MIT License**. See [COPYING](COPYING) for details.
- The AI Worker subsystem, Compute Agents, and Attestation protocols are Copyright © Quavence DAO, licensed under the **Business Source License 1.1 (BSL-1.1)**. See [LICENSE-ADDITIONS](LICENSE-ADDITIONS) for details.
