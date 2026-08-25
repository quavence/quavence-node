# Quavence Node & PoUS AI Worker Qt Wallet (QVNC)

[![Release](https://img.shields.io/badge/release-v15.0.0-blue.svg)](https://github.com/dtd-tosh/quavence-node/releases)
[![Consensus](https://img.shields.io/badge/consensus-PoS3.0%20%2B%20PoUS-emerald.svg)](https://quavence.com)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](COPYING)
[![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20Windows-lightgrey.svg)](https://quavence.com)

**Quavence (QVNC)** is a decentralized security and compute network powered by **Proof-of-Useful-Stake (PoUS)**. It combines ultra-fast PoS 3.0 staking with autonomous on-chain AI worker attestation, knowledge-base RAG verification, and real-time staking yield boosts.

---

## 🌟 Key Features

* 🤖 **Built-in AI Worker (All-in-One GUI):** Seamlessly connects to local LLM providers (LM Studio, Ollama, OpenAI-compatible APIs) to process RAG knowledge base checks, task composer synthesis, and AI consensus tasks.
* ⚡ **Proof-of-Useful-Stake (PoUS) & Dynamic Boost:** Stakers running active AI workers receive dynamic staking boosts (up to **+50%**) based on verified task execution within the on-chain attestation registry.
* 🛡️ **DevFee & AI Staking Pools:** Native protocol-level fee routing to automated security and compute reward pools.
* 🔀 **Advanced Coin Control & UTXO Split:** Integrated splitting for staking optimization directly within the GUI wallet without arbitrary reserve locks.
* 🖥️ **Dual Binary Architecture:**
  * **All-in-One PoUS Qt Wallet:** Full GUI wallet + AI Worker node + Staking boost dashboard.
  * **Standard Qt Wallet & Headless Node:** Lightweight classic GUI wallet (`quavence-qt`), daemon (`quavenced`), and CLI (`quavence-cli`).

---

## 🚀 Quick Start & Downloads

Pre-built standalone binaries for **Windows (Win64)** and **Linux (x86_64)** are available on the [Releases](https://github.com/dtd-tosh/quavence-node/releases) page:

| Platform | Binary | Description |
| :--- | :--- | :--- |
| **Windows** | `quavence-qt-pous-allinone-v15.0.exe` | All-in-One GUI Wallet + AI Worker |
| **Windows** | `quavence-qt-standard-v15.0.exe` | Classic Standard GUI Wallet |
| **Windows** | `quavenced-v15.0.exe` / `quavence-cli-v15.0.exe` | Headless Daemon & CLI |
| **Linux** | `quavence-qt-pous-allinone-v15.0` | Linux All-in-One Qt Wallet |
| **Linux** | `quavence-qt-standard-v15.0` | Linux Classic Standard Qt Wallet |
| **Linux** | `quavenced-v15.0` / `quavence-cli-v15.0` | Linux Headless Daemon & CLI |

---

## 🛠️ Building From Source

### Prerequisites (Ubuntu / Debian / WSL2)

```bash
sudo apt-get update
sudo apt-get install -y build-essential libtool autotools-dev automake pkg-config \
    libssl-dev libevent-dev bsdmainutils python3 \
    libboost-system-dev libboost-filesystem-dev libboost-chrono-dev \
    libboost-program-options-dev libboost-test-dev libboost-thread-dev \
    libqt5gui5 libqt5core5a libqt5dbus5 qttools5-dev qttools5-dev-tools \
    libprotobuf-dev protobuf-compiler libqrencode-dev libdb5.3++-dev libdb5.3++
```

### Build Linux Binaries

```bash
./autogen.sh
mkdir build-linux && cd build-linux
../configure --prefix=/ --disable-bench --disable-tests --enable-wallet --with-gui=qt5
make -j4
```

### Cross-Compile Windows (Win64) Binaries with MinGW

```bash
# Build dependencies
cd depends
make HOST=x86_64-w64-mingw32 -j4
cd ..

# Build Windows binaries
mkdir build-win64 && cd build-win64
CONFIG_SITE="$PWD/../depends/x86_64-w64-mingw32/share/config.site" ../configure --prefix=/ --disable-bench --disable-tests --enable-wallet --with-gui=qt5
make -j4
```

---

## ⚙️ Configuration & AI Worker Setup

Create `quavence.conf` in your data directory:
* **Linux:** `~/.quavence/quavence.conf`
* **Windows:** `%APPDATA%\Quavence\quavence.conf`

```ini
server=1
listen=1
daemon=1
rpcuser=your_username
rpcpassword=your_secure_password
rpcallowip=127.0.0.1
rpcport=15715
port=15714
staking=1
```

### AI Worker Configuration (GUI Wallet):
1. Launch **Quavence All-in-One Wallet**.
2. Navigate to the **AI Worker** tab.
3. Enter your **Node Token** (obtained from the Quavence Community Hub).
4. Set the **Inference Endpoint** (e.g. `http://127.0.0.1:1234/v1` for LM Studio).
5. Click **Start Worker** to begin claiming tasks, earning QVNC rewards, and unlocking the +50% PoUS Staking Boost.

---

## 📄 License

Quavence is released under the terms of the MIT license. See [COPYING](COPYING) for more information.
