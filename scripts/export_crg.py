#!/usr/bin/env python3
"""Read-only Horizon project -> instance-aware CRG graph. Python 3.9+, stdlib only."""
import argparse
import json
import sys
from collections import defaultdict
from pathlib import Path


SCHEMA = "horizon-crg-graph/1"


class ExportError(ValueError):
    pass


def read_json(path):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ExportError("duplicate JSON key: " + key)
            result[key] = value
        return result
    try:
        with Path(path).open(encoding="utf-8") as stream:
            return json.load(stream, object_pairs_hook=unique)
    except (OSError, ValueError) as error:
        raise ExportError("{}: {}".format(path, error)) from error


def key(path, uuid):
    return "/".join(tuple(path) + (uuid,))


class Pool:
    """Explicit pool order: project cache first, then user supplied pool roots.

    No pool DB updates, network access or global registry changes.
    """
    def __init__(self, roots):
        self.items = {}
        self.files = set()
        for root in roots:
            for category, kind in (("entities", "entity"), ("units", "unit")):
                folder = Path(root) / category
                current = {}
                for path in sorted(folder.rglob("*.json")):
                    self.files.add(path.resolve())
                    obj = read_json(path)
                    if obj.get("type") != kind or not obj.get("uuid"):
                        raise ExportError("invalid {} file: {}".format(kind, path))
                    uid = (kind, obj["uuid"])
                    if uid in current and current[uid] != obj:
                        raise ExportError("conflicting {} UUID in pool {}: {}".format(kind, root, obj["uuid"]))
                    current[uid] = obj
                for uid, obj in current.items():
                    self.items.setdefault(uid, obj)

    def get(self, kind, uid):
        try:
            return self.items[(kind, uid)]
        except KeyError as error:
            raise ExportError("missing {} {}; provide its pool using --pool".format(kind, uid)) from error


