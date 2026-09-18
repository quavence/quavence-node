# Quavence Core Release Notes — v15.1.1 (Protocol 70015)

Quavence Core version `15.1.1` is a maintenance and consensus calibration release for the desktop GUI wallet and AI Worker subsystems. It introduces calibrated PoUS boost tiers, per-worker task credit isolation in the GUI, strict Base58 payout address preflight validation, fallback address safeguards, and cleanly isolated wallet editions.

---

## What's New in v15.1.1

### 1. PoUS Staking Boost Tier Calibration
- Calibrated GUI and RPC boost calculations to match canonical consensus tiers in `src/pos.cpp` (`GetWorkerPoUSBoost`):
  - **Tier 0 (0 tasks / 24h)**: **0%** staking weight boost (Standby)
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
- **`quavence-qt-pous-allinone-v15.1.1.exe`** (44.8 MB) — PoUS AI Worker GUI Wallet + Glyphs + Embedded Tor Daemon **(Flagship)**
- **`quavence-qt-standard-v15.1.1.exe`** (44.7 MB) — Classic Qt GUI Wallet + PoUS Glyphs + Embedded Tor Daemon **(Recommended)**
- **`quavence-qt-clearnet-v15.1.1.exe`** (34.4 MB) — Classic Qt GUI Wallet + PoUS Glyphs (Clearnet / External Tor)
- **`quavence-qt-pous-worker-clearnet-v15.1.1.exe`** (34.6 MB) — PoUS AI Worker GUI Wallet + Glyphs (Clearnet / External Tor)
- **`quavenced-v15.1.1.exe`** (9.9 MB) — Headless Node Daemon
- **`quavence-cli-v15.1.1.exe`** (3.8 MB) — RPC Command-Line Interface

### Linux (x86_64)
- **`quavence-qt-v15.1.1-x86_64.AppImage`** (35.6 MB) — Self-Contained Standalone AppImage (Embedded Qt, Plugins & Libs) **(Recommended)**
- **`quavence-qt-pous-allinone-v15.1.1-linux-x64.tar.gz`** (5.6 MB) — Linux PoUS AI Worker Qt GUI Wallet
- **`quavence-qt-standard-v15.1.1-linux-x64.tar.gz`** (5.6 MB) — Linux Qt GUI Wallet (Classic + PoUS Glyphs)
- **`quavenced-v15.1.1-linux-x64.tar.gz`** (2.0 MB) — Headless Node Daemon for VPS, Docker, and Ubuntu servers
- **`quavence-cli-v15.1.1-linux-x64.tar.gz`** (0.2 MB) — RPC Command-Line Interface

---

## Official SHA-256 Checksums

```text
c6c529436a04b41ad75818e411d43cf868b979b559c9c3e942e2eaf7245a665c  quavence-qt-pous-allinone-v15.1.1.exe
580206c11db443d10e7fd872b4496d4b5e2fcf6f3e95ff6df8ffc25478cdc58a  quavence-qt-standard-v15.1.1.exe
24979c17425f7d9c3d357d75d92d426e14e06c68a30434bb5ccc79cf9f7a358d  quavence-qt-clearnet-v15.1.1.exe
484c7908f01b0debd7204b1356a393c5f567391afc5bcbb13c2cd18c157c408a  quavence-qt-pous-worker-clearnet-v15.1.1.exe
a595cdb05f705eef7270d38ca84c5a903a3a83fbee72714c192537d8e6d9bcab  quavenced-v15.1.1.exe
3fe25e9978bea6965c9daad87f7328a6b3fd596df39187314e10c7ae8ba96532  quavence-cli-v15.1.1.exe

4eba0a2c1a48cde5ba2a82bd52af61693728f13703412022f3ed673719378649  quavence-qt-pous-allinone-v15.1.1-linux-x64.tar.gz
848f81c5564d065e9b2441ad2d993eeaedc4dd2ce97d638fdbd9f04a349ad58f  quavence-qt-standard-v15.1.1-linux-x64.tar.gz
d4eae2b3cf5e3d37b4c3c7ac0d6916b42a9c8030a2ba95df83c2a90d03eec906  quavenced-v15.1.1-linux-x64.tar.gz
bef4299738727e90fac6a78332d28a5f928381b7ea07504f549f9ea5918eb41e  quavence-cli-v15.1.1-linux-x64.tar.gz

3c7f0c41db5bf2a3bd435c1af2f9a2f756dd0e13424f41f4cc6ad874e800a9d2  quavence-qt-v15.1.1-x86_64.AppImage
```
