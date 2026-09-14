# Quavence Core Release Notes — v15.1.0 (Protocol 70015)

Quavence Core version `15.1.0` is a major networking and privacy release introducing native **Tor v3 (ED25519-V3 / BIP155)** hidden services, **SHA3-256 (Keccak-f[1600])** checksum verification, **automatic onion seednode routing**, and an **All-in-One single executable distribution** with a bundled background Tor daemon for Windows.

---

## What's New in v15.1.0

### 1. Native Tor v3 Hidden Services (BIP155 / ED25519-V3)
- Full support for 56-character `.onion` addresses with 2-byte SHA3-256 checksums and version byte `0x03`.
- Automatic creation and advertising of persistent v3 onion hidden services via `ADD_ONION` with SAFECOOKIE authentication.
- Private key persistence in `<DataDir>/onion_v3_private_key`.

### 2. Standalone All-in-One Windows GUI Wallet
- Bundles the official, statically linked `tor.exe` binary directly inside the Windows PE resource section (`RCDATA`).
- Automatically extracts and manages Tor in the background with zero user configuration.
- Process lifecycle bound to Windows kernel Job Object (`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`) to prevent orphaned/zombie processes.

### 3. Built-in Permanent Seednodes
- Added permanent onion seednode (`kalwfcd7ia3gcwksq7yipu3b2lseibic6ytmawkbvq7odlleic6lifqd.onion`) and VPS seednode (`89.125.130.116`) to `vSeeds` in `chainparams.cpp`.
- Enables true P2P network resilience: new wallets connect directly over Tor even if the central clearnet VPS is offline or blocked.

---

## Release Binaries

### Windows (x86_64)
- **`quavence-qt-standard-v15.1.exe`** (44.7 MB) — Classic Qt GUI Wallet + PoUS Glyphs + Embedded Tor Daemon **(Recommended)**
- **`quavence-qt-clearnet-v15.1.exe`** (34.5 MB) — Classic Qt GUI Wallet + PoUS Glyphs (Clearnet / External Tor)
- **`quavence-qt-pous-allinone-v15.1.exe`** (44.8 MB) — PoUS AI Worker GUI Wallet + Glyphs + Embedded Tor Daemon **(Flagship)**
- **`quavence-qt-pous-worker-clearnet-v15.1.exe`** (34.6 MB) — PoUS AI Worker GUI Wallet + Glyphs (Clearnet / External Tor)
- **`quavenced-v15.1.exe`** (9.9 MB) — Headless Node Daemon
- **`quavence-cli-v15.1.exe`** (3.8 MB) — RPC Command-Line Interface

### Linux (x86_64)
- **`quavence-qt-standard-v15.1`** (11.3 MB) — Linux Qt GUI Wallet (Classic + PoUS Glyphs)
- **`quavence-qt-pous-allinone-v15.1`** (11.5 MB) — Linux PoUS AI Worker Qt GUI Wallet
- **`quavenced-v15.1`** (5.0 MB) — Headless Node Daemon for VPS, Docker, and Ubuntu servers
- **`quavence-cli-v15.1`** (0.4 MB) — RPC Command-Line Interface
- **`quavence-qt-v15.1.0-x86_64.AppImage`** (37.0 MB) — Self-Contained Standalone AppImage (Embedded Qt, Plugins & Libs) **(Recommended)**

---

## Official SHA-256 Checksums

```text
fc6207b56e345465ff1a6c98e8b0532f39ae748a8ccaf4c95c0395393f9d61c2  quavence-qt-pous-allinone-v15.1.exe
0a919961d176d8d8a780ce962a5a7399333314caa5fe5c9ce9e96a9e0084f633  quavence-qt-standard-v15.1.exe
7d9972ae96f00bc895c0a0795d68c47e0dab73c6a189111a76d0ef6df3badcf4  quavence-qt-clearnet-v15.1.exe
a7d8026b7bdee6fb952dcb9af07134c3647b16fa7eb53c66c6fdede379341b15  quavence-qt-pous-worker-clearnet-v15.1.exe
928df2b34872919bdc00e564a2bc14f6cd4d997cfe545be8e86349b81d59ef82  quavenced-v15.1.exe
f2f539f4b02790e603d85b44263404511741d1ffa43616ac76787cfb789cef62  quavence-cli-v15.1.exe

1635ddb2c20ab840710bdcfdcf701be87c60e21959b20c8211375ea32d13d348  quavence-qt-pous-allinone-v15.1
0e20560cd3674174babd3fc4c81384c802fbc39b9565517d96caf0ad362b645d  quavence-qt-standard-v15.1
be3766312256e655d6983d0a144ef8673f756d12649a36e145c15d69294ac4f3  quavenced-v15.1
35d4750db3fa2c936ddd4af62bdff9fb2929d3569fd5d1d438a221df8bfcf0d6  quavence-cli-v15.1

b218326af514b56c426944ff7ae4fd468c01a4b471bf550978172d0204c49a78  quavence-qt-v15.1.0-x86_64.AppImage
```

