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

## Platform acceptance

Source is published on `crg/verification-foundation`. Master and the separate `codex/crg-verification-foundation` branch were left unchanged.

Linux acceptance passed: https://github.com/DoudouMee/horizon_crg/actions/runs/39102702497

- Full native Horizon editor and CRG executables compiled on Ubuntu 22.04.
- Analytical regressions passed, including all 24 known functional / DFT scenarios.
- Actual native loading, connectivity expansion, saving and reopening preserved 15 components and matching scenarios.
- Xvfb launched the real GTK editor, selected XTAL and opened its CRG properties and report. JSON/DOT/CSV exported through the file chooser exactly matched the headless results.
- The UI changed XTAL from 24 MHz to 12 MHz, applied and saved it. The actual native loader reopened the edited project and verified functional clock frequencies halved while DFT frequencies stayed unchanged. The UI worked on an isolated copy; the delivered example remains at 24 MHz.
- The stripped delivery executables passed the native round-trip check again. The Linux archive is about 15 MB and requires Ubuntu/Horizon shared runtime dependencies; it is not a universal standalone AppImage.

Windows UCRT64 acceptance passed: https://github.com/DoudouMee/horizon_crg/actions/runs/39102425830

- Full native editor and CRG tools compiled.
- All 24 example scenarios and fault/hierarchy regressions passed.
- Native load/expand/save/reopen checks passed.
- The stripped Windows package included 161 runtime DLLs. Both CRG executables passed with MSYS2/UCRT removed from PATH; pool preparation also passed.
- The Windows GUI has not been exercised on user hardware or by the CI GUI test. Linux GUI acceptance is separate evidence, not Windows UI acceptance.

Deliveries: `horizon-crg-windows.zip` (108,805,198 bytes) and `horizon-crg-linux-ubuntu22.04.tar.gz` (15,345,285 bytes). The Linux delivery came from run 39102505264; subsequent source changes through the full UI acceptance run only strengthened its test script. Both packages use the pool-only preparation mode so incomplete CRG projects can reopen for correction. Binaries and the complete Linux acceptance archive have been retained with downloadable copies in the workspace outputs.

## Build issues resolved

- Removed the unavailable Ubuntu 22.04 `cppzmq-dev` package, matching the upstream dependency list.
- Switched Windows dependencies to the maintained MSYS2 UCRT64 environment.
- Kept the full JSON definition out of a widely included component header to avoid an upstream Glib overload ambiguity. Native on-disk CRG metadata remains JSON and undo/copy retains value semantics.
- Linked the native acceptance helper with the text renderer implementation required by the native schematic model.
- Added compiler caches, PE dependency closure checks, isolated Windows package execution and binary stripping/verification on both platforms.
- Added PoDoFo 1.x API compatibility to the existing native PDF exporter, retaining the prior 0.10 API path and the separate Linux 0.9 implementation. PDF units are now checked early by the compile probe.
- Separated pool preparation from full verification in the launcher, allowing incomplete edits to reopen.

## Remaining product work

1. Exercise Windows GUI behavior on user hardware, plus broader interactive drawing, hierarchy and undo/redo workflows.
2. Replace JSON property tabs with dedicated graphical forms and improve scenario browsing.
3. Add executable verification adapters/UVM and temporal checks once their timing/register contracts are specified.

This checkpoint completes the first runnable verification foundation and its automated acceptance. It does not claim the full intended CRG product is finished.

The properties UI is an initial JSON-based editor. Dynamic switching/glitch checks, reset test bypass, executable register adapters and automatic UVM generation remain future work. Current scenarios describe steady-state candidates and explicit setup requirements.

Detailed interfaces and current model limitations: [docs/crg.md](docs/crg.md).