class Exporter:
    def __init__(self, project, metadata=None, pools=()):
        self.input = Path(project).resolve()
        data = read_json(self.input)
        if data.get("type") == "project":
            self.root = self.input.parent
            self.blocks_file = self.root / data.get("blocks_filename", "blocks.json")
            pool_path = self.root / data.get("pool_directory", "pool")
            self.pool = Pool([pool_path] + list(pools))
            self.index = read_json(self.blocks_file)
        elif data.get("type") == "blocks":
            self.blocks_file = self.input
            self.root = self.input.parent
            self.pool = Pool([self.root / "pool"] + list(pools))
            self.index = data
        else:
            raise ExportError("expected a current Horizon .hprj or blocks.json file")
        self.metadata = read_json(metadata) if metadata else {"schema_version": 1}
        if self.metadata.get("schema_version") != 1:
            raise ExportError("unsupported CRG metadata schema_version")
        self.blocks, self.nodes, self.nets = {}, {}, {}
        self.parent, self.top_ports, self.instances, self.diagnostics = {}, [], [], []
        self.used_components, self.used_entities = set(), set()

    def diagnostic(self, severity, code, source, message):
        self.diagnostics.append(dict(severity=severity, code=code, source=source, message=message))

    def find(self, uid):
        trail = []
        while self.parent[uid] != uid:
            trail.append(uid)
            uid = self.parent[uid]
        for item in trail:
            self.parent[item] = uid
        return uid

    def union(self, a, b):
        a, b = self.find(a), self.find(b)
        if a != b:
            self.parent[max(a, b)] = min(a, b)

    def block(self, uid):
        if uid not in self.blocks:
            try:
                entry = self.index["blocks"][uid]
            except KeyError as error:
                raise ExportError("missing block definition " + uid) from error
            data = read_json(self.root / entry["block_filename"])
            if data.get("uuid") != uid:
                raise ExportError("block UUID differs from blocks.json: " + uid)
            self.blocks[uid] = data
        return self.blocks[uid]

    def visit(self, uid, path=(), names=(), stack=()):
        if uid in stack:
            raise ExportError("recursive block hierarchy at " + "/".join(path))
        if len(path) > 10:
            raise ExportError("hierarchy exceeds Horizon's 10-instance-path limit")
        block = self.block(uid)
        entry = self.index["blocks"][uid]
        source = dict(block_uuid=uid, instance_path=list(path), file=entry["block_filename"])
        self.instances.append(dict(id="/".join(path), name="/".join(names), **source))
        local = block.get("nets", {})
        seen_names = set()
        for nid, net in sorted(local.items()):
            full = key(path, nid)
            self.parent[full] = full
            self.nets[full] = dict(id=full, uuid=nid, name=net.get("name", ""),
                                   is_power=net.get("is_power", False), **source)
            if net.get("name"):
                if net["name"] in seen_names:
                    self.diagnostic("warning", "duplicate_net_name", full, "Same name does not imply connectivity")
                seen_names.add(net["name"])
            if not path and net.get("is_port"):
                self.top_ports.append(dict(id=full, name=net.get("name", ""), net=full,
                                           direction=net.get("port_direction", "bidirectional")))
            # Native Block::flatten treats power nets as global by UUID, not name.
            if path and net.get("is_power"):
                if nid in self.nets and self.nets[nid]["is_power"]:
                    self.union(full, nid)
                else:
                    self.diagnostic("error", "unresolved_global_power", full,
                                    "Power UUID has no matching top-level power net")
        refs = set()
        top = self.block(self.index["top_block"])
        mapping = top.get("block_instance_mappings", {}).get("/".join(path), {})
        for cid, comp in sorted(block.get("components", {}).items()):
            full = key(path, cid)
            entity = self.pool.get("entity", comp["entity"])
            spec = self.metadata.get("entities", {}).get(comp["entity"], {})
            override = self.metadata.get("components", {}).get(full, {})
            if spec:
                self.used_entities.add(comp["entity"])
            if full in self.metadata.get("components", {}):
                self.used_components.add(full)
            info = mapping.get("components", {}).get(cid, {})
            refdes = info.get("refdes", comp.get("refdes", ""))
            if refdes in refs:
                self.diagnostic("error", "duplicate_refdes", full, "Duplicate component refdes in instance")
            refs.add(refdes)
            ports = {}
            for gid, gate in sorted(entity["gates"].items()):
                unit = self.pool.get("unit", gate["unit"])
                for pid, pin in sorted(unit["pins"].items()):
                    pkey = gid + "/" + pid
                    connection = comp.get("connections", {}).get(pkey)
                    net = connection.get("net") if connection is not None else None
                    if net is not None and net not in local:
                        raise ExportError("{} port {} refers to missing net {}".format(full, pkey, net))
                    pmeta = spec.get("ports", {}).get(pkey, {})
                    width = pmeta.get("width")
                    if width is not None and (type(width) is not int or width < 1):
                        raise ExportError("port width must be a positive integer: " + full + "/" + pkey)
                    direction = pin["direction"]
                    alt = comp.get("alt_pins", {}).get(pkey, {})
                    if alt:
                        if alt.get("use_custom_name"):
                            direction = alt["custom_direction"]
                        else:
                            # Avoid substituting a guessed direction for a native accumulator result.
                            self.diagnostic("warning", "alternate_pin_direction", full + "/" + pkey,
                                            "Alternate-name direction accumulation requires native resolution")
                            direction = "unresolved"
                    ports[pkey] = dict(name=pin["primary_name"], gate=gate["name"],
                                       direction=direction, role=pmeta.get("role", "unclassified"), width=width,
                                       net=key(path, net) if net is not None else None,
                                       connection_state=("connected" if net is not None else
                                                         "explicit_nc" if connection is not None else "unconnected"))
                    if net is None and pmeta.get("required", False):
                        self.diagnostic("error", "required_port_unconnected", full + "/" + pkey,
                                        "Required CRG port is not connected")
            for pkey in set(comp.get("connections", {})) - set(ports):
                raise ExportError("unknown gate/pin connection on " + full + ": " + pkey)
            for pkey in set(spec.get("ports", {})) - set(ports):
                raise ExportError("metadata refers to nonexistent port on " + full + ": " + pkey)
            params = dict(spec.get("parameters", {}))
            params.update(override.get("parameters", {}))
            self.nodes[full] = dict(id=full, uuid=cid, refdes=refdes, instance_name="/".join(names),
                                   entity_uuid=comp["entity"], entity_name=entity["name"],
                                   kind=spec.get("kind", "unclassified"), parameters=params,
                                   controls=override.get("controls", {}), ports=ports,
                                   nopopulate=info.get("nopopulate", comp.get("nopopulate", False)), **source)
            if not spec:
                self.diagnostic("warning", "missing_crg_model", full,
                                "Topology exported; behavior cannot be inferred from symbol or name")
        for iid, inst in sorted(block.get("block_instances", {}).items()):
            child = self.block(inst["block"])
            child_path = path + (iid,)
            self.visit(inst["block"], child_path, names + (inst["refdes"],), stack + (uid,))
            for port, conn in sorted(inst.get("connections", {}).items()):
                if port not in child.get("nets", {}) or not child["nets"][port].get("is_port"):
                    raise ExportError("block instance connection refers to non-port: " + key(child_path, port))
                if conn["net"] is not None:
                    if conn["net"] not in local:
                        raise ExportError("block instance refers to missing parent net")
                    self.union(key(child_path, port), key(path, conn["net"]))
            for port, net in child.get("nets", {}).items():
                if net.get("is_port") and not inst.get("connections", {}).get(port, {}).get("net"):
                    self.diagnostic("warning", "hierarchical_port_unconnected", key(child_path, port),
                                    "Child port has no parent connection; kept as an isolated net")

    def run(self):
        self.visit(self.index["top_block"])
        groups = defaultdict(list)
        for nid, net in sorted(self.nets.items()):
            groups[self.find(nid)].append(net)
        nets = {nid: dict(id=nid, aliases=aliases, endpoints=[]) for nid, aliases in sorted(groups.items())}
        for node in self.nodes.values():
            for pid, port in node["ports"].items():
                if port["net"] is not None:
                    port["net"] = self.find(port["net"])
                    nets[port["net"]]["endpoints"].append(dict(node=node["id"], port=pid,
                                                              direction=port["direction"], role=port["role"],
                                                              width=port["width"]))
        for port in self.top_ports:
            port["net"] = self.find(port["net"])
        for nid, net in nets.items():
            endpoints = net["endpoints"]
            external_inputs = [p for p in self.top_ports if p["net"] == nid and p["direction"] == "input"]
            outputs = [e for e in endpoints if e["direction"] in ("output", "power_output")]
            inputs = [e for e in endpoints if e["direction"] in ("input", "power_input")]
            if len(outputs) + len(external_inputs) > 1:
                self.diagnostic("error", "multiple_drivers", nid, "Net has more than one unconditional driver")
            ambiguous = any(e["direction"] not in ("input", "output", "power_input", "power_output", "not_connected")
                            for e in endpoints)
            external_outputs = [p for p in self.top_ports if p["net"] == nid and p["direction"] == "output"]
            if (inputs or external_outputs) and not outputs and not external_inputs:
                self.diagnostic("warning" if ambiguous else "error", "no_driver", nid,
                                "Net has input loads without a resolved driver")
            roles = {e["role"] for e in endpoints} - {"unclassified"}
            if len(roles) > 1:
                self.diagnostic("error", "mixed_signal_roles", nid, "Connected ports have different CRG signal roles")
            if len({e["width"] for e in endpoints if e["width"] is not None}) > 1:
                self.diagnostic("error", "width_mismatch", nid, "Connected ports have different declared widths")
        for uid in set(self.metadata.get("components", {})) - self.used_components:
            self.diagnostic("error", "stale_component_metadata", uid, "Metadata target is not instantiated")
        for uid in set(self.metadata.get("entities", {})) - self.used_entities:
            self.diagnostic("warning", "unused_entity_model", uid, "Entity model is not used by this hierarchy")
        # Deliberately do not infer clock-transfer arcs from generic connectivity.
        # A legal feedback PLL and an illegal combinational loop need different models.
        self.diagnostic("info", "behavior_analysis_pending", "project",
                        "Frequency propagation, mode reachability, transfer loops and executable test generation are not implemented")
        return dict(schema=SCHEMA, source=dict(project=self.input.name, blocks=self.blocks_file.name),
                    instances=self.instances, nodes=list(self.nodes.values()), nets=list(nets.values()),
                    top_ports=self.top_ports,
                    diagnostics=sorted(self.diagnostics, key=lambda d: (d["severity"], d["code"], d["source"])))


