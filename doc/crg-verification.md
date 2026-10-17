# CRG verification extension: baseline and accepted scope

## Purpose and status

This fork is being adapted for chip clock/reset-generation verification. The design drawing is a source of structured connectivity and explicitly declared behavior for constructing verification scenarios. RTL generation is out of scope. Target platforms are Windows and Linux.

Baseline inspected: `7790bbaafe012d279934f3a673be73766e7508a5`, the fork's `master` at the start of this work. The repository tree had 2,391 entries and no CRG-specific source or previous exporter. Earlier reports of completed CRG work are not accepted as evidence for this baseline.

This change adds a **read-only command-line data extraction milestone**, not a finished GUI. It uses Horizon's actual project, blocks, entity, gate, pin and instance-connection formats. It does not replace Horizon or introduce a separate drawing application.

## Accepted product requirements

* Native Horizon editing, custom modules, hierarchy, multipage drawings and editable parameters.
* Clock sources, PLLs, dividers, clock selectors, gates and output observation points.
* Reset sources, combinational reset logic, synchronizers and output observation points.
* DFT clock inputs, functional/test selection, scan enable, test mode, gate test enables and reset test bypasses.
* Extensible element models with control-signal/register bindings, encodings, mode constraints, transition requirements and observation points.
* Versioned JSON plus graphical exports; no RTL generation.
* Structural checks, explicitly modelled frequency/path propagation, reset dependency checks and derived test scenarios.
* Offline Windows and Linux operation with runnable packages and native GUI acceptance tests.

## Verified code findings

`src/project/project.cpp` resolves `blocks_filename` and `pool_directory` relative to the project file. `src/blocks/blocks.cpp` maps block UUIDs to definition/schematic/symbol files. A component connection key is `gate_uuid/pin_uuid`; its value is `{ "net": "net_uuid" }`, or an explicit null net. Pins are resolved via entity gates and unit definitions, not pin names.

`src/block/block_instance.cpp` connects a child's **port net UUID** to a parent's net. `src/block/block.cpp` retains UUID instance paths when flattening. Reusing a block requires distinct instance identities. Equal local net names must not short separate instances. Native global power nets are identified by UUID, not name.

`Component::serialize` writes a fixed set of fields. Arbitrary CRG fields inserted into native JSON are not preserved by existing GUI save. This milestone therefore stores semantic annotations in an explicit sidecar, addressed by UUID paths. A later GUI editor must edit that same sidecar and detect stale references; it must not pretend unpersisted attributes are supported.

The upstream Windows workflow includes a push-triggered external mirror deployment. The initial development commit uses `[skip ci]` to avoid triggering inherited workflows. No mirror, release, permissions or default-branch changes are part of this milestone.

## Run the exporter

Requires Python 3.9 or later; no third-party packages. Save the project in Horizon before exporting. Unsaved UI changes are not visible to the exporter.

```sh
python scripts/export_crg.py design/design.hprj --metadata design/crg.json --output graph.json
python scripts/export_crg.py design/blocks.json --pool /path/to/included/pool --output graph.dot --format dot
python -m unittest discover -s scripts -p 'test_export_crg.py' -v
```

On Windows, `py -3` can be used in place of `python`; quote paths containing spaces. Supply `--pool` repeatedly for included pools whose objects are not present in the project cache. Project/cache objects take precedence, then the supplied pool order. Conflicting duplicate definitions inside one pool are fatal. No pool databases, registries or source files are updated.

Exit codes: `0` means extraction succeeded without detected structural errors; `1` means a graph was written with structural errors; `2` means extraction/output failed. **Exit 0 does not mean CRG behavior is verified.** Warnings and unimplemented analysis are recorded in `diagnostics`.

## Sidecar contract, version 1

```json
{
  "schema_version": 1,
  "entities": {
    "ENTITY_UUID": {
      "kind": "clock_mux",
      "parameters": {},
      "ports": {
        "GATE_UUID/PIN_UUID": {
          "role": "clock",
          "width": 1,
          "required": true
        }
      }
    }
  },
  "components": {
    "INSTANCE_UUID/COMPONENT_UUID": {
      "parameters": {},
      "controls": {}
    },
    "TOP_LEVEL_COMPONENT_UUID": {
      "parameters": {},
      "controls": {}
    }
  }
}
```

