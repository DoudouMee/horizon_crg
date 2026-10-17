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

## Platform validation in progress

The user confirmed upload and platform builds on 2026-10-18. Remote branch `crg/verification-foundation` was created at `6db57114b6bf4062f3b29f2122c5f3f093c564ab`. Master was unchanged. Another existing branch, `codex/crg-verification-foundation`, contains a different earlier Python exporter; it was inspected and left unchanged.

Initial Linux Actions run: https://github.com/DoudouMee/horizon_crg/actions/runs/39068643168

Initial Windows Actions run: https://github.com/DoudouMee/horizon_crg/actions/runs/39068643262

The first Linux job stopped during dependency installation because Ubuntu 22.04 has no separate `cppzmq-dev` package. Removed that unnecessary package, matching the upstream Horizon dependency list. Windows was still installing dependencies at this checkpoint. No full native build has passed yet.

## Remaining acceptance

1. Inspect platform compile, native round-trip and UI logs; fix actual failures.
2. Download and visually inspect native-editor evidence. Exercise property changes, save/reopen and exports before calling the UI accepted.
3. Supply downloadable platform builds with the example and launchers. Open a draft PR if appropriate; do not merge without instruction.

Detailed interfaces and current model limitations: [docs/crg.md](docs/crg.md).
