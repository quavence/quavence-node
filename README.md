# Quavence Core Node & PoUS All-in-One Wallet (QVNC)

[![Release](https://img.shields.io/badge/release-v15.1.0-blue.svg)](https://github.com/quavence/quavence-node/releases)
[![Consensus](https://img.shields.io/badge/consensus-PoS3.0%20%2B%20PoUS-emerald.svg)](https://quavence.com)
[![Tor v3](https://img.shields.io/badge/p2p-Dual--Stack%20Tor%20v3-purple.svg)](https://quavence.com)
[![License](https://img.shields.io/badge/license-MIT%20%2F%20BSL--1.1-green.svg)](LICENSE-ADDITIONS)

Quavence (QVNC) is a decentralized Layer-1 DePIN AI compute network powered by **Proof-of-Useful-Stake (PoUS)**. It combines high-throughput PoS 3.0 staking with autonomous on-chain AI worker attestation, knowledge-base RAG verification, and real-time staking yield boosts.

---

## Key Features

- **Dual-Stack Censorship-Resistant P2P (Native Tor v3 & Clearnet IPv4):**
  - Native Tor v3 Hidden Service support (56-character ED25519 addresses with SHA3-256 checksums) integrated directly into P2P protocol 70015 and address discovery (`addrman`).
  - Zero-config standalone Tor daemon bundled into desktop clients (`quavence-qt`) — instant NAT traversal and ISP censorship evasion without manual port-forwarding.
  - Permanent on-chain Tor v3 seednodes embedded directly in `chainparams.cpp` ensuring indestructible peer discovery across global network partitions.
- **Integrated AI Worker:** Connects to local LLM engines (Ollama, LM Studio, OpenAI-compatible APIs) to process consensus audits, governance proposals, and verification tasks.
- **Proof-of-Useful-Stake (PoUS):** Stakers operating active AI workers receive dynamic staking boosts (up to **+50%**) based on verified task execution within the on-chain attestation registry.
- **On-Chain Fee Routing:** Native protocol-level fee allocation to decentralized security and compute reward pools.
- **Coin Control & Staking Optimization:** Built-in UTXO splitting and coin control for optimized staking weight.
- **Modular Targets:**
  - `quavenced`: Headless P2P daemon for servers, validators, and mining pools.
  - `quavence-cli`: Command-line RPC client.
  - `quavence-qt`: All-in-One GUI desktop wallet with integrated AI Worker, PoUS dashboard, and embedded Tor daemon.

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

# Compile (adjust -j flag to your CPU core count, e.g. -j2)
make -j2
```

To compile only the headless server daemon without GUI dependencies:

```bash
../configure --prefix=/ --disable-bench --disable-tests --enable-wallet --without-gui
make -j2
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
rpcport=27715
port=27714
staking=1

# Optional Dual-Stack Tor configuration
onion=127.0.0.1:9050
listenonion=1
```

### Running the Node

```bash
# Start daemon
./src/quavenced -daemon

# Check blockchain info via CLI
./src/quavence-cli getinfo

# Check network connections & Tor v3 peers
./src/quavence-cli getnetworkinfo
./src/quavence-cli getpeerinfo

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