Identifiers above are placeholders. Use actual UUIDs from the saved project. Entity port roles and widths are annotations; native pin direction remains authoritative. Width is `null` when undeclared. Kind/parameters/controls are preserved data in this milestone, not executed behavior. Component parameters override entity defaults. This version does not provide per-instance port-role or kind overrides.

Unknown components are exported with `unclassified` kind and warnings. Missing component-sidecar references are errors so that stale annotations cannot silently disappear. Alternate pin-name direction accumulation is explicitly unresolved until native direction semantics are integrated.

## Graph contract, version 1

`schema` is `horizon-crg-graph/1`. `instances` records the hierarchy. `nodes` retains each instance-qualified component, native identity, entity identity, refdes, pin connections, native directions, declared roles and parameters. `nets` groups explicitly connected aliases and endpoints; each alias retains block UUID, instance path and relative source file. `top_ports` retains input/output boundaries.

Net IDs are deterministic canonical UUID paths. Adding/removing an alias can change the canonical ID; retain aliases and source UUID paths for matching between revisions. Refdes and net names are display labels, not unique identities. `nopopulate` is retained rather than silently dropping components.

Implemented checks: unknown references, recursive/deep block hierarchy, unconnected required ports, duplicate refdes, duplicate local net names, multi-driver nets, inputs/outputs without resolved drivers, mixed annotated roles, annotated width mismatches and stale sidecar targets. Explicit NC and missing connections remain distinct.

Not yet implemented: bus member expansion, electrical net-tie transfer semantics, sheet coordinates, automatic element palette, GUI sidecar editing, frequency/mode analysis, combinational-transfer cycle classification, reset timing/polarity analysis, test-case generation, UVM integration and packaged GUI binaries. Generic connectivity is not sufficient to classify feedback loops; no blanket loop result is claimed.

## Acceptance sequence

1. **Data extraction (this change):** native project -> JSON/DOT; verify shared hierarchy and malformed projects; prove sources unchanged.
2. **GUI and element library:** load an actual CRG sample, place/connect/edit elements, annotate controls, save, close and reopen. Capture results on Windows and Linux.
3. **Mode-aware models:** distinguish clock/reset/control arcs; derive reachable paths and frequencies using explicit control values, legal combinations, PLL lock and transition constraints. Missing models produce unresolved results.
4. **Scenario generation:** output preconditions, stimuli, observation points, expected outcomes and source UUID paths. Cover functional and DFT modes, gate override, mux selection, dividers, reset assertion/release and clock dependencies. Bound combinatorial expansion and report coverage gaps.
5. **Delivery:** reproducible Windows/Linux packages, source revision, sample project and operation guide. Validate against an independent simulation model before claiming executable tests.

## Environment and validation evidence

The development host for this milestone was macOS arm64 with Python 3.9.6. Tests exercise native-format fixtures and command-line behavior. They are not native Horizon loader/GUI tests. See `crg-validation.txt` for the local run.

Direct GitHub cloning failed with an SSL connection error. The GitHub connector was used to inspect a fixed revision and preserve selected source files locally. That local directory is a **partial source snapshot**, not a complete Git checkout. No full-repository audit or native full build is claimed.

The Codex project/environment listing interfaces returned errors, and the in-app Codex page was not logged in. The user's configured Codex cloud environment has therefore not been attached or used. The full checkout, dependencies, GUI build and Windows/Linux runs remain required there.

For the cloud environment, start with the development branch associated with this change, run the Python regression command above, then use the dependencies and Meson commands in the existing `.github/workflows/all.yml`. Build on Linux with `meson setup build --buildtype=debug` followed by `meson compile -C build -j2`. Validate the unmodified native baseline before changing the GUI. Windows uses the repository's MSYS2 configuration; remove the external mirror upload from any new fork-specific workflow before enabling it.
