# Quavence Core Release Notes

This directory contains the release notes for all published versions of Quavence Core (quavenced, quavence-cli, and quavence-qt).

---

## 🌟 Quavence 15.x Line (Mainnet Architecture & PoUS)

- [**Quavence Core v15.1.0**](release-notes/release-notes-15.1.0.md) (Protocol 70015)
  - **Native Tor v3 Hidden Services (BIP155 / ED25519-V3)** with SHA3-256 checksums and automated SAFECOOKIE onion service creation.
  - **Standalone All-in-One Windows GUI Wallet** with embedded background Tor daemon for zero-configuration privacy and censorship resistance.
  - **Permanent Built-in Tor & Clearnet Seednodes** in `chainparams.cpp`.
  - Official multi-platform binary releases and SHA-256 verification sums.

- [**Quavence Core v15.0.0**](release-notes/release-notes-15.0.0.md) (Protocol 70015)
  - **Proof-of-Useful-Stake (PoUS) Stake Boost v1.1** (+20% to +50% dynamic tier boosts bound to on-chain AI attestations).
  - **Zero-I/O Pubkey Extraction** from `scriptSig` in `TxSpendsFromKeyID` for rapid consensus verification without disk lookups.
  - **`CStakeCache` Architecture** streamlining high-frequency PoS 3.0 block minting.
  - **PoUS All-in-One Qt GUI Wallet** with built-in AI Worker tab and real-time accrued rewards telemetry.

---

## Upstream Legacy Notes (Historical Reference)

Legacy release notes from the upstream base codebases (Bitcoin Core and Blackcoin More) are preserved in the [release-notes/](release-notes/) subdirectory for cryptographic and protocol heritage tracking.
