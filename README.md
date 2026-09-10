# Quavence Core Node & PoUS All-in-One Wallet (QVNC)

[![Release](https://img.shields.io/badge/release-v15.1.0-blue.svg)](https://github.com/quavence/quavence-node/releases)
[![Consensus](https://img.shields.io/badge/consensus-PoS3.0%20%2B%20PoUS%20v1.1-emerald.svg)](https://quavence.com)
[![Tor v3](https://img.shields.io/badge/p2p-Dual--Stack%20Tor%20v3-purple.svg)](https://quavence.com)
[![License](https://img.shields.io/badge/license-MIT%20%2F%20BSL--1.1-green.svg)](LICENSE-ADDITIONS)

Quavence (QVNC) is a decentralized Layer-1 DePIN AI compute network powered by **Proof-of-Useful-Stake (PoUS)**. It combines high-throughput PoS 3.0 staking with autonomous on-chain AI worker attestation, knowledge-base RAG verification, on-chain PoUS Glyphs, and real-time staking yield boosts.

---

## Key Features

- **Dual-Stack Censorship-Resistant P2P (Native Tor v3 & Clearnet IPv4):**
  - Native Tor v3 Hidden Service support (56-character ED25519 addresses with SHA3-256 checksums) integrated directly into P2P protocol 70015 and peer address discovery (`addrman`).
  - Zero-config standalone Tor daemon bundled into desktop clients (`quavence-qt`) — instant NAT traversal and ISP censorship evasion without manual port-forwarding.
  - Permanent on-chain Tor v3 seednodes embedded directly in `chainparams.cpp` ensuring indestructible peer discovery across global network partitions.
- **Proof-of-Useful-Stake (PoUS v1.1 Consensus):**
  - Dynamic staking weight multiplier (up to **+50%**) based on verified computational tasks recorded in the on-chain attestation registry.
  - Non-active stakers receive standard base rewards (`0% Standby`), permanently eliminating free-rider vulnerabilities.
- **Integrated AI Worker Client:** Connects to local LLM engines (Ollama, LM Studio, OpenAI-compatible APIs) to process consensus audits, governance proposals, and verification tasks.
- **PoUS Glyphs & Carrier Protection:** Protocol-level carrier UTXO protection ensuring non-fungible on-chain state carriers are staking-immune and protected against accidental spending.
- **On-Chain Protocol Fee Routing:** Native protocol-level fee allocation continuously funding the decentralized AI Worker reward pool (30% of DevFee).
- **High-Performance Memory Architecture:** Zero-I/O RAM pubkey extraction and `CStakeCache` streamlining high-frequency PoS 3.0 block validation.

---

## Desktop Client Editions

The Quavence desktop client is distributed across **4 modular editions**, tailored to distinct network participant profiles — from institutional custody to sovereign private AI nodes:

| Edition | Included Modules | Target Audience | Key Advantages |
|---|---|---|---|
| **Enterprise Edition** | Wallet + PoUS Glyph Carrier | Funds, exchanges, institutions, custodians | Maximum stability and compliance purity (EDR / antivirus-safe). No background compute or stealth daemons. Hardware-grade carrier UTXO protection against accidental burning. |
| **DePIN Compute Edition** | Wallet + AI Worker + Glyph | Home miners, GPU owners, AI workers | Built-in local inference runner and automated participation in the PoUS dividend pool (**+20% to +50%** staking weight multiplier). |
| **Privacy Edition** | Wallet + Built-in Tor + Glyph | Cypherpunks, privacy-conscious holders, censored regions | **Zero-Config Tor**: silent, embedded onion routing daemon launched directly from the binary. Bypasses ISP firewalls/NAT, fully anonymizing the P2P node IP. |
| **Sovereign Ultimate** | Wallet + AI + Tor + Glyph | Full network validators, sovereign nodes | Complete ecosystem stack: useful DePIN computation inside an encrypted private network with on-chain attestations and maximum staking yield. |

### Feature Comparison Matrix

| Capability | Enterprise | DePIN Compute | Privacy | Sovereign Ultimate |
|---|:---:|:---:|:---:|:---:|
| L1 PoS Wallet & Staking | ✅ | ✅ | ✅ | ✅ |
| PoUS Glyph Carrier Protection | ✅ | ✅ | ✅ | ✅ |
| Decentralized AI Worker | ❌ | ✅ | ❌ | ✅ |
| AI Worker Pool Dividends (+20%..+50%) | ❌ | ✅ | ❌ | ✅ |
| Built-in Tor Daemon (Zero-Config) | ❌ | ❌ | ✅ | ✅ |
| Hidden Services `.onion` (P2P Stealth) | ❌ | ❌ | ✅ | ✅ |
| Corporate Compliance (EDR-Safe) | ✅ | ✅ | ⚠️ *(Tor)* | ⚠️ *(Tor)* |
| Supported OS | Windows / Linux | Windows / Linux | Windows / Linux | Windows / Linux |

---

## Release History & Version Documentation

Official release notes for all published versions are maintained in the [`doc/release-notes/`](doc/release-notes/) directory:

- [**Quavence Core v15.1.0 Release Notes**](doc/release-notes/release-notes-15.1.0.md)
  - Native Tor v3 Hidden Services (BIP155 / ED25519-V3 / SHA3-256)
  - Standalone All-in-One Windows binary with embedded Tor daemon
  - Permanent onion seednodes in `chainparams.cpp`
  - Protocol 70015 compatibility & Windows PE metadata
- [**Quavence Core v15.0.0 Release Notes**](doc/release-notes/release-notes-15.0.0.md)
  - PoUS Stake Boost v1.1 consensus implementation
  - Zero-I/O RAM pubkey extraction & `CStakeCache`
  - Integrated AI Worker tab with live reward telemetry

### Official SHA-256 Checksums (v15.1.0)

```text
Windows (x86_64):
fc6207b56e345465ff1a6c98e8b0532f39ae748a8ccaf4c95c0395393f9d61c2  quavence-qt-pous-allinone-v15.1.exe
0a919961d176d8d8a780ce962a5a7399333314caa5fe5c9ce9e96a9e0084f633  quavence-qt-standard-v15.1.exe
7d9972ae96f00bc895c0a0795d68c47e0dab73c6a189111a76d0ef6df3badcf4  quavence-qt-clearnet-v15.1.exe
a7d8026b7bdee6fb952dcb9af07134c3647b16fa7eb53c66c6fdede379341b15  quavence-qt-pous-worker-clearnet-v15.1.exe
928df2b34872919bdc00e564a2bc14f6cd4d997cfe545be8e86349b81d59ef82  quavenced-v15.1.exe
f2f539f4b02790e603d85b44263404511741d1ffa43616ac76787cfb789cef62  quavence-cli-v15.1.exe

Linux (Ubuntu / Debian x86_64):
1635ddb2c20ab840710bdcfdcf701be87c60e21959b20c8211375ea32d13d348  quavence-qt-pous-allinone-v15.1
0e20560cd3674174babd3fc4c81384c802fbc39b9565517d96caf0ad362b645d  quavence-qt-standard-v15.1
be3766312256e655d6983d0a144ef8673f756d12649a36e145c15d69294ac4f3  quavenced-v15.1
35d4750db3fa2c936ddd4af62bdff9fb2929d3569fd5d1d438a221df8bfcf0d6  quavence-cli-v15.1
```

---

## Building from Source (Linux / Ubuntu / Debian)

### 1. Install Build Dependencies

```bash
sudo apt-get update
sudo apt-get install -y build-essential libtool autotools-dev automake pkg-config \
    libssl-dev libevent-dev bsdmainutils python3 \
    libboost-system-dev libboost-filesystem-dev libboost-chrono-dev \
    libboost-program-options-dev libboost-test-dev libboost-thread-dev \
    libqt5gui5 libqt5core5a libqt5dbus5 qttools5-dev qttools5-dev-tools \
    libprotobuf-dev protobuf-compiler libqrencode-dev libdb5.3++-dev libdb5.3++
```

### 2. Configure and Compile

```bash
# Generate build scripts
./autogen.sh

# Create build directory
mkdir build-linux && cd build-linux

# Configure (builds headless daemon, CLI, and Qt wallet)
../configure --prefix=/ --disable-bench --disable-tests --enable-wallet --with-gui=qt5

# Compile (adjust -j flag to your available CPU threads)
make -j$(nproc)
```

To compile only the headless server daemon without GUI dependencies:

```bash
../configure --prefix=/ --disable-bench --disable-tests --enable-wallet --without-gui
make -j$(nproc)
```

Built binaries:
- `src/quavenced` — Headless daemon
- `src/quavence-cli` — RPC command-line interface
- `src/qt/quavence-qt` — GUI desktop wallet

---

## Node Configuration

Create or edit your configuration file at `~/.quavence/quavence.conf` (Linux) or `%APPDATA%\Quavence\quavence.conf` (Windows):

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

### Running the Node & CLI Commands

```bash
# Start daemon in background
./src/quavenced -daemon

# Check blockchain info via CLI
./src/quavence-cli getinfo

# Check network connectivity & Tor v3 peers
./src/quavence-cli getnetworkinfo
./src/quavence-cli getpeerinfo

# Check staking status & weight
./src/quavence-cli getstakinginfo
```

---

## Ecosystem Links

- **Platform Hub:** [https://quavence.com](https://quavence.com)
- **Live Blockchain Explorer:** [https://explorer.quavence.com](https://explorer.quavence.com)
- **Official GitHub Organization:** [https://github.com/quavence](https://github.com/quavence)
- **Official Discord Community:** [https://discord.gg/5c8jY9aCa7](https://discord.gg/5c8jY9aCa7)
- **Official Twitter (X):** [@QuavenceX](https://x.com/QuavenceX)

---

## License

- The Quavence Blockchain Core, P2P network daemon, RPC interfaces, and Qt GUI wallet framework are released under the terms of the **MIT License**. See [COPYING](COPYING) for details.
- The Proof-of-Useful-Stake (PoUS) consensus engine, AI Worker subsystem, Attestation registry, and Glyph Carrier extensions are Copyright © Quavence DAO, licensed under the **Business Source License 1.1 (BSL-1.1)** with conversion to MIT on 2030-01-01. See [LICENSE-ADDITIONS](LICENSE-ADDITIONS) for details.