def to_dot(graph):
    quote = json.dumps
    lines = ["digraph CRG {", '  rankdir="LR";']
    for node in graph["nodes"]:
        lines.append("  {} [label={}];".format(quote("c:" + node["id"]),
                     quote(node["instance_name"] + "/" + node["refdes"] + "\n" + node["kind"])))
    for net in graph["nets"]:
        lines.append("  {} [shape=point];".format(quote("n:" + net["id"])))
        for ep in net["endpoints"]:
            a, b = "c:" + ep["node"], "n:" + net["id"]
            if ep["direction"] in ("input", "power_input"):
                a, b = b, a
            attrs = "label=" + quote(ep["port"])
            if ep["direction"] not in ("input", "output", "power_input", "power_output"):
                attrs += ',dir="none"'
            lines.append("  {} -> {} [{}];".format(quote(a), quote(b), attrs))
    for port in graph["top_ports"]:
        a, b = "p:" + port["id"], "n:" + port["net"]
        lines.append("  {} [shape=box,label={}];".format(quote(a), quote(port["name"])))
        if port["direction"] == "output":
            a, b = b, a
        attrs = '' if port["direction"] in ("input", "output") else ' [dir="none"]'
        lines.append("  {} -> {}{};".format(quote(a), quote(b), attrs))
    return "\n".join(lines + ["}", ""])


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("project", type=Path)
    parser.add_argument("--metadata", type=Path)
    parser.add_argument("--pool", type=Path, action="append", default=[])
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--format", choices=("json", "dot"), default="json")
    args = parser.parse_args(argv)
    try:
        exporter = Exporter(args.project, args.metadata, args.pool)
        graph = exporter.run()
        # Refuse to overwrite ANY source input, including metadata and pool files.
        output = args.output.resolve()
        inputs = {exporter.input, exporter.blocks_file.resolve()}
        inputs.update((exporter.root / entry["block_filename"]).resolve()
                      for entry in exporter.index["blocks"].values())
        if args.metadata:
            inputs.add(args.metadata.resolve())
        inputs.update(exporter.pool.files)
        if output in inputs or (output.exists() and any(output.samefile(p) for p in inputs if p.exists())):
            raise ExportError("output would overwrite a project, metadata or pool input")
        content = (json.dumps(graph, indent=2, ensure_ascii=False, allow_nan=False) + "\n"
                   if args.format == "json" else to_dot(graph))
        args.output.write_text(content, encoding="utf-8")
        errors = sum(d["severity"] == "error" for d in graph["diagnostics"])
        print("{} components, {} nets, {} structural errors -> {}".format(
            len(graph["nodes"]), len(graph["nets"]), errors, args.output), file=sys.stderr)
        return 1 if errors else 0
    except (ExportError, KeyError, TypeError, OSError, ValueError, AttributeError) as error:
        print("CRG export failed: " + str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
