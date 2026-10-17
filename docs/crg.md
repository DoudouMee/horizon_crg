# Horizon CRG verification foundation

This branch extends the **native Horizon schematic editor**. It preserves the existing canvas, wiring, sheet/block hierarchy and undo/redo. CRG component metadata is stored in each native component as `crg`. No RTL is generated.

## Current scope

- Clock/control/reset sources, PLL with lock, two-input functional/DFT clock MUX, fixed/programmed divider, ICG with test enable, logic, reset combination, reset synchronizer and observation points.
- Hierarchical expansion with instance UUID identity and parent/child port-net merging.
- Structural diagnostics: missing role/pin/net/port, unconnected pins, duplicate names, multiple drivers, undriven nets, width/type mismatch, cycles and invalid parameter domains.
- Frequency, gating and reset steady-state expectations for bounded control combinations. Functional, DFT shift and DFT capture modes are present in the example.
- Versioned JSON with original snapshot, nodes, nets, aliases, source sheet/symbol coordinates, controls/bindings, diagnostics, scenarios and coverage. DOT and CSV are companion views.
- Unknown/missing primitive behavior stays unknown. Structural errors make scenarios invalid. Scenario enumeration is capped at 256 (configurable in the C++ API up to 4096) and explicitly reports truncation.

**This is a foundation for review, not the finished CRG tool.** Automatic UVM sequences, dynamic/glitch assertions, register access adapters, temporal legality checks, reset test bypass and richer custom semantics remain future work. Custom symbols can be drawn using the native pool, while `custom` primitives are exported with unknown behavior. Metadata editing currently uses JSON tabs; graphical forms are still needed.

## Build / start

Linux target: Ubuntu 22.04 or compatible, with the native Horizon dependencies listed in `.github/workflows/crg.yml`. The Linux archive requires these shared libraries; it is not a universal AppImage. Windows packaging includes Horizon runtime DLLs.

```sh
meson setup build --buildtype=debugoptimized
meson compile -C build
meson compile -C build horizon-crg-tests
./build/horizon-crg-tests examples/crg/demo/blocks.json examples/crg/demo/pool
./build/horizon-crg-native examples/crg/demo/blocks.json examples/crg/demo/pool validation
HORIZON_POOL="$PWD/examples/crg/demo/pool" ./build/horizon-imp -c examples/crg/demo/blocks.json examples/crg/demo/pictures
```

For a packaged build: Windows double-click `open-demo.bat`; Linux run `bash open-demo.sh`. The preparation tool indexes the example pool and checks native load → connectivity expansion → save → reopen → expectation equality. It writes round-trip files under `validation`.

The diagram uses named native nets to connect short wire stubs. Place/move/connect objects using standard Horizon operations. Select one component and open the hamburger menu → **CRG component properties…**. Edit its kind, semantic roles and parameters. Apply, save, reopen and check that the properties are retained. Properties belong to the block definition; edits affect all its instances.

Open hamburger menu → **CRG verification analysis…** to inspect diagnostics, scenarios and provenance, then export JSON / DOT / CSV. Reports use the current in-memory model, including unsaved edits. The existing Export PDF menu exports the drawing.

## Headless interface

```sh
./build/horizon-crg examples/crg/demo/blocks.json examples/crg/demo/pool verification
```

Produces `verification.json`, `.dot`, `.csv`. Exit 0: structural checks passed; 1: structural errors; 2: input/output failure. Exit 0 does not guarantee every scenario is complete. The headless reader requires the entities/units to be present in the supplied pool directory; native GUI export resolves installed/included pools through Horizon.

`horizon.crg.verification`, schema version 1: `instances`, `nodes`, `nets`, `controls`, `diagnostics`, `scenarios`, `coverage`, `valid_structure`, `snapshot`. Node IDs are `top-block-uuid/instance-uuid/.../component-uuid`. Names are labels and never used as identity. Native single-bit pins default to width 1; `crg.pin_attributes[gate/pin].width` can declare a bus width and `signal_type` can declare clock/reset/control.

## Semantics and limitations

- Roles refer to exact native `gate_uuid/pin_uuid` values shown in Available native pins.
- `control_source`/`reset_source.parameters`: explicit integer `values`, globally unique `key`, and a `binding` identifying a register field or testbench signal. Bindings are descriptive, not executable bus transactions.
- `clock_source.parameters.frequency_hz`; `pll.parameters.multiply/divide` and an explicitly connected `lock` control. Unlocked PLL output is unspecified.
- `mux.parameters.select_map`: encoded select → input role. `switch_protocol` describes the required setup; the tool does not prove that protocol or infer glitch-free switching.
- `divider.parameters.divisor`, or `divide_map` with a `cfg` input. Zero/non-integer divisors are errors.
- `gate`: active-high `en OR test_en`, with optional test_en. This models steady-state gating, not ICG latch timing.
- `reset_combine`/`logic`: electrical-level and/or/xor/not; reset synchronization requires explicit input/output polarity, stages ≥ 2 and async_assert. Deasserted reset with a stopped synchronization clock remains unknown because initial history is absent. Running-clock release includes a start-asserted and edge-count precondition.
- `clock_output`/`reset_output.parameters.binding` identifies an observation signal.
- Top block project properties may contain `crg.modes`: a JSON-encoded object, mode name → fixed control key/value assignments. Unfixed controls enumerate their declared values. This does not infer mutually exclusive functional states.
- Case steps describe desired control settings, not a safe chronological register write sequence. Every case requires declared setup protocols before steady-state observations. Frequency tolerance, settling time, pulse width, PLL lock time, mode transition legality and CDC/RDC correctness require additional specifications.
- Native PCB no-populate flags are exported with a review warning; they do not remove circuitry automatically.
- Round-trip with this CRG build is required; upstream Horizon does not understand the added CRG fields.

## Acceptance

The source includes analytical regressions for 24 known example scenarios, wrong divider settings, static cycles, multiple drivers, width/type mismatch, unknown PLL lock, missing role/pin, missing switching contract, mode errors, reset polarity, enumeration limits and two instances of one hierarchical definition. `horizon-crg-native` separately checks the real native loaders and serializers. Linux CI launches the actual editor under Xvfb and retains screenshots; screenshots alone do not verify editing/property/export interactions.
