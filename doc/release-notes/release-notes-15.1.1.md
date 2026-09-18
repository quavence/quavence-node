# Quavence Core Release Notes — v15.1.1 (Protocol 70015)

Quavence Core version `15.1.1` is a maintenance and consensus calibration release for the desktop GUI wallet and AI Worker subsystems. It introduces calibrated PoUS boost tiers, per-worker task credit isolation in the GUI, strict Base58 payout address preflight validation, and fallback address safeguards.

---

## What's New in v15.1.1

### 1. PoUS Staking Boost Tier Calibration
- Calibrated GUI and RPC boost calculations to match canonical consensus tiers in `src/pos.cpp` (`GetWorkerPoUSBoost`):
  - **Tier 1 (1–5 tasks / 24h)**: **+10%** staking weight boost (1.10x multiplier)
  - **Tier 2 (6–20 tasks / 24h)**: **+25%** staking weight boost (1.25x multiplier)
  - **Tier 3 (21+ tasks / 24h)**: **+50%** staking weight boost (1.50x multiplier)

### 2. Isolated Worker Task Credit Counters
- Fixed UI counter display bug where `GetAiAttestationsCountInWindow` (network total) was mistakenly displayed as individual worker completed tasks.
- The GUI and Overview pages now query `GetWorkerCreditsInWindow(workerKey)` for the worker's own attested tasks within the 1,440-block rolling window.

### 3. Strict Preflight Validation & Typed Status
- Refactored `WorkerActivationPreflight` to return a strongly typed `enum class PreflightStatus`.
- Integrated `CBitcoinAddress::IsValid()` Base58 validation for worker payout addresses.
- Blocked registration of the default DAO fallback treasury address (`SXbKabu...`) during worker preflight.

---

## Release Binaries

### Windows (x86_64)
- **`quavence-qt-standard-v15.1.1.exe`** (44.8 MB) — Classic Qt GUI Wallet + PoUS Glyphs + Embedded Tor Daemon **(Recommended)**
- **`quavence-qt-clearnet-v15.1.1.exe`** (44.8 MB) — Classic Qt GUI Wallet + PoUS Glyphs (Clearnet / External Tor)
- **`quavence-qt-pous-allinone-v15.1.1.exe`** (44.8 MB) — PoUS AI Worker GUI Wallet + Glyphs + Embedded Tor Daemon **(Flagship)**
- **`quavence-qt-pous-worker-clearnet-v15.1.1.exe`** (44.8 MB) — PoUS AI Worker GUI Wallet + Glyphs (Clearnet / External Tor)
- **`quavenced-v15.1.1.exe`** (9.9 MB) — Headless Node Daemon
- **`quavence-cli-v15.1.1.exe`** (3.8 MB) — RPC Command-Line Interface

### Linux (x86_64)
- **`quavence-qt-v15.1.1-x86_64.AppImage`** (36.5 MB) — Self-Contained Standalone AppImage (Embedded Qt, Plugins & Libs) **(Recommended)**
- **`quavence-qt-standard-v15.1.1-linux-x64.tar.gz`** (5.6 MB) — Linux Qt GUI Wallet (Classic + PoUS Glyphs)
- **`quavence-qt-pous-allinone-v15.1.1-linux-x64.tar.gz`** (5.6 MB) — Linux PoUS AI Worker Qt GUI Wallet
- **`quavenced-v15.1.1-linux-x64.tar.gz`** (2.0 MB) — Headless Node Daemon for VPS, Docker, and Ubuntu servers
- **`quavence-cli-v15.1.1-linux-x64.tar.gz`** (0.2 MB) — RPC Command-Line Interface

---

## Official SHA-256 Checksums

```text
8ed74bf8fc771662d2b4af2e16329432eef26551af2c555b2d4b09e6b7abb9c6  quavence-qt-pous-allinone-v15.1.1.exe
768a0311ec0b0cd21eec6cc191fafe7f26fd7c9446d3b6aba6e97f9cabd3aace  quavence-qt-standard-v15.1.1.exe
768a0311ec0b0cd21eec6cc191fafe7f26fd7c9446d3b6aba6e97f9cabd3aace  quavence-qt-clearnet-v15.1.1.exe
768a0311ec0b0cd21eec6cc191fafe7f26fd7c9446d3b6aba6e97f9cabd3aace  quavence-qt-pous-worker-clearnet-v15.1.1.exe
a595cdb05f705eef7270d38ca84c5a903a3a83fbee72714c192537d8e6d9bcab  quavenced-v15.1.1.exe
3fe25e9978bea6965c9daad87f7328a6b3fd596df39187314e10c7ae8ba96532  quavence-cli-v15.1.1.exe

fb1cfb9b8bd686310b20d4220bf03fb44f156f9c923970a320ab2316e74225a7  quavence-qt-pous-allinone-v15.1.1-linux-x64.tar.gz
83e3d605b3bd40f00f436fdb7d2028c4c632239068bda532e3389d1ba711fdae  quavence-qt-standard-v15.1.1-linux-x64.tar.gz
d4eae2b3cf5e3d37b4c3c7ac0d6916b42a9c8030a2ba95df83c2a90d03eec906  quavenced-v15.1.1-linux-x64.tar.gz
bef4299738727e90fac6a78332d28a5f928381b7ea07504f549f9ea5918eb41e  quavence-cli-v15.1.1-linux-x64.tar.gz

fd6bdc371d4597e53bba992edbf4e2df141a78e9d6fbae3cc8206625452307bd  quavence-qt-v15.1.1-x86_64.AppImage
```
