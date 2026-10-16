# Guidance for new coding agents (MOD branch)

This file applies to work on the `mod` branch.

**Read [docs/AGENT-HANDOFF-2026-10-16.md](docs/AGENT-HANDOFF-2026-10-16.md) first**, then `docs/mod-README.md` and `docs/history/COMPLETE-DEVELOPMENT-LOG-2026-10-16.md`.
The handoff is a dated snapshot: fetch the newest `mod` HEAD, CI results and build workflow pin before making changes.

- **Never merge modified MOD economics into `main`** (the original v3.11 reconstruction).
- MOD rules are centralized in `include/rich4/mod_rules.hpp`; legacy DAT financial/inventory previews need the checksum-bound `.DAT.mod` sidecar (`include/rich4/mod_save.hpp`). Do not silently truncate 64-bit money or wide shares.
- Changes involving money, tools, stock quantities, AI or bank/stock UI need boundary tests, original-save compatibility review and `ctest --output-on-failure`; check field width at all call sites.
- For APKs, edit the pinned `RICH4_UPSTREAM_SHA` in the build repo's **`.github/workflows/build-richman4-android-mod.yml`** only; avoid the similarly named outdated MOD workflow and the mainline workflow.
- Android MOD package `io.divby0.rich4.mod` is distinct from vanilla. Reusing a signing certificate requires its persistent private key; never commit secrets.
- Do not change the already-accepted shareholder gift-card behavior without a specific user request.
- The latest code and test/build results supersede dated handoff assertions. Clearly label any Android/original-game visual flow not physically verified.
