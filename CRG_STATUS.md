# CRG continuation checkpoint — 2026-10-18

## Agreed scope

Native Horizon extension for chip CRG verification, structured data and test-scenario generation. Windows and Linux. Reuse native hierarchy, sheets, net editing and custom symbols. Include functional/DFT controls. No RTL generation.

## Source baseline and changes

Repository: https://github.com/DoudouMee/horizon_crg

Audited baseline: `7790bbaafe012d279934f3a673be73766e7508a5` on master. No existing CRG implementation was present. Local branch: `crg/verification-foundation`.

The native component now persists a `crg` object. The schematic editor has CRG property and report menu entries. `src/crg/analysis.cpp` is shared by the editor and a headless exporter. `src/crg/native.cpp` snapshots the actual native model. A deterministic native demonstration includes 15 components, 13 nets and 24 functional / DFT shift / DFT capture scenarios.

Local, actually completed validation:

- C++17 Clang compilation of analysis/export and analysis tests, with `-Wall -Wextra -Werror` and Horizon's `JSON_USE_IMPLICIT_CONVERSIONS=0`.
- Exact clock and reset expectations for all 24 demonstration scenarios, plus faults, missing contracts, mode/domain issues, repeated child block instances and enumeration limits.
- JSON, DOT and CSV exporter execution.
- clang-format 19 formatting and Git whitespace checks.
- Python generator/test script syntax checks.

**Not yet verified:** full GTK/native editor compilation, native load/expand/save/reopen executable, Linux GUI interaction and Windows/Linux packaging. The corresponding source and GitHub Actions jobs have been prepared, but have not run. The properties UI is an initial JSON-based editor. Dynamic switching/glitch checks, reset test bypass and UVM generation remain future work.

## Current blocker

GitHub source reads work. A local clone was recovered through the available host execution path. The cloud-project listing endpoint failed; no concrete cloud-environment identity is known.

Direct Git push could not authenticate. The GitHub connector's `create_tree` action was then rejected by automatic approval review because it uploads modified source to `DoudouMee/horizon_crg` without explicit authorization naming that destination. No connector-created tree, commit, branch or PR was created, and no Actions build was started. Do not retry any upload through an alternative path until the user explicitly authorizes it.

## Next authorized step after approval

1. With explicit authorization to upload to `DoudouMee/horizon_crg`, publish local changes onto `crg/verification-foundation`, retaining the baseline parent and leaving master unchanged.
2. Run the prepared GitHub Actions Linux and Windows builds. Inspect compile, native round-trip and UI logs; fix actual failures.
3. Download and visually inspect native-editor evidence. Exercise property changes, save/reopen and exports before calling the UI accepted.
4. Supply downloadable platform builds with the example and launchers. Open a draft PR if appropriate; do not merge without instruction.

Detailed interfaces and current model limitations: [docs/crg.md](docs/crg.md).
