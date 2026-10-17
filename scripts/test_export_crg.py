"""Regression fixtures follow Horizon's saved project/entity/unit/block schema.

Run from the repository root: python -m unittest discover -s scripts -p 'test_export_crg.py' -v
"""
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import uuid

spec = importlib.util.spec_from_file_location("export_crg", Path(__file__).with_name("export_crg.py"))
crg = importlib.util.module_from_spec(spec)
spec.loader.exec_module(crg)


def uid(name):
    return str(uuid.uuid5(uuid.NAMESPACE_URL, "horizon-crg-test/" + name))


def write(path, obj):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(obj, indent=2) + "\n", encoding="utf-8")


class ProjectFixture:
    def __init__(self, root):
        self.root = root
        self.project = root / "demo.hprj"
        self.gate, self.pin_in, self.pin_out = uid("gate"), uid("in"), uid("out")
        self.entity, self.unit, self.top = uid("entity"), uid("unit"), uid("top")
        self.c1, self.c2 = uid("c1"), uid("c2")
        self.n0, self.n1, self.n2 = uid("n0"), uid("n1"), uid("n2")
        self.nc = uid("net-class")
        self.blocks = {self.top: self.block(self.top)}
        top = self.blocks[self.top]
        top["nets"] = {self.n0: self.net("clk_in", True, "input"),
                       self.n1: self.net("mid"), self.n2: self.net("clk_out", True, "output")}
        top["components"] = {self.c1: self.component("U1", self.n0, self.n1),
                             self.c2: self.component("U2", self.n1, self.n2)}
        self.metadata = {"schema_version": 1, "entities": {self.entity: {
            "kind": "clock_buffer", "ports": {
                self.port(self.pin_in): {"role": "clock", "width": 1, "required": True},
                self.port(self.pin_out): {"role": "clock", "width": 1, "required": True}}}}}
        write(root / "pool/entities/buffer.json", dict(type="entity", uuid=self.entity,
              name="CRG buffer", manufacturer="", prefix="U", tags=[], gates={self.gate: dict(
                  name="main", suffix="", swap_group=0, unit=self.unit)}))
        write(root / "pool/units/buffer.json", dict(type="unit", uuid=self.unit, name="CRG buffer",
              manufacturer="", pins={self.pin_in: dict(primary_name="I", direction="input", swap_group=0),
                                     self.pin_out: dict(primary_name="O", direction="output", swap_group=0)}))

    def port(self, pin):
        return self.gate + "/" + pin

    def net(self, name, port=False, direction="bidirectional"):
        return dict(name=name, is_port=port, port_direction=direction, is_power=False, net_class=self.nc)

    def block(self, bid):
        return dict(uuid=bid, name="block", net_class_default=self.nc,
                    net_classes={self.nc: {"name": "default"}}, nets={}, components={}, buses={},
                    block_instances={}, block_instance_mappings={}, group_names={}, tag_names={}, project_meta={})

    def component(self, ref, source, target):
        return dict(entity=self.entity, refdes=ref, value="", connections={
            self.port(self.pin_in): {"net": source}, self.port(self.pin_out): {"net": target}})

    def child(self):
        child = uid("child")
        a, b = uid("a"), uid("b")
        data = self.block(child)
        data["nets"] = {a: self.net("I", True, "input"), b: self.net("O", True, "output")}
        data["components"] = {self.c1: self.component("BUF", a, b)}
        self.blocks[child] = data
        return child, a, b

    def save(self):
        write(self.project, dict(type="project", uuid=uid("project"), blocks_filename="blocks.json",
                                pool_directory="pool", pool_uuid=uid("pool"), board_filename="board.json"))
        entries = {}
        for bid, block in self.blocks.items():
            filename = "blocks/" + bid + ".json"
            write(self.root / filename, block)
            entries[bid] = dict(block_filename=filename, schematic_filename="", symbol_filename="")
        write(self.root / "blocks.json", dict(type="blocks", top_block=self.top, blocks=entries))
        write(self.root / "crg.json", self.metadata)

    def run(self):
        self.save()
        return crg.Exporter(self.project, self.root / "crg.json").run()


class ExportTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.f = ProjectFixture(Path(self.temp.name))

    def codes(self, graph):
        return {d["code"] for d in graph["diagnostics"]}

    def test_native_uuid_connections_and_external_driver(self):
        graph = self.f.run()
        self.assertEqual((len(graph["nodes"]), len(graph["nets"])), (2, 3))
        self.assertFalse([d for d in graph["diagnostics"] if d["severity"] == "error"])
        self.assertIn("behavior_analysis_pending", self.codes(graph))

    def test_two_instances_of_one_definition_preserve_identity(self):
        f = self.f
        child, a, b = f.child()
        top = f.blocks[f.top]
        top["components"] = {}
        i1, i2 = uid("instance1"), uid("instance2")
        top["block_instances"] = {
            i1: dict(block=child, refdes="A", connections={a: {"net": f.n0}, b: {"net": f.n1}}),
            i2: dict(block=child, refdes="B", connections={a: {"net": f.n1}, b: {"net": f.n2}})}
        graph = f.run()
        self.assertEqual({n["id"] for n in graph["nodes"]}, {i1 + "/" + f.c1, i2 + "/" + f.c1})
        self.assertEqual(len(graph["nets"]), 3)
        mid = next(n for n in graph["nets"] if any(x["uuid"] == f.n1 for x in n["aliases"]))
        self.assertEqual(len(mid["endpoints"]), 2)
        self.assertEqual(len(mid["aliases"]), 3)

    def test_same_names_in_separate_instances_do_not_short(self):
        f = self.f
        child, _, _ = f.child()
        top = f.blocks[f.top]
        top["components"], top["nets"] = {}, {}
        top["block_instances"] = {uid("i1"): dict(block=child, refdes="A", connections={}),
                                  uid("i2"): dict(block=child, refdes="B", connections={})}
        graph = f.run()
        self.assertEqual(len(graph["nets"]), 4)
        self.assertIn("hierarchical_port_unconnected", self.codes(graph))

    def test_missing_pool_fails_instead_of_omitting_ports(self):
        self.f.save()
        (self.f.root / "pool/units/buffer.json").unlink()
        with self.assertRaisesRegex(crg.ExportError, "missing unit"):
            crg.Exporter(self.f.project).run()

    def test_unknown_net_is_fatal(self):
        self.f.blocks[self.f.top]["components"][self.f.c1]["connections"][self.f.port(self.f.pin_in)]["net"] = uid("missing")
        with self.assertRaisesRegex(crg.ExportError, "missing net"):
            self.f.run()

    def test_recursive_hierarchy_is_fatal(self):
        f = self.f
        f.blocks[f.top]["block_instances"][uid("self")] = dict(block=f.top, refdes="SELF", connections={})
        with self.assertRaisesRegex(crg.ExportError, "recursive"):
            f.run()

    def test_multiple_drivers_and_no_driver(self):
        f = self.f
        f.blocks[f.top]["components"][f.c2]["connections"][f.port(f.pin_out)]["net"] = f.n0
        codes = self.codes(f.run())
        self.assertIn("multiple_drivers", codes)
        self.assertIn("no_driver", codes)

    def test_explicit_nc_distinguished_from_absent_connection(self):
        f = self.f
        comp = f.blocks[f.top]["components"][f.c1]
        comp["connections"][f.port(f.pin_in)] = {"net": None}
        del comp["connections"][f.port(f.pin_out)]
        node = next(n for n in f.run()["nodes"] if n["id"] == f.c1)
        self.assertEqual(node["ports"][f.port(f.pin_in)]["connection_state"], "explicit_nc")
        self.assertEqual(node["ports"][f.port(f.pin_out)]["connection_state"], "unconnected")

    def test_stale_metadata_and_unsupported_schema(self):
        f = self.f
        f.metadata["components"] = {uid("deleted"): {"parameters": {"divide": 4}}}
        self.assertIn("stale_component_metadata", self.codes(f.run()))
        f.metadata["schema_version"] = 9
        f.save()
        with self.assertRaisesRegex(crg.ExportError, "schema_version"):
            crg.Exporter(f.project, f.root / "crg.json")

    def test_per_instance_override_uses_uuid_path(self):
        f = self.f
        f.metadata["entities"][f.entity]["parameters"] = {"divide": 2}
        f.metadata["components"] = {f.c2: {"parameters": {"divide": 8}}}
        nodes = {n["id"]: n for n in f.run()["nodes"]}
        self.assertEqual(nodes[f.c1]["parameters"]["divide"], 2)
        self.assertEqual(nodes[f.c2]["parameters"]["divide"], 8)

    def test_mixed_roles_and_widths(self):
        f = self.f
        ports = f.metadata["entities"][f.entity]["ports"]
        ports[f.port(f.pin_in)].update(role="reset", width=2)
        codes = self.codes(f.run())
        self.assertIn("mixed_signal_roles", codes)
        self.assertIn("width_mismatch", codes)

    def test_unknown_width_is_not_assumed_to_be_one(self):
        f = self.f
        f.metadata["entities"][f.entity]["ports"] = {}
        graph = f.run()
        self.assertTrue(all(p["width"] is None for n in graph["nodes"] for p in n["ports"].values()))
        self.assertNotIn("width_mismatch", self.codes(graph))

    def test_nonport_hierarchical_connection_fails(self):
        f = self.f
        child, a, _ = f.child()
        f.blocks[child]["nets"][a]["is_port"] = False
        f.blocks[f.top]["block_instances"][uid("i")] = dict(block=child, refdes="CHILD", connections={a: {"net": f.n0}})
        with self.assertRaisesRegex(crg.ExportError, "non-port"):
            f.run()

    def test_export_is_deterministic_and_read_only(self):
        f = self.f
        f.save()
        before = {p: hashlib.sha256(p.read_bytes()).digest() for p in f.root.rglob("*.json")}
        first = crg.Exporter(f.project, f.root / "crg.json").run()
        second = crg.Exporter(f.project, f.root / "crg.json").run()
        self.assertEqual(first, second)
        self.assertEqual(before, {p: hashlib.sha256(p.read_bytes()).digest() for p in before})

    def test_duplicate_json_key_rejected(self):
        path = self.f.root / "bad.json"
        path.write_text('{"schema_version":1,"schema_version":2}', encoding="utf-8")
        with self.assertRaisesRegex(crg.ExportError, "duplicate JSON key"):
            crg.read_json(path)

    def test_pool_conflict_and_project_cache_priority(self):
        f = self.f
        src = f.root / "pool/units/buffer.json"
        different = crg.read_json(src)
        different["name"] = "other"
        write(src.with_name("duplicate.json"), different)
        with self.assertRaisesRegex(crg.ExportError, "conflicting"):
            f.run()
        src.with_name("duplicate.json").unlink()
        other = f.root / "included"
        write(other / "units/buffer.json", different)
        pool = crg.Pool([f.root / "pool", other])
        self.assertEqual(pool.get("unit", f.unit)["name"], "CRG buffer")

    def test_cli_writes_diagnostics_but_returns_nonzero(self):
        f = self.f
        f.blocks[f.top]["components"][f.c1]["connections"][f.port(f.pin_in)]["net"] = None
        f.save()
        output = f.root / "out.json"
        result = subprocess.run([sys.executable, str(Path(crg.__file__)), str(f.project),
                                 "--metadata", str(f.root / "crg.json"), "--output", str(output)], capture_output=True)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn("required_port_unconnected", self.codes(crg.read_json(output)))

    def test_cli_refuses_overwriting_inputs(self):
        f = self.f
        f.save()
        target = f.root / "crg.json"
        content = target.read_bytes()
        self.assertEqual(crg.main([str(f.project), "--metadata", str(target), "--output", str(target)]), 2)
        self.assertEqual(content, target.read_bytes())

    def test_dot_retains_fanout_without_inventing_driver(self):
        graph = self.f.run()
        dot = crg.to_dot(graph)
        self.assertIn("digraph CRG", dot)
        self.assertEqual(dot.count("shape=point"), 3)
        self.assertEqual(dot.count(" -> "), 6)

    def test_output_cannot_overwrite_a_custom_project_pool(self):
        f = self.f
        f.save()
        custom = f.root / "custom-pool"
        (f.root / "pool").rename(custom)
        project = crg.read_json(f.project)
        project["pool_directory"] = "custom-pool"
        write(f.project, project)
        target = custom / "units/buffer.json"
        content = target.read_bytes()
        self.assertEqual(crg.main([str(f.project), "--output", str(target)]), 2)
        self.assertEqual(target.read_bytes(), content)

    def test_hard_link_to_input_is_not_overwritten(self):
        import os
        f = self.f
        f.save()
        target = f.root / "alias.json"
        try:
            os.link(f.project, target)
        except OSError:
            self.skipTest("hard links unavailable on this filesystem")
        content = f.project.read_bytes()
        self.assertEqual(crg.main([str(f.project), "--output", str(target)]), 2)
        self.assertEqual(f.project.read_bytes(), content)


if __name__ == "__main__":
    unittest.main()